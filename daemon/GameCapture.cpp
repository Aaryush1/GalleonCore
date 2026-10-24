// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameCapture.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif
namespace galleond {
GameCapture::GameCapture(QString output, QObject *parent) : QObject(parent), m_output(std::move(output)) {
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &GameCapture::consume);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_diagnostics = (m_diagnostics + m_process.readAllStandardError()).right(4096);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_stopping) return;
        m_state = QStringLiteral("error"); m_message = tr("Cannot start PresentMon: %1").arg(m_process.errorString());
        m_retryAt = QDateTime::currentMSecsSinceEpoch() + 30000;
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
        consume();
        m_diagnostics = (m_diagnostics + m_process.readAllStandardError()).right(4096);
        if (m_stopping) return;
        m_state = QStringLiteral("error");
        m_message = m_apiPath.isEmpty()
            ? tr("PresentMon stopped (code %1). Check capture permissions (Performance Log Users), then sign out and back in. No driver needs disabling.").arg(code)
            : tr("PresentMon service/API inaccessible (code %1): %2").arg(code).arg(QString::fromLocal8Bit(m_diagnostics).left(400));
        m_retryAt = QDateTime::currentMSecsSinceEpoch() + 30000;
        QFile::remove(m_output);
    });
#ifdef Q_OS_WIN
    m_cleanup.setStandardOutputFile(QProcess::nullDevice());
    m_cleanup.setStandardErrorFile(QProcess::nullDevice());
    m_cleanup.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
}
GameCapture::~GameCapture() {
    stop();
    if (m_cleanup.state() != QProcess::NotRunning && !m_cleanup.waitForFinished(1000)) { m_cleanup.kill(); m_cleanup.waitForFinished(1000); }
    if (m_process.state() != QProcess::NotRunning) {
        if (!m_process.waitForFinished(1000)) { m_process.kill(); m_process.waitForFinished(1000); }
    }
    if (m_ownsOutput) QFile::remove(m_output);
}
void GameCapture::stop() {
    if (!m_stopping && m_process.state() != QProcess::NotRunning) {
#ifdef Q_OS_WIN
        if (!m_apiPath.isEmpty()) m_process.write("stop\n");
        else {
            // Legacy console captures own only their private ETW session.
            m_cleanup.start(m_program, {QStringLiteral("--session_name"),
                QStringLiteral("Galleon-%1").arg(QCoreApplication::applicationPid()), QStringLiteral("--terminate_existing_session")});
        }
#else
        m_process.terminate();
#endif
        QTimer::singleShot(2000, this, [this] { if (m_stopping) m_process.kill(); });
    }
    m_stopping = true;
    if (m_ownsOutput) QFile::remove(m_output);
    m_ownsOutput = false;
    m_header.clear(); m_tail.clear(); m_partial.clear();
}
void GameCapture::consume() {
    // Consume all pipe data, retaining only complete CSV rows in a bounded ring.
    m_partial += m_process.readAllStandardOutput();
    qsizetype at = 0;
    qsizetype next = 0;
    while ((next = m_partial.indexOf('\n', at)) >= 0) {
        const auto row = m_partial.mid(at, next - at + 1);
        if (row.startsWith("Application,") && (row.toLower().contains("msbetweenpresents") || row.toLower().contains(",fps,"))) m_header = row;
        else if (!m_header.isEmpty() && row.size() < 16384) m_tail += row;
        at = next + 1;
    }
    m_partial.remove(0, at);
    if (m_partial.size() > 16384) m_partial.clear();
    if (m_tail.size() > 65536) { m_tail = m_tail.right(65536); m_tail.remove(0, m_tail.indexOf('\n') + 1); }
}
void GameCapture::tick(bool enabled, const CaptureTarget &target, const QString &program, const QString &apiPath) {
    if (!enabled || !target.pid || program.isEmpty()) {
        stop(); m_target = {}; m_retryAt = 0;
        m_program = program;
        if (!enabled) {
            m_state = QStringLiteral("disabled"); m_message = tr("Automatic capture disabled");
        } else if (program.isEmpty()) {
            m_state = QStringLiteral("missing"); m_message = tr("Install the official PresentMon MSI with its console component, then restart the daemon.");
        } else {
            m_state = QStringLiteral("waiting"); m_message = tr("Ready. Start a Steam game; capture starts automatically.");
        }
        return;
    }
    if (target != m_target || program != m_program || apiPath != m_apiPath) {
        stop(); m_target = target; m_program = program; m_apiPath = apiPath; m_retryAt = 0;
    }
    if (m_stopping && (m_process.state() != QProcess::NotRunning || m_cleanup.state() != QProcess::NotRunning)) return;
    if (m_process.state() != QProcess::NotRunning || QDateTime::currentMSecsSinceEpoch() < m_retryAt) return;
    m_stopping = false; m_header.clear(); m_tail.clear(); m_partial.clear(); m_diagnostics.clear();
    QDir().mkpath(QFileInfo(m_output).absolutePath());
    QFile::remove(m_output);
    m_state = QStringLiteral("starting"); m_message = tr("Starting capture for %1").arg(target.name);
    m_startedAt = QDateTime::currentMSecsSinceEpoch();
    m_ownsOutput = true;
    if (!apiPath.isEmpty()) {
        m_process.start(program, {QStringLiteral("--api"), apiPath, QStringLiteral("--pid"), QString::number(target.pid), QStringLiteral("--name"), target.name});
        return;
    }
    m_process.start(program, {QStringLiteral("--process_id"), QString::number(target.pid),
        QStringLiteral("--v1_metrics"), QStringLiteral("--output_stdout"), QStringLiteral("--no_console_stats"),
        QStringLiteral("--terminate_on_proc_exit"), QStringLiteral("--stop_existing_session"), QStringLiteral("--session_name"),
        QStringLiteral("Galleon-%1").arg(QCoreApplication::applicationPid())});
}
FpsSample GameCapture::sample() {
    if (m_stopping || m_process.state() == QProcess::NotRunning) return {{}, m_message};
    consume();
    // Write only when new bytes arrive: a stalled capture must become stale.
    const auto bytes = m_header + m_tail;
    QFile existing(m_output);
    const bool same = existing.open(QIODevice::ReadOnly) && existing.read(81920) == bytes;
    existing.close();
    if (!m_header.isEmpty() && !same) {
        QSaveFile file(m_output);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            m_state = QStringLiteral("error"); m_message = tr("Cannot write the local capture cache");
            return {{}, m_message};
        }
    }
    auto result = readFpsLog(m_output, m_target.name);
    if (result.fps) { m_state = QStringLiteral("capturing"); m_message = tr("Capture active: %1").arg(m_target.name); }
    else if (QDateTime::currentMSecsSinceEpoch() - m_startedAt > 15000) {
        m_state = QStringLiteral("no_frames");
        m_message = tr("Game detected, but no fresh frames. Check PresentMon permissions and whether the game is rendering.");
    }
    return result;
}
QVariantMap GameCapture::status() const {
    const bool running = !m_stopping && m_process.state() == QProcess::Running;
    const bool starting = !m_stopping && m_process.state() == QProcess::Starting;
    return {{"running", running}, {"starting", starting},
            {"canStart", m_target.pid != 0 && !m_program.isEmpty() && !m_stopping && !running && !starting},
            {"state", m_state}, {"message", m_message}, {"program", m_program},
            {"process", m_target.name}, {"detail", QString::fromLocal8Bit(m_diagnostics).left(4096)}};
}
QStringList GameCapture::registeredPresentMonFolders() {
    QStringList result;
#ifdef Q_OS_WIN
    // The official MSI writes this per-user key; inspect both registry views
    // and machine installations as well. Registry reads never execute commands.
    for (const auto format : {QSettings::Registry64Format, QSettings::Registry32Format}) {
        for (const auto &hive : {QStringLiteral("HKEY_CURRENT_USER"), QStringLiteral("HKEY_LOCAL_MACHINE")}) {
            QSettings intel(hive + QStringLiteral("\\Software\\Intel\\PresentMon"), format);
            const auto folder = intel.value(QStringLiteral("PresentMonInstallFolder")).toString().trimmed();
            if (!folder.isEmpty()) result << folder;
            QSettings uninstall(hive + QStringLiteral("\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"), format);
            const auto groups = uninstall.childGroups();
            for (int i = 0; i < qMin(2048, int(groups.size())); ++i) {
                uninstall.beginGroup(groups[i]);
                const auto name = uninstall.value(QStringLiteral("DisplayName")).toString();
                if (name.contains(QStringLiteral("PresentMon"), Qt::CaseInsensitive)) {
                    const auto location = uninstall.value(QStringLiteral("InstallLocation")).toString().trimmed();
                    if (!location.isEmpty()) result << location;
                }
                uninstall.endGroup();
            }
        }
    }
#endif
    result.removeDuplicates();
    return result;
}
namespace {
QString consoleIn(QString folder) {
    folder = folder.trimmed();
    if (folder.startsWith('"') && folder.endsWith('"')) folder = folder.mid(1, folder.size() - 2);
    // Expand registry REG_EXPAND_SZ paths without interpreting command lines.
    const QRegularExpression variable(QStringLiteral("%([^%]+)%"));
    auto matches = variable.globalMatch(folder);
    QList<QPair<QString, QString>> replacements;
    while (matches.hasNext()) { const auto match = matches.next(); replacements.append({match.captured(0), qEnvironmentVariable(qPrintable(match.captured(1)))}); }
    for (const auto &entry : replacements) if (!entry.second.isEmpty()) folder.replace(entry.first, entry.second);
    if (!QFileInfo(folder).isAbsolute()) return {};
    const auto acceptable = [](const QString &path) {
        const QFileInfo file(path);
        return file.isFile() && (file.fileName().compare(QStringLiteral("PresentMonConsole.exe"), Qt::CaseInsensitive) == 0
            || QRegularExpression(QStringLiteral("^PresentMon-[0-9.]+-x64\\.exe$"), QRegularExpression::CaseInsensitiveOption).match(file.fileName()).hasMatch());
    };
    if (acceptable(folder)) return QFileInfo(folder).absoluteFilePath();
    for (const auto &directory : {folder, QDir(folder).filePath(QStringLiteral("PresentMonConsoleApplication"))}) {
        const auto files = QDir(directory).entryInfoList({QStringLiteral("PresentMon-*-x64.exe"), QStringLiteral("PresentMonConsole.exe")}, QDir::Files, QDir::Name | QDir::Reversed);
        for (const auto &file : files) if (acceptable(file.absoluteFilePath())) return file.absoluteFilePath();
    }
    return {};
}
}
QString GameCapture::findPresentMon(const QString &overridePath, const QStringList &roots) {
    // An invalid saved path does not mask a working MSI/PATH/default installation.
    if (!overridePath.isEmpty() && QFileInfo(overridePath).isAbsolute() && QFileInfo(overridePath).isFile()) return overridePath;
    if (!roots.isEmpty()) {
        for (const auto &root : roots) { const auto found = consoleIn(root); if (!found.isEmpty()) return found; }
        return {};
    }
    for (const auto &root : registeredPresentMonFolders()) { const auto found = consoleIn(root); if (!found.isEmpty()) return found; }
    for (const auto &root : qEnvironmentVariable("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
        const auto found = consoleIn(root); if (!found.isEmpty()) return found;
    }
    QStringList folders{QCoreApplication::applicationDirPath() + QStringLiteral("/PresentMon")};
    for (const auto &variable : {"ProgramW6432", "ProgramFiles"}) {
        const auto base = qEnvironmentVariable(variable);
        if (!base.isEmpty()) folders << base + QStringLiteral("/Intel/PresentMon/PresentMonConsoleApplication");
    }
    for (const auto &root : folders) { const auto found = consoleIn(root); if (!found.isEmpty()) return found; }
    return {};
}
bool GameCapture::isGamePath(QString path, const QStringList &profiles) {
    path.replace('\\', '/'); path = path.toLower();
    const auto name = path.section('/', -1);
    if (!name.endsWith(QStringLiteral(".exe"))) return false;
    if (profiles.contains(name, Qt::CaseInsensitive)) return true;
    for (const auto &part : {"launcher", "crash", "helper", "setup", "unins", "anticheat", "reporter", "redist"})
        if (name.contains(QLatin1String(part))) return false;
    return path.contains(QStringLiteral("/steamapps/common/")) && !path.contains(QStringLiteral("/steamworks shared/"));
}
CaptureTarget GameCapture::detectGame(const QStringList &profiles, quint32 currentPid) {
#ifdef Q_OS_WIN
    DWORD foreground = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
    CaptureTarget current, first;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return {};
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    int count = 0;
    if (Process32FirstW(snapshot, &entry)) do {
        if (++count > 8192) break;
        HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!handle) continue;
        wchar_t buffer[32768]; DWORD length = 32768;
        const bool ok = QueryFullProcessImageNameW(handle, 0, buffer, &length);
        CloseHandle(handle);
        if (!ok || !isGamePath(QString::fromWCharArray(buffer, int(length)), profiles)) continue;
        const CaptureTarget candidate{quint32(entry.th32ProcessID), QString::fromWCharArray(entry.szExeFile)};
        if (entry.th32ProcessID == foreground) { CloseHandle(snapshot); return candidate; }
        if (entry.th32ProcessID == currentPid) current = candidate;
        if (!first.pid) first = candidate;
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return current.pid ? current : first;
#else
    Q_UNUSED(profiles)
    Q_UNUSED(currentPid)
    return {};
#endif
}
}
