// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameMode.h"
#include "SteamLibrary.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QUrl>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif
namespace galleond {
namespace {
QByteArray smallFile(const QString &path, qint64 limit = 65536) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.read(limit) : QByteArray();
}
QString validImage(const QString &path) {
    QFileInfo info(path);
    if (!info.isFile() || info.size() > 32 * 1024 * 1024) return {};
    QImageReader reader(path);
    const auto size = reader.size();
    if (size.width() <= 0 || size.height() <= 0 || qint64(size.width()) * size.height() > 32000000) return {};
    return QUrl::fromLocalFile(info.absoluteFilePath()).toString();
}
QString executableName(QString name) {
    name.replace('\\', '/');
    return name.section('/', -1).toLower();
}
QVariantMap identity(const QString &process) {
    const auto base = executableName(process);
    QString path;
    int appid = 0;
#ifdef Q_OS_WIN
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) do {
            if (QString::fromWCharArray(entry.szExeFile).compare(base, Qt::CaseInsensitive) != 0) continue;
            HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!handle) continue;
            wchar_t buffer[32768]; DWORD length = 32768;
            if (QueryFullProcessImageNameW(handle, 0, buffer, &length)) path = QString::fromWCharArray(buffer, int(length));
            CloseHandle(handle);
            if (!path.isEmpty()) break;
        } while (Process32NextW(snapshot, &entry));
        CloseHandle(snapshot);
    }
#else
    const auto processes = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    int inspected = 0;
    for (const auto &pid : processes) {
        if (++inspected > 8192) break;
        bool numeric = false; pid.toUInt(&numeric); if (!numeric) continue;
        const auto prefix = QStringLiteral("/proc/") + pid;
        const auto executable = QFileInfo(prefix + QStringLiteral("/exe")).symLinkTarget();
        if (executableName(executable) != base) continue;
        path = executable;
        for (const auto &variable : smallFile(prefix + QStringLiteral("/environ")).split('\0')) {
            if (!variable.startsWith("SteamAppId=")) continue;
            bool ok = false; const int id = variable.mid(11).toInt(&ok);
            if (ok && id > 0) appid = id;
        }
        break;
    }
#endif
    path.replace('\\', '/');
    // Windows uses the running executable's Steam library; also a Linux fallback.
    const auto marker = path.indexOf(QStringLiteral("/steamapps/common/"), 0, Qt::CaseInsensitive);
    QString root = SteamLibrary::defaultRoot();
    if (!appid && marker >= 0) {
        const auto steamapps = path.left(marker) + QStringLiteral("/steamapps");
        const auto folder = path.mid(marker + 18).section('/', 0, 0);
        const auto manifests = QDir(steamapps).entryList({QStringLiteral("appmanifest_*.acf")}, QDir::Files);
        for (int i = 0; i < qMin(500, int(manifests.size())); ++i) {
            const auto tree = SteamLibrary::parseVdf(smallFile(steamapps + '/' + manifests[i], SteamLibrary::MaxManifestBytes));
            const auto *state = tree ? tree->child(QStringLiteral("AppState")) : nullptr;
            if (!state || state->value(QStringLiteral("installdir")).compare(folder, Qt::CaseInsensitive) != 0) continue;
            bool ok = false; const int id = state->value(QStringLiteral("appid")).toInt(&ok);
            if (ok && id > 0) appid = id;
            break;
        }
    }
    QVariantMap result{{"title", process}, {"image", QString()}};
    if (appid > 0) {
        result["appid"] = appid;
        for (const auto &game : SteamLibrary::installedGames(root)) {
            if (game.appid == appid) { result["title"] = game.name; break; }
        }
        const auto folder = root + QStringLiteral("/appcache/librarycache/") + QString::number(appid);
        QString image;
        for (const auto &pattern : {QStringLiteral("*library_hero*.jpg"), QStringLiteral("*header*.jpg")}) {
            const auto entries = QDir(folder).entryList({pattern}, QDir::Files | QDir::NoSymLinks);
            if (!entries.isEmpty()) { image = validImage(folder + '/' + entries.first()); break; }
            image = validImage(folder + (pattern.contains("hero") ? QStringLiteral("_library_hero.jpg") : QStringLiteral("_header.jpg")));
            if (!image.isEmpty()) break;
        }
        if (image.isEmpty()) image = validImage(SteamLibrary::coverImage(appid, root));
        result["image"] = image;
    }
    return result;
}
void sensorNodes(const QJsonObject &node, QVariantMap &result, int depth, int &count) {
    if (depth > 16 || ++count > 4096) return;
    const auto id = node.value(QStringLiteral("SensorId")).toString();
    const bool isTemperature = id.contains(QStringLiteral("/temperature/"));
    const bool isLoad = id.endsWith(QStringLiteral("/load/0"));
    if (isTemperature || isLoad) {
        const auto text = node.value(QStringLiteral("Value")).toString().section(' ', 0, 0).replace(',', '.');
        bool ok = false; double value = text.toDouble(&ok);
        if (isTemperature && node.value(QStringLiteral("Value")).toString().contains(QStringLiteral("°F"))) value = (value - 32) * 5 / 9;
        auto key = id.startsWith("/intelcpu/") || id.startsWith("/amdcpu/") ? QStringLiteral("cpu")
            : id.startsWith("/gpu-") ? QStringLiteral("gpu") : QString();
        if (isLoad && !key.isEmpty()) key += QStringLiteral("Load");
        const bool valid = isTemperature ? value > 0 && value < 150 : value >= 0 && value <= 100;
        if (ok && std::isfinite(value) && valid && !key.isEmpty()) {
            if (!result.contains(key) || result[key].toDouble() < value) result[key] = value;
        }
    }
    for (const auto &child : node.value(QStringLiteral("Children")).toArray()) sensorNodes(child.toObject(), result, depth + 1, count);
}
}
QVariantMap GameMode::readAmdGpuSensors(const QString &root) {
    const QDir hwmon(root);
    const auto entries = hwmon.entryList({QStringLiteral("hwmon*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (int i = 0; i < qMin(64, int(entries.size())); ++i) {
        const QDir dir(hwmon.filePath(entries[i]));
        if (smallFile(dir.filePath(QStringLiteral("name")), 128).trimmed() != "amdgpu") continue;
        QVariantMap values;
        bool ok = false;
        const auto temperature = smallFile(dir.filePath(QStringLiteral("temp1_input")), 128).trimmed().toDouble(&ok) / 1000.0;
        if (ok && std::isfinite(temperature) && temperature > 0 && temperature < 150) values["gpu"] = temperature;
        const auto load = smallFile(dir.filePath(QStringLiteral("device/gpu_busy_percent")), 128).trimmed().toDouble(&ok);
        if (ok && std::isfinite(load) && load >= 0 && load <= 100) values["gpuLoad"] = load;
        return values;
    }
    return {};
}
QVariantMap GameMode::parseSensors(const QByteArray &json) {
    if (json.size() > 1024 * 1024) return {};
    QVariantMap result; int count = 0;
    sensorNodes(QJsonDocument::fromJson(json).object(), result, 0, count);
    return result;
}
QVariantMap GameMode::update(const FpsSample &sample, const QVariantMap &config, const QVariantMap &sensors) {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (sample.token != m_token) { m_token = sample.token; m_sampleAt = now; }
    if (!config.value("automatic", true).toBool() || !sample.fps || now - m_sampleAt > 10000) {
        m_process.clear(); m_identity.clear();
        return {{"active", false}};
    }
    if (m_process != sample.process) { m_process = sample.process; m_identity = identity(sample.process); }
    auto result = m_identity;
    result["active"] = true;
    result["process"] = sample.process;
    const auto profiles = config.value("profiles").toMap();
    const auto profile = profiles.value(executableName(sample.process)).toMap();
    if (!profile.value("title").toString().isEmpty()) result["title"] = profile.value("title");
    const auto image = validImage(profile.value("image").toString());
    if (!image.isEmpty()) result["image"] = image;
    result["fps"] = QLocale().toString(*sample.fps, 'f', 0);
    const auto temperature = [&](const QString &key, std::optional<double> value) {
        if (value) return QLocale().toString(*value, 'f', 0) + QStringLiteral(" °C");
        if (sensors.contains(key)) return QLocale().toString(sensors[key].toDouble(), 'f', 0) + QStringLiteral(" °C");
        return QString(QChar(0x2014));
    };
    result["cpu"] = temperature(QStringLiteral("cpu"), sample.cpuTemperature);
    result["gpu"] = temperature(QStringLiteral("gpu"), sample.gpuTemperature);
    const auto load = [&](const QString &key, std::optional<double> value) {
        bool ok = false;
        const double fallback = sensors.value(key).toDouble(&ok);
        if (value && std::isfinite(*value) && *value >= 0 && *value <= 100) result[key] = *value;
        else if (ok && std::isfinite(fallback) && fallback >= 0 && fallback <= 100) result[key] = fallback;
    };
    load(QStringLiteral("cpuLoad"), sample.cpuLoad);
    load(QStringLiteral("gpuLoad"), sample.gpuLoad);
    return result;
}
}
