// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "RecordingController.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <utility>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace galleond {
namespace {
bool gameBarInstalled() {
#ifdef Q_OS_WIN
    const QSettings packages(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages"), QSettings::NativeFormat);
    for (const auto &name : packages.childGroups())
        if (name.startsWith(QStringLiteral("Microsoft.XboxGamingOverlay_"))) return true;
#endif
    return false;
}
bool obsInstalled() {
    if (!QStandardPaths::findExecutable(QStringLiteral("obs")).isEmpty()) return true;
#ifdef Q_OS_WIN
    for (const auto &root : {qEnvironmentVariable("ProgramFiles"), qEnvironmentVariable("ProgramFiles(x86)")})
        if (!root.isEmpty() && QFile::exists(root + QStringLiteral("/obs-studio/bin/64bit/obs64.exe"))) return true;
    if (!QStandardPaths::findExecutable(QStringLiteral("obs64.exe")).isEmpty()) return true;
    for (const auto &hive : {QStringLiteral("HKEY_CURRENT_USER"), QStringLiteral("HKEY_LOCAL_MACHINE")}) {
        for (const auto &view : {QSettings::Registry64Format, QSettings::Registry32Format}) {
            QSettings uninstall(hive + QStringLiteral("\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"), view);
            for (const auto &group : uninstall.childGroups()) {
                uninstall.beginGroup(group);
                const QString name = uninstall.value(QStringLiteral("DisplayName")).toString();
                const QString folder = uninstall.value(QStringLiteral("InstallLocation")).toString();
                uninstall.endGroup();
                if (name.startsWith(QStringLiteral("OBS Studio"), Qt::CaseInsensitive)
                    && !folder.isEmpty() && QFile::exists(QDir(folder).filePath(QStringLiteral("bin/64bit/obs64.exe")))) return true;
            }
        }
    }
    return false;
#else
    const QStringList flatpakRoots{QDir::homePath() + QStringLiteral("/.local/share/flatpak"), QStringLiteral("/var/lib/flatpak")};
    return std::ranges::any_of(flatpakRoots, [](const QString &root) {
        return QDir(root + QStringLiteral("/app/com.obsproject.Studio/current/active")).exists();
    });
#endif
}
bool shortcut(int index) {
#ifdef Q_OS_WIN
    // Microsoft's fixed Game Bar shortcuts. No recording state is inferred.
    const WORD key = index == 0 ? WORD('R') : index == 1 ? WORD('G') : index == 2 ? WORD(VK_SNAPSHOT) : WORD('G');
    const bool alt = index != 3;
    INPUT inputs[6]{};
    int count = 0;
    const auto append = [&](WORD vk, bool up) {
        auto &item = inputs[count++]; item.type = INPUT_KEYBOARD;
        item.ki.wVk = vk; item.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    };
    append(VK_LWIN, false); if (alt) append(VK_MENU, false);
    append(key, false); append(key, true);
    if (alt) append(VK_MENU, true);
    append(VK_LWIN, true);
    const UINT sent = SendInput(static_cast<UINT>(count), inputs, sizeof(INPUT));
    if (sent != static_cast<UINT>(count)) {
        // Release only the keys in this fixed chord after a partial send.
        INPUT releases[3]{};
        const WORD keys[3]{key, VK_MENU, VK_LWIN};
        for (int i = 0; i < 3; ++i) { releases[i].type = INPUT_KEYBOARD; releases[i].ki.wVk = keys[i]; releases[i].ki.dwFlags = KEYEVENTF_KEYUP; }
        SendInput(3, releases, sizeof(INPUT));
        return false;
    }
    return true;
#else
    Q_UNUSED(index)
    return false;
#endif
}
QByteArray hash64(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toBase64(); }
}
RecordingController::RecordingController(QObject *parent, QString preferences) : QObject(parent), m_preferences(std::move(preferences)), m_shortcut(shortcut) {
#ifdef Q_OS_WIN
    m_windows = true;
#endif
    if (m_preferences.isEmpty()) m_preferences = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/galleoncore/recording.ini");
    m_socket.setMaxAllowedIncomingMessageSize(65536);
    m_socket.setMaxAllowedIncomingFrameSize(65536);
    m_deadline.setSingleShot(true); m_deadline.setInterval(5000);
    connect(&m_deadline, &QTimer::timeout, this, [this] { m_socket.abort(); clearObs(); m_message = tr("OBS WebSocket response timed out"); Q_EMIT changed(); });
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &RecordingController::receive);
    connect(&m_socket, &QWebSocket::disconnected, this, [this] {
        if (static_cast<int>(m_socket.closeCode()) == 4009) m_message = tr("OBS WebSocket authentication failed: check the password");
        clearObs(); Q_EMIT changed();
    });
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(&m_socket, &QWebSocket::errorOccurred, this,
#else
    connect(&m_socket, qOverload<QAbstractSocket::SocketError>(&QWebSocket::error), this,
#endif
        [this](QAbstractSocket::SocketError) { clearObs(); m_message = tr("OBS unavailable: enable its WebSocket server and check the port/password"); Q_EMIT changed(); });
    connect(&m_timer, &QTimer::timeout, this, &RecordingController::refresh);
    m_timer.start(3000); refresh();
}
RecordingController::~RecordingController() {
    m_timer.stop(); m_deadline.stop();
    disconnect(&m_socket, nullptr, this, nullptr);
    m_socket.abort();
}
QString RecordingController::backend() const {
    if (m_choice != QStringLiteral("auto")) return m_choice;
    return m_windows ? QStringLiteral("gamebar") : QStringLiteral("obs");
}
QVariantList RecordingController::options() const {
    return {QVariantMap{{"id", "auto"}, {"label", tr("Automatique")}, {"available", true}},
        QVariantMap{{"id", "gamebar"}, {"label", "Game Bar"}, {"available", m_windows && m_gameBar}},
        QVariantMap{{"id", "obs"}, {"label", "OBS"}, {"available", m_obs || m_ready}}};
}
QString RecordingController::status() const {
    if (backend() == QStringLiteral("gamebar")) {
        if (!m_windows || !m_gameBar) return tr("Game Bar is unavailable on this computer");
        return m_barMessage.isEmpty() ? tr("Game Bar: shortcuts only; recording state is not confirmed") : m_barMessage;
    }
    if (!m_message.isEmpty()) return m_message;
    if (!m_ready) return tr("OBS: start OBS and enable Tools → WebSocket Server Settings");
    if (!m_recordKnown) return tr("OBS connected; waiting for recording status");
    if (!m_record) return tr("OBS: recording stopped");
    return m_paused ? tr("OBS: recording paused") : tr("OBS: recording active");
}
QVariantList RecordingController::buttons() const {
    QVariantList result;
    const bool bar = backend() == QStringLiteral("gamebar");
    const QStringList labels = bar ? QStringList{tr("Record / stop"), tr("Save last moments"), tr("Screenshot"), tr("Open Game Bar")}
        : QStringList{m_recordKnown && m_record ? tr("Stop recording") : tr("Start recording"),
            m_paused ? tr("Resume recording") : tr("Pause recording"), tr("Save replay"), m_replay ? tr("Stop replay buffer") : tr("Start replay buffer")};
    const QStringList commands{QStringLiteral("ToggleRecord"), QStringLiteral("ToggleRecordPause"), QStringLiteral("SaveReplayBuffer"), QStringLiteral("ToggleReplayBuffer")};
    QStringList icons;
    if (bar) icons = {QStringLiteral("record"), QStringLiteral("save-replay"), QStringLiteral("screenshot"), QStringLiteral("gamebar")};
    else icons = {m_recordKnown && m_record ? QStringLiteral("stop") : QStringLiteral("record"),
        m_paused ? QStringLiteral("resume") : QStringLiteral("pause"), QStringLiteral("save-replay"),
        m_replay ? QStringLiteral("replay-stop") : QStringLiteral("replay-start")};
    for (int i = 0; i < 12; ++i) {
        bool enabled = i < 4 && (bar ? m_windows && m_gameBar : m_ready && m_requests.contains(commands.value(i)));
        if (!bar && i == 0) enabled = enabled && m_recordKnown;
        if (!bar && i == 1) enabled = enabled && m_recordKnown && m_record;
        if (!bar && i == 2) enabled = enabled && m_replayKnown && m_replay;
        if (!bar && i == 3) enabled = enabled && m_replayKnown;
        if (!bar && !m_pending.isEmpty()) enabled = false;
        result.append(QVariantMap{{"icon", icons.value(i)}, {"index", i}, {"label", labels.value(i)}, {"enabled", enabled},
            {"active", !bar && ((i == 0 && m_recordKnown && m_record) || (i == 1 && m_recordKnown && m_paused) || (i == 3 && m_replayKnown && m_replay))}});
    }
    return result;
}
void RecordingController::load() {
    const QSettings settings(m_preferences, QSettings::IniFormat);
    QString choice = settings.value(QStringLiteral("backend"), QStringLiteral("auto")).toString();
    if (choice != QStringLiteral("auto") && choice != QStringLiteral("gamebar") && choice != QStringLiteral("obs")) choice = QStringLiteral("auto");
    const int savedPort = settings.value(QStringLiteral("obsPort"), 4455).toInt();
    const int port = savedPort > 0 && savedPort <= 65535 ? savedPort : 4455;
    const QString password = settings.value(QStringLiteral("obsPassword")).toString();
    if (choice == m_choice && port == m_port && password == m_password) return;
    m_choice = choice; m_port = port; m_password = password;
    m_socket.abort(); clearObs(); m_message.clear(); m_barMessage.clear();
}
bool RecordingController::configure(const QString &choice, int port, const QString &password, bool replacePassword) {
    if ((choice != QStringLiteral("auto") && choice != QStringLiteral("gamebar") && choice != QStringLiteral("obs")) || port < 1 || port > 65535 || password.size() > 1024) return false;
    // Keep an unavailable saved choice, but do not allow selecting a new unavailable backend.
    if (choice != m_choice && ((choice == QStringLiteral("gamebar") && !(m_windows && m_gameBar)) || (choice == QStringLiteral("obs") && !m_obs && !m_ready))) return false;
    QDir().mkpath(QFileInfo(m_preferences).absolutePath());
    QFile preferenceFile(m_preferences);
    if (!preferenceFile.exists() && !preferenceFile.open(QIODevice::WriteOnly, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        m_message = tr("Could not save recording preferences"); Q_EMIT changed(); return false;
    }
    preferenceFile.close();
    QSettings settings(m_preferences, QSettings::IniFormat);
    settings.setValue(QStringLiteral("backend"), choice); settings.setValue(QStringLiteral("obsPort"), port);
    if (replacePassword) settings.setValue(QStringLiteral("obsPassword"), password);
    settings.sync();
    QFile::setPermissions(m_preferences, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (settings.status() != QSettings::NoError) { m_message = tr("Could not save recording preferences"); Q_EMIT changed(); return false; }
    refresh(); return true;
}
void RecordingController::clearObs() {
    m_ready = false; m_recordKnown = false; m_replayKnown = false; m_record = false; m_paused = false; m_replay = false;
    m_requests.clear(); m_pending.clear(); m_deadline.stop();
}
void RecordingController::refresh() {
    load();
    if (!m_test) { m_gameBar = gameBarInstalled(); m_obs = obsInstalled(); }
    // A read-only local connection also discovers portable OBS installations while
    // Game Bar is selected. Only trigger() sends recording actions to the chosen backend.
    connectObs();
    Q_EMIT changed();
}
void RecordingController::connectObs() {
    if (m_socket.state() == QAbstractSocket::UnconnectedState) {
        m_message.clear(); m_socket.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(m_port))); m_deadline.start();
    } else if (m_ready && m_pending.isEmpty()) {
        if (m_requests.contains(QStringLiteral("GetRecordStatus"))) request(QStringLiteral("GetRecordStatus"));
        if (m_requests.contains(QStringLiteral("GetReplayBufferStatus"))) request(QStringLiteral("GetReplayBufferStatus"));
    }
}
void RecordingController::request(const QString &type) {
    if (m_pending.size() >= 8) return;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pending.insert(id, type);
    const QJsonObject data{{"requestType", type}, {"requestId", id}};
    m_socket.sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"op", 6}, {"d", data}}).toJson(QJsonDocument::Compact)));
    m_deadline.start();
}
void RecordingController::receive(const QString &message) {
    if (message.size() > 65536) { m_socket.abort(); return; }
    const auto root = QJsonDocument::fromJson(message.toUtf8()).object();
    const int op = root.value("op").toInt(-1); const auto data = root.value("d").toObject();
    if (op == 0 && !m_ready) {
        if (data.value("rpcVersion").toInt() < 1) { m_message = tr("Unsupported OBS WebSocket protocol"); m_socket.abort(); return; }
        QJsonObject identify{{"rpcVersion", 1}, {"eventSubscriptions", 64}};
        if (data.contains("authentication")) {
            const auto auth = data.value("authentication").toObject();
            const auto secret = hash64(m_password.toUtf8() + auth.value("salt").toString().toUtf8());
            identify.insert("authentication", QString::fromLatin1(hash64(secret + auth.value("challenge").toString().toUtf8())));
        }
        m_socket.sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"op", 1}, {"d", identify}}).toJson(QJsonDocument::Compact)));
    } else if (op == 2 && !m_ready) {
        m_ready = true; m_obs = true; m_message.clear(); m_deadline.stop(); request(QStringLiteral("GetVersion"));
    } else if (op == 7 && m_ready) {
        const QString id = data.value("requestId").toString();
        if (!m_pending.contains(id)) return;
        const QString type = m_pending.take(id);
        if (m_pending.isEmpty()) m_deadline.stop();
        const auto response = data.value("responseData").toObject();
        if (!data.value("requestStatus").toObject().value("result").toBool()) {
            m_message = tr("OBS refused %1 (code %2)").arg(type).arg(data.value("requestStatus").toObject().value("code").toInt());
            if (type == QStringLiteral("GetRecordStatus")) m_recordKnown = false;
            if (type == QStringLiteral("GetReplayBufferStatus")) m_replayKnown = false;
        } else if (type == QStringLiteral("GetVersion")) {
            for (const auto &value : response.value("availableRequests").toArray()) if (value.isString()) m_requests.insert(value.toString());
            if (m_requests.contains(QStringLiteral("GetRecordStatus"))) request(QStringLiteral("GetRecordStatus"));
            if (m_requests.contains(QStringLiteral("GetReplayBufferStatus"))) request(QStringLiteral("GetReplayBufferStatus"));
        } else if (type == QStringLiteral("GetRecordStatus")) {
            m_recordKnown = response.value("outputActive").isBool() && response.value("outputPaused").isBool();
            m_record = response.value("outputActive").toBool(); m_paused = response.value("outputPaused").toBool();
        } else if (type == QStringLiteral("GetReplayBufferStatus")) {
            m_replayKnown = response.value("outputActive").isBool(); m_replay = response.value("outputActive").toBool();
        } else {
            m_message.clear();
            if (m_requests.contains(QStringLiteral("GetRecordStatus"))) request(QStringLiteral("GetRecordStatus"));
            if (m_requests.contains(QStringLiteral("GetReplayBufferStatus"))) request(QStringLiteral("GetReplayBufferStatus"));
        }
    } else if (op == 5 && m_ready) {
        const QString event = data.value("eventType").toString();
        if (event == QStringLiteral("RecordStateChanged") || event == QStringLiteral("ReplayBufferStateChanged")) {
            // Poll authoritative full status (events do not always include paused).
            if (m_pending.isEmpty()) connectObs();
        }
    }
    Q_EMIT changed();
}
void RecordingController::trigger(int index) {
    if (index < 0 || index >= 4 || !buttons().at(index).toMap().value("enabled").toBool()) return;
    if (backend() == QStringLiteral("gamebar")) {
        m_barMessage = m_shortcut(index) ? tr("Game Bar shortcut sent; recording state is not confirmed") : tr("Could not send Game Bar shortcut");
    } else {
        const QStringList commands{QStringLiteral("ToggleRecord"), QStringLiteral("ToggleRecordPause"), QStringLiteral("SaveReplayBuffer"), QStringLiteral("ToggleReplayBuffer")};
        request(commands.at(index));
    }
    Q_EMIT changed();
}
void RecordingController::setPlatformForTest(bool windows, bool gameBar, bool obs, std::function<bool(int)> sender) {
    m_test = true; m_windows = windows; m_gameBar = gameBar; m_obs = obs; m_shortcut = std::move(sender); refresh();
}
}
