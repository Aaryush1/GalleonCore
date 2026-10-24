// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "HomeWidgets.h"
#include "FpsReader.h"
#include "PresentMonService.h"
#include <QUrl>
#include <QCoreApplication>
#include <QUuid>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSaveFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QLocale>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
static void initHomeWidgets() { Q_INIT_RESOURCE(home_widgets); } // NOLINT(misc-use-anonymous-namespace) -- Qt resource symbol is global.
namespace galleond {
namespace {
QByteArray read(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
QVariantList defaults() {
    QVariantList result;
    const QStringList ids{QStringLiteral("clock"), QStringLiteral("weather"), QStringLiteral("cpu"), QStringLiteral("memory"), QStringLiteral("temperature"), QStringLiteral("downloads"), QStringLiteral("fps")};
    for (int i = 0; i < ids.size(); ++i)
        result.append(QVariantMap{{"id", ids[i]}, {"enabled", i == 0}, {"x", i == 0 ? 30 : 440}, {"y", i == 0 ? 108 : 16 + ((i - 1) % 5) * 68}, {"width", i == 0 ? 400 : 260}, {"height", i == 0 ? 170 : 60}, {"textSize", 32}});
    return result;
}
}
HomeWidgets::HomeWidgets(QObject *parent, QString file, bool draftMode) : QObject(parent), m_capture(file.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/galleoncore/widgets.json.capture.csv")
        : file + QStringLiteral(".capture.csv")),
    m_draftMode(draftMode), m_file(std::move(file)), m_layout(defaults()) {
    initHomeWidgets();
#ifdef Q_OS_WIN
    m_gpuProcess.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    if (m_file.isEmpty()) m_file = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/galleoncore/widgets.json");
    load();
    connect(&m_gpuProcess, &QProcess::readyReadStandardOutput, this, [this] {
        if (m_gpuProcess.bytesAvailable() > 4096) m_gpuProcess.kill();
    });
    connect(&m_gpuProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) { m_gpuTemperature.reset(); m_gpuLoad.reset(); });
    connect(&m_gpuProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus status) {
        m_gpuTemperature.reset(); m_gpuLoad.reset();
        if (code || status != QProcess::NormalExit) {
            if (m_gpuUsesProbe) m_nativeGpuRetryAt = QDateTime::currentSecsSinceEpoch() + 60;
            return;
        }
        if (m_gpuUsesProbe) {
            const auto data = QJsonDocument::fromJson(m_gpuProcess.readAllStandardOutput()).object();
            const auto temperature = data.value("gpu");
            const auto load = data.value("gpuLoad");
            if (temperature.isDouble() && temperature.toDouble() > 0 && temperature.toDouble() < 150) m_gpuTemperature = temperature.toDouble();
            if (load.isDouble() && load.toDouble() >= 0 && load.toDouble() <= 100) m_gpuLoad = load.toDouble();
            m_gpuProvider = data.value("provider").toString().left(32);
            if (!m_gpuTemperature && !m_gpuLoad) m_nativeGpuRetryAt = QDateTime::currentSecsSinceEpoch() + 60;
            return;
        }
        m_gpuProvider = QStringLiteral("nvidia-smi");
        bool ok = false;
        const auto fields = m_gpuProcess.readAllStandardOutput().split('\n').value(0).split(',');
        const double value = fields.value(0).trimmed().toDouble(&ok);
        if (ok && std::isfinite(value) && value > 0 && value < 150) m_gpuTemperature = value;
        const double load = fields.value(1).trimmed().toDouble(&ok);
        if (ok && std::isfinite(load) && load >= 0 && load <= 100) m_gpuLoad = load;
    });
    connect(&m_timer, &QTimer::timeout, this, &HomeWidgets::refresh);
    m_timer.start(2000);
    refresh();
}
void HomeWidgets::load() {
    if (m_draftMode && m_pending) return;
    const QByteArray bytes = read(m_file);
    if (bytes == m_lastFile) return;
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject()) return;
    const auto object = doc.object();
    // Apply known entries only, clamping geometry even for hand-edited files.
    auto layout = defaults();
    const auto saved = object.value(QStringLiteral("layout")).toVariant().toList();
    for (auto &entry : layout) {
        auto item = entry.toMap();
        for (const auto &candidate : saved) {
            const auto c = candidate.toMap();
            if (c.value("id") != item.value("id")) continue;
            const int w = qBound(100, c.value("width", item["width"]).toInt(), 720);
            const int h = qBound(40, c.value("height", item["height"]).toInt(), 384);
            item["width"] = w; item["height"] = h;
            item["x"] = qBound(0, c.value("x").toInt(), 720 - w);
            item["y"] = qBound(0, c.value("y").toInt(), 384 - h);
            item["textSize"] = qBound(12, c.value("textSize", 32).toInt(), 64);
            item["enabled"] = c.value("enabled", item["enabled"]);
        }
        entry = item;
    }
    m_layout = layout;
    const auto location = object.value(QStringLiteral("location")).toObject().toVariantMap();
    if (location != m_location) { m_location = location; m_weatherAt = 0; m_values.remove("weather"); }
    m_gameConfig = object.value(QStringLiteral("gameMode")).toObject().toVariantMap();
    m_fpsConfig = object.value(QStringLiteral("fps")).toObject().toVariantMap();
    m_lastFile = bytes;
}
bool HomeWidgets::save() {
    if (!m_draftMode) return write();
    m_pending = true; m_error.clear(); Q_EMIT changed(); return true;
}
bool HomeWidgets::apply() {
    if (!m_pending) return true;
    if (!write()) return false;
    m_pending = false; Q_EMIT changed(); return true;
}
bool HomeWidgets::write() {
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QSaveFile file(m_file);
    const auto bytes = QJsonDocument(QJsonObject::fromVariantMap({{"version", 1}, {"layout", m_layout}, {"location", m_location}, {"fps", m_fpsConfig}, {"gameMode", m_gameConfig}})).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = tr("Cannot save widgets: %1").arg(file.errorString());
        if (!m_draftMode) { m_layout = defaults(); m_lastFile.clear(); load(); }
        Q_EMIT changed(); return false;
    }
    m_lastFile = bytes; m_error.clear(); Q_EMIT changed(); return true;
}
bool HomeWidgets::setWidget(const QString &id, bool enabled, int x, int y, int width, int height) {
    if (width < 100 || width > 720 || height < 40 || height > 384 || x < 0 || y < 0 || x > 720 - width || y > 384 - height) return false;
    load();
    for (auto &entry : m_layout) {
        auto item = entry.toMap();
        if (item.value("id").toString() != id) continue;
        item["enabled"] = enabled; item["x"] = x; item["y"] = y; item["width"] = width; item["height"] = height;
        entry = item; return save();
    }
    return false;
}
bool HomeWidgets::setWidgetTextSize(const QString &id, int pixels) {
    if (pixels < 12 || pixels > 64) return false;
    load();
    for (auto &entry : m_layout) {
        auto item = entry.toMap();
        if (item.value("id").toString() != id) continue;
        item["textSize"] = pixels; entry = item; return save();
    }
    return false;
}
QString HomeWidgets::recommendedFpsFolder() const { // NOLINT(readability-convert-member-functions-to-static) -- QObject property accessor.
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/galleoncore/fps");
}
QString HomeWidgets::steamLaunchOptions() const {
    QString config = QStringLiteral("autostart_log=1,log_interval=500,output_folder=") + recommendedFpsFolder();
    config.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("MANGOHUD_CONFIG='") + config + QStringLiteral("' mangohud %command%");
}
bool HomeWidgets::prepareFpsFolder() {
    const QString folder = recommendedFpsFolder();
    if (!QDir().mkpath(folder)) {
        m_error = tr("Could not create the FPS log folder."); Q_EMIT changed(); return false;
    }
    return setFpsSource(folder, {});
}
bool HomeWidgets::configureMangoHud() {
    const auto fail = [this](const QString &message) {
        m_error = message; Q_EMIT changed(); return false;
    };
    const QString folder = recommendedFpsFolder();
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/MangoHud");
    const QString path = configDir + QStringLiteral("/MangoHud.conf");
    if (!QDir().mkpath(folder) || !QDir().mkpath(configDir))
        return fail(tr("Could not create the MangoHud configuration or FPS log folder."));
    QByteArray original;
    const bool exists = QFile::exists(path);
    if (exists) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return fail(tr("Could not read the existing MangoHud configuration."));
        original = file.readAll();
        if (file.error() != QFileDevice::NoError) return fail(tr("Could not read the existing MangoHud configuration."));
    }
    // Keep unrelated options and comments, replacing only the three logging keys.
    const QRegularExpression loggingKey(QStringLiteral("^\\s*(autostart_log|log_interval|output_folder)\\s*="));
    QByteArray updated;
    auto lines = original.split('\n');
    if (original.endsWith('\n')) lines.removeLast();
    for (const auto &line : lines) {
        if (!loggingKey.match(QString::fromUtf8(line)).hasMatch()) {
            updated += line;
            updated += '\n';
        }
    }
    if (original.isEmpty()) updated.clear();
    updated += "autostart_log=1\nlog_interval=500\noutput_folder=" + folder.toUtf8() + "\n";
    if (updated != original) {
        if (exists) {
            const QString backup = path + QStringLiteral(".galleon-backup-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
            if (!QFile::copy(path, backup)) return fail(tr("Could not back up the MangoHud configuration."));
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(updated) != updated.size() || !file.commit())
            return fail(tr("Could not save the MangoHud configuration."));
    }
    return setFpsSource(folder, {}) && apply();
}
bool HomeWidgets::setFpsSource(const QString &source, const QString &process) {
    load();
    const auto url = QUrl(source.trimmed());
    const auto path = url.isLocalFile() ? url.toLocalFile() : source.trimmed();
    if (path.size() > 4096 || process.size() > 260 || path.contains(QChar::Null)
        || path.contains(QChar::LineFeed) || process.contains(QChar::LineFeed)
        || (!path.isEmpty() && !QFileInfo(path).isAbsolute())) {
        m_error = tr("Choose an absolute path to a local FPS CSV file or log folder.");
        Q_EMIT changed(); return false;
    }
    m_fpsConfig = {{"source", path}, {"process", process.trimmed()}};
    return save();
}
bool HomeWidgets::setGameOptions(bool automatic, bool localSensors) {
    load(); m_gameConfig["automatic"] = automatic; m_gameConfig["localSensors"] = localSensors;
    return save();
}
bool HomeWidgets::requestGameCapture(bool start) { // NOLINT(readability-convert-member-functions-to-static) -- Windows uses instance state.
#ifndef Q_OS_WIN
    Q_UNUSED(start)
#endif
#ifdef Q_OS_WIN
    if (!m_draftMode) return false;
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QSaveFile request(m_file + QStringLiteral(".capture-request.json"));
    const auto bytes = QJsonDocument(QJsonObject{{"action", start ? "start" : "stop"}, {"at", QDateTime::currentMSecsSinceEpoch()},
        {"id", QUuid::createUuid().toString(QUuid::Id128)}}).toJson(QJsonDocument::Compact);
    if (!request.open(QIODevice::WriteOnly) || request.write(bytes) != bytes.size() || !request.commit()) {
        m_error = tr("Cannot request FPS capture: %1").arg(request.errorString()); Q_EMIT changed(); return false;
    }
    return true;
#else
    return false;
#endif
}
bool HomeWidgets::setCaptureOptions(bool managed, const QString &program) {
    load();
    const auto path = program.trimmed();
    if (path.size() > 4096 || path.contains(QChar::Null) || path.contains(QChar::LineFeed)
        || (!path.isEmpty() && !QFileInfo(path).isAbsolute())) {
        m_error = tr("Choose an absolute path to the PresentMon console executable.");
        Q_EMIT changed(); return false;
    }
    m_gameConfig["managedCapture"] = managed; m_gameConfig["presentMon"] = path;
    return save();
}
bool HomeWidgets::setGameProfile(const QString &process, const QString &title, const QString &image) {
    load();
    auto key = process.trimmed().toLower(); key.replace('\\', '/'); key = key.section('/', -1);
    const auto url = QUrl(image.trimmed());
    const auto path = url.isLocalFile() ? url.toLocalFile() : image.trimmed();
    if (key.isEmpty() || key.size() > 260 || title.size() > 120 || path.size() > 4096
        || (!path.isEmpty() && !QFileInfo(path).isAbsolute())) {
        m_error = tr("Enter a game executable name and an absolute local image path."); Q_EMIT changed(); return false;
    }
    auto profiles = m_gameConfig.value("profiles").toMap();
    if (profiles.size() >= 100 && !profiles.contains(key)) return false;
    profiles[key] = QVariantMap{{"title", title.trimmed()}, {"image", path}};
    m_gameConfig["profiles"] = profiles;
    return save();
}
void HomeWidgets::pollGpu() {
    if (m_gpuProcess.state() != QProcess::NotRunning || QDateTime::currentSecsSinceEpoch() - m_gpuAt < 5) return;
    m_gpuAt = QDateTime::currentSecsSinceEpoch();
#ifdef Q_OS_WIN
    const auto probe = QCoreApplication::applicationDirPath() + QStringLiteral("/galleon-gpu-probe.exe");
#else
    const auto probe = QCoreApplication::applicationDirPath() + QStringLiteral("/galleon-gpu-probe");
#endif
    m_gpuUsesProbe = QFileInfo(probe).isFile() && QDateTime::currentSecsSinceEpoch() >= m_nativeGpuRetryAt;
    const auto command = m_gpuUsesProbe ? probe : QStandardPaths::findExecutable(QStringLiteral("nvidia-smi"));
    if (command.isEmpty()) { m_gpuTemperature.reset(); m_gpuLoad.reset(); return; }
    const int serial = ++m_gpuSerial;
    m_gpuProcess.start(command, m_gpuUsesProbe ? QStringList() : QStringList{QStringLiteral("--query-gpu=temperature.gpu,utilization.gpu"), QStringLiteral("--format=csv,noheader,nounits")});
    QTimer::singleShot(2000, this, [this, serial] { if (serial == m_gpuSerial && m_gpuProcess.state() != QProcess::NotRunning) m_gpuProcess.kill(); });
}
void HomeWidgets::pollGameSensors() {
    if (!m_gameConfig.value("localSensors", false).toBool()) { m_gameSensors.clear(); return; }
    if (m_sensorPending || QDateTime::currentSecsSinceEpoch() - m_sensorAt < 5) return;
    m_sensorAt = QDateTime::currentSecsSinceEpoch(); m_sensorPending = true;
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:8085/data.json")));
    request.setTransferTimeout(2000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::readyRead, this, [reply] { if (reply->bytesAvailable() > qint64(1024) * 1024) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_sensorPending = false; m_gameSensors.clear();
        if (reply->error() == QNetworkReply::NoError && m_gameConfig.value("localSensors").toBool())
            m_gameSensors = GameMode::parseSensors(reply->read(1024 * 1024 + 1));
        reply->deleteLater();
    });
}
bool HomeWidgets::setCity(const QString &city) {
    load();
    const auto name = city.trimmed();
    if (name.size() > 80 || name.contains(QChar::LineFeed) || name.contains(QChar::CarriageReturn)) {
        m_error = tr("City name must fit on one line (80 characters maximum).");
        Q_EMIT changed(); return false;
    }
    m_location.insert(QStringLiteral("city"), name);
    return save();
}
bool HomeWidgets::reset() { m_layout = defaults(); return save(); }
bool HomeWidgets::setLocation(const QString &latitude, const QString &longitude) {
    bool a = false;
    bool b = false;
    const double lat = QLocale::c().toDouble(latitude, &a);
    const double lon = QLocale::c().toDouble(longitude, &b);
    if (!a || !b || !std::isfinite(lat) || !std::isfinite(lon) || std::abs(lat) > 90 || std::abs(lon) > 180) {
        m_error = tr("Enter latitude (-90…90) and longitude (-180…180), using a decimal point."); Q_EMIT changed(); return false;
    }
    m_location.insert(QStringLiteral("latitude"), lat);
    m_location.insert(QStringLiteral("longitude"), lon); m_weatherAt = 0;
    m_values.remove("weather");
    if (!save()) return false;
    weather(); return true;
}
void HomeWidgets::weather() {
    bool enabled = false;
    for (const auto &entry : m_layout) { const auto item = entry.toMap(); if (item["id"] == "weather" && item["enabled"].toBool()) enabled = true; }
    if (!enabled || m_weatherPending || m_location.isEmpty() || QDateTime::currentSecsSinceEpoch() - m_weatherAt < 900) return;
    bool a = false;
    bool b = false;
    const double lat = m_location.value("latitude").toDouble(&a);
    const double lon = m_location.value("longitude").toDouble(&b);
    if (!a || !b || !std::isfinite(lat) || !std::isfinite(lon) || std::abs(lat) > 90 || std::abs(lon) > 180) return;
    const auto location = m_location;
    QNetworkRequest request(QUrl(QStringLiteral("https://api.open-meteo.com/v1/forecast?latitude=%1&longitude=%2&current=temperature_2m,weather_code").arg(lat, 0, 'f', 5).arg(lon, 0, 'f', 5)));
    request.setTransferTimeout(10000);
    m_weatherPending = true; m_weatherAt = QDateTime::currentSecsSinceEpoch();
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, location] {
        m_weatherPending = false;
        if (location == m_location) {
            const auto current = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("current")).toObject();
            if (reply->error() == QNetworkReply::NoError && current.value(QStringLiteral("temperature_2m")).isDouble()) {
                const int code = current.value(QStringLiteral("weather_code")).toInt();
                QString icon = QStringLiteral("☂");
                if (code == 0) icon = QStringLiteral("☀");
                else if (code < 50) icon = QStringLiteral("☁");
                else if (code >= 71 && code <= 77) icon = QStringLiteral("❄");
                m_values["weather"] = icon + QStringLiteral("  ") + QLocale().toString(current.value(QStringLiteral("temperature_2m")).toDouble(), 'f', 1) + QStringLiteral(" °C");
            } else {
                m_values["weather"] = tr("Weather unavailable");
            }
            Q_EMIT changed();
        }
        reply->deleteLater();
    });
}
void HomeWidgets::refresh() {
    const auto previousCaptureStatus = m_captureStatus;
    const auto previousGameConfig = m_gameConfig;
    const auto previousGameState = m_gameState;
    const auto previousFpsConfig = m_fpsConfig;
    const auto previousLocation = m_location;
    const auto previousLayout = m_layout;
    const auto previousValues = m_values;
    load();
    const auto percent = [](double v) { return QLocale().toString(v, 'f', 0) + QStringLiteral(" %"); };
#ifdef Q_OS_WIN
    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const auto number = [](FILETIME v) { return (quint64(v.dwHighDateTime) << 32) | v.dwLowDateTime; };
        const quint64 total = number(kernel) + number(user), idleTime = number(idle);
        if (m_cpuTotal && total > m_cpuTotal) m_values["cpu"] = percent(100.0 * (1.0 - double(idleTime - m_cpuIdle) / double(total - m_cpuTotal)));
        m_cpuTotal = total; m_cpuIdle = idleTime;
    }
    MEMORYSTATUSEX memory{}; memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) m_values["memory"] = percent(memory.dwMemoryLoad);
#else
    const auto fields = read(QStringLiteral("/proc/stat")).split('\n').value(0).simplified().split(' ');
    quint64 total = 0;
    for (int i = 1; i < qMin(9, int(fields.size())); ++i) total += fields[i].toULongLong();
    const quint64 idle = fields.value(4).toULongLong() + fields.value(5).toULongLong();
    if (m_cpuTotal && total > m_cpuTotal) m_values["cpu"] = percent(100.0 * (1.0 - double(idle - m_cpuIdle) / double(total - m_cpuTotal)));
    m_cpuTotal = total; m_cpuIdle = idle;
    quint64 memoryTotal = 0;
    quint64 available = 0;
    for (const auto &line : read(QStringLiteral("/proc/meminfo")).split('\n')) {
        const auto parts = line.simplified().split(' ');
        if (parts.value(0) == "MemTotal:") memoryTotal = parts.value(1).toULongLong();
        if (parts.value(0) == "MemAvailable:") available = parts.value(1).toULongLong();
    }
    if (memoryTotal) m_values["memory"] = percent(100.0 * (1.0 - double(available) / double(memoryTotal)));
    m_values.remove("temperature");
    const QDir hwmon(QStringLiteral("/sys/class/hwmon"));
    for (const auto &sensor : hwmon.entryList({QStringLiteral("hwmon*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QDir dir(hwmon.filePath(sensor));
        const auto name = read(dir.filePath(QStringLiteral("name"))).trimmed();
        if (name != "coretemp" && name != "k10temp" && name != "zenpower") continue;
        for (const auto &input : dir.entryList({QStringLiteral("temp*_input")}, QDir::Files)) {
            bool ok = false; const double temperature = read(dir.filePath(input)).trimmed().toDouble(&ok) / 1000.0;
            if (ok && temperature > -30 && temperature < 150) { m_values["temperature"] = QLocale().toString(temperature, 'f', 1) + QStringLiteral(" °C"); break; }
        }
        if (m_values.contains("temperature")) break;
    }
#endif
    const QDir downloads(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    const auto partials = downloads.entryInfoList({QStringLiteral("*.part"), QStringLiteral("*.crdownload")}, QDir::Files | QDir::NoSymLinks);
    m_values["downloads"] = tr("%1 file(s)").arg(partials.size());
    if (!m_values.contains("weather")) m_values["weather"] = m_location.isEmpty() ? tr("Set location in Bosun") : tr("Waiting for weather");
    bool capture = m_gameConfig.value("automatic", true).toBool();
    for (const auto &entry : m_layout) {
        const auto item = entry.toMap();
        if (item.value("id").toString() == "fps" && item.value("enabled").toBool()) capture = true;
    }
    FpsSample sample;
#ifdef Q_OS_WIN
    const bool managed = m_gameConfig.value("managedCapture", true).toBool();
    if (!m_draftMode) {
        if (capture && managed) m_captureTarget = GameCapture::detectGame(m_gameConfig.value("profiles").toMap().keys(), m_captureTarget.pid);
        else m_captureTarget = {};
        if (!m_captureTarget.pid) m_captureStoppedPid = 0;
        QFile request(m_file + QStringLiteral(".capture-request.json"));
        const auto bytes = request.open(QIODevice::ReadOnly) ? request.read(4096) : QByteArray();
        if (bytes != m_lastCaptureRequest) {
            m_lastCaptureRequest = bytes;
            const auto object = QJsonDocument::fromJson(bytes).object();
            const auto age = QDateTime::currentMSecsSinceEpoch() - object.value("at").toVariant().toLongLong();
            if (!object.value("id").toString().isEmpty() && age >= 0 && age < 10000) {
                const bool start = object.value("action").toString() != QStringLiteral("stop");
                m_captureStoppedPid = start ? 0 : m_captureTarget.pid;
                if (start) m_capture.retry();
            }
        }
        const auto option = m_gameConfig.value("presentMon").toString();
        const auto now = QDateTime::currentMSecsSinceEpoch();
        if (!m_captureDiscoveryAt || option != m_capturePathOption || now - m_captureDiscoveryAt >= 10000
            || (!m_captureProgram.isEmpty() && !QFileInfo(m_captureProgram).isFile())) {
            m_captureInstallation = findPresentMonService();
            m_captureProgram = QCoreApplication::applicationDirPath() + QStringLiteral("/galleon-presentmon-probe.exe");
            m_capturePathOption = option; m_captureDiscoveryAt = now;
        }
        const auto program = m_captureInstallation.value("installed").toBool() && QFileInfo(m_captureProgram).isFile() ? m_captureProgram : QString();
        const bool stopped = m_captureTarget.pid && m_captureTarget.pid == m_captureStoppedPid;
        m_capture.tick(capture && managed && !stopped, m_captureTarget, program, m_captureInstallation.value("apiPath").toString());
        if (capture && managed && !stopped) sample = m_capture.sample();
        m_captureStatus = m_capture.status();
        for (auto it = m_captureInstallation.cbegin(); it != m_captureInstallation.cend(); ++it) m_captureStatus[it.key()] = it.value();
        if (!m_captureInstallation.value("error").toString().isEmpty()) m_captureStatus["message"] = m_captureInstallation.value("error");
        else if (stopped) m_captureStatus["message"] = tr("Capture FPS arrêtée pour ce jeu. Cliquez sur Démarrer pour reprendre.");
        else if (!QFileInfo(m_captureProgram).isFile()) m_captureStatus["message"] = tr("Galleon FPS helper missing. Reinstall Galleon.");
    }
    if (capture && !managed) sample = readFpsLog(m_fpsConfig.value("source").toString(), m_fpsConfig.value("process").toString());
#else
    if (capture) sample = readFpsLog(m_fpsConfig.value("source").toString(), m_fpsConfig.value("process").toString());
#endif
    m_values["fps"] = sample.fps ? QLocale().toString(*sample.fps, 'f', 0) + QStringLiteral(" FPS") : sample.status;
    pollGameSensors();
    auto sensors = m_gameSensors;
    if (!sensors.contains("cpuLoad")) {
        bool ok = false;
        const auto load = QLocale().toDouble(m_values.value("cpu").toString().section(' ', 0, 0), &ok);
        if (ok && load >= 0 && load <= 100) sensors["cpuLoad"] = load;
    }
#ifndef Q_OS_WIN
    if (!sensors.contains("cpu")) {
        bool ok = false; const auto text = m_values.value("temperature").toString().section(' ', 0, 0);
        const auto value = QLocale().toDouble(text, &ok);
        if (ok) sensors["cpu"] = value;
    }
#endif
    if (sample.fps) {
        pollGpu();
        if (m_gpuLoad && QDateTime::currentSecsSinceEpoch() - m_gpuAt < 10) { sensors["gpuLoad"] = *m_gpuLoad; sample.gpuLoad = m_gpuLoad; }
        if (m_gpuTemperature && QDateTime::currentSecsSinceEpoch() - m_gpuAt < 10) { sensors["gpu"] = *m_gpuTemperature; sample.gpuTemperature = m_gpuTemperature; }
#ifndef Q_OS_WIN
        if (!sensors.contains("gpu") || !sensors.contains("gpuLoad")) {
            const auto amd = GameMode::readAmdGpuSensors();
            for (auto i = amd.cbegin(); i != amd.cend(); ++i)
                if (!sensors.contains(i.key())) sensors[i.key()] = i.value();
        }
#endif
    }
    m_gameState = m_gameMode.update(sample, m_gameConfig, sensors);
    if (m_gameState.value("active").toBool() && (m_gpuLoad || m_gpuTemperature)) m_gameState["gpuProvider"] = m_gpuProvider;
#ifdef Q_OS_WIN
    // Bosun observes the daemon's actual capture, never launches a second ETW session.
    const auto runtimePath = m_file + QStringLiteral(".runtime.json");
    if (!m_draftMode) {
        QDir().mkpath(QFileInfo(runtimePath).absolutePath());
        QSaveFile runtime(runtimePath);
        const auto data = QJsonDocument(QJsonObject::fromVariantMap({{"at", QDateTime::currentMSecsSinceEpoch()},
            {"capture", m_captureStatus}, {"game", m_gameState}})).toJson(QJsonDocument::Compact);
        if (runtime.open(QIODevice::WriteOnly) && runtime.write(data) == data.size()) runtime.commit();
    } else {
        QFile runtime(runtimePath);
        const auto data = runtime.open(QIODevice::ReadOnly) ? runtime.read(65536) : QByteArray();
        const auto state = QJsonDocument::fromJson(data).object().toVariantMap();
        const auto age = QDateTime::currentMSecsSinceEpoch() - state.value("at").toLongLong();
        if (!state.isEmpty() && age >= 0 && age < 10000) {
            m_captureStatus = state.value("capture").toMap();
            m_gameState = state.value("game").toMap();
        } else {
            m_captureStatus = {{"state", "offline"}, {"message", tr("Start the Galleon daemon to detect games automatically.")}};
            m_gameState.clear();
        }
    }
#endif
    weather();
    // Disabled cards do not wake the renderer: the default remains a minute clock.
    for (const auto &entry : m_layout) {
        const auto item = entry.toMap();
        if (!item.value("enabled").toBool()) m_values.remove(item.value("id").toString());
    }
    if (previousCaptureStatus != m_captureStatus || previousGameConfig != m_gameConfig || previousGameState != m_gameState || previousFpsConfig != m_fpsConfig || previousLocation != m_location || previousLayout != m_layout || previousValues != m_values) Q_EMIT changed();
}
}
