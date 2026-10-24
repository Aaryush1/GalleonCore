// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameCapture.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
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
        if (m_stopping) return;
        m_state = QStringLiteral("error");
        m_message = tr("PresentMon stopped (code %1). Check capture permissions (Performance Log Users), then sign out and back in. No driver needs disabling.").arg(code);
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
        // Ask PresentMon to close only our named ETW session before killing a stuck child.
        m_cleanup.start(m_program, {QStringLiteral("--session_name"),
            QStringLiteral("Galleon-%1").arg(QCoreApplication::applicationPid()), QStringLiteral("--terminate_existing_session")});
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
    qsizetype at = 0, next = 0;
    while ((next = m_partial.indexOf('\n', at)) >= 0) {
        const auto row = m_partial.mid(at, next - at + 1);
        if (row.startsWith("Application,") && row.toLower().contains("msbetweenpresents")) m_header = row;
        else if (!m_header.isEmpty() && row.size() < 16384) m_tail += row;
        at = next + 1;
    }
    m_partial.remove(0, at);
    if (m_partial.size() > 16384) m_partial.clear();
    if (m_tail.size() > 65536) { m_tail = m_tail.right(65536); m_tail.remove(0, m_tail.indexOf('\n') + 1); }
}
void GameCapture::tick(bool enabled, const CaptureTarget &target, const QString &program) {
    if (!enabled || !target.pid || program.isEmpty()) {
        stop(); m_target = {}; m_retryAt = 0;
        m_program = program;
        m_state = !enabled ? QStringLiteral("disabled") : program.isEmpty() ? QStringLiteral("missing") : QStringLiteral("waiting");
        m_message = !enabled ? tr("Automatic capture disabled") : program.isEmpty()
            ? tr("Install the official PresentMon MSI with its console component, then restart the daemon.")
            : tr("Ready. Start a Steam game; capture starts automatically.");
        return;
    }
    if (target != m_target || program != m_program) {
        stop(); m_target = target; m_program = program; m_retryAt = 0;
    }
    if (m_stopping && (m_process.state() != QProcess::NotRunning || m_cleanup.state() != QProcess::NotRunning)) return;
    if (m_process.state() != QProcess::NotRunning || QDateTime::currentMSecsSinceEpoch() < m_retryAt) return;
    m_stopping = false; m_header.clear(); m_tail.clear(); m_partial.clear(); m_diagnostics.clear();
    QDir().mkpath(QFileInfo(m_output).absolutePath());
    QFile::remove(m_output);
    m_state = QStringLiteral("starting"); m_message = tr("Starting capture for %1").arg(target.name);
    m_startedAt = QDateTime::currentMSecsSinceEpoch();
    m_ownsOutput = true;
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
    return {{"state", m_state}, {"message", m_message}, {"program", m_program},
            {"process", m_target.name}, {"detail", QString::fromLocal8Bit(m_diagnostics).left(4096)}};
}
QString GameCapture::findPresentMon(const QString &overridePath, const QStringList &roots) {
    if (!overridePath.isEmpty()) return QFileInfo(overridePath).isAbsolute() && QFileInfo(overridePath).isFile() ? overridePath : QString();
    auto folders = roots;
    if (folders.isEmpty()) {
        folders << QCoreApplication::applicationDirPath() + QStringLiteral("/PresentMon");
        for (const auto &variable : {"ProgramW6432", "ProgramFiles"}) {
            const auto base = qEnvironmentVariable(variable);
            if (!base.isEmpty()) folders << base + QStringLiteral("/Intel/PresentMon/PresentMonConsoleApplication");
        }
    }
    for (const auto &folder : folders) {
        const auto files = QDir(folder).entryInfoList({QStringLiteral("PresentMon-*-x64.exe"), QStringLiteral("PresentMonConsole.exe")}, QDir::Files, QDir::Name | QDir::Reversed);
        if (!files.isEmpty()) return files.first().absoluteFilePath();
    }
    // Do not accidentally launch the MSI's GUI, also named PresentMon.exe.
    return roots.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("PresentMonConsole.exe")) : QString();
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
