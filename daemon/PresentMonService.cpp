// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PresentMonService.h"
#include "../third_party/PresentMonAPI.h"
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QThread>
#include <array>
#include <cmath>
#include <cstring>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace galleond {
QVariantMap presentMonInstallationFromImagePath(const QString &imagePath) {
    QString path = imagePath.trimmed();
    if (path.startsWith('"')) { const auto end = path.indexOf('"', 1); if (end < 0) return {}; path = path.mid(1, end - 1); }
    else { const auto end = path.indexOf(QStringLiteral(".exe"), 0, Qt::CaseInsensitive); if (end < 0) return {}; path = path.left(end + 4); }
    const QRegularExpression variables(QStringLiteral("%([^%]+)%"));
    auto matches = variables.globalMatch(path);
    QList<QPair<QString, QString>> expanded;
    while (matches.hasNext()) { const auto match = matches.next(); expanded.append({match.captured(0), qEnvironmentVariable(qPrintable(match.captured(1)))}); }
    for (const auto &pair : expanded) if (!pair.second.isEmpty()) path.replace(pair.first, pair.second);
    const QFileInfo service(path);
    if (!service.isAbsolute() || service.fileName().compare(QStringLiteral("PresentMonService.exe"), Qt::CaseInsensitive) != 0) return {};
    const auto api = service.dir().filePath(QStringLiteral("PresentMonAPI2.dll"));
    return {{"installed", service.isFile() && QFileInfo(api).isFile()}, {"servicePath", service.absoluteFilePath()}, {"apiPath", api}};
}
QVariantMap findPresentMonService() {
#ifdef Q_OS_WIN
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) return {{"installed", false}, {"error", QStringLiteral("PresentMon: service manager inaccessible (%1)").arg(GetLastError())}};
    const auto managerCleanup = qScopeGuard([&] { CloseServiceHandle(manager); });
    const auto service = OpenServiceW(manager, L"PresentMonSharedService", SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
    if (!service) return {{"installed", false}, {"error", GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST
        ? QStringLiteral("PresentMonSharedService absent. Installer le MSI PresentMon.")
        : QStringLiteral("PresentMonSharedService inaccessible (%1)").arg(GetLastError())}};
    const auto serviceCleanup = qScopeGuard([&] { CloseServiceHandle(service); });
    DWORD required = 0;
    QueryServiceConfigW(service, nullptr, 0, &required);
    if (!required || required > 65536) return {{"installed", false}, {"error", "PresentMon: invalid service configuration"}};
    QByteArray storage(int(required), '\0');
    auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(storage.data());
    if (!QueryServiceConfigW(service, config, required, &required)) return {{"installed", false}, {"error", "PresentMon: service configuration inaccessible"}};
    auto result = presentMonInstallationFromImagePath(QString::fromWCharArray(config->lpBinaryPathName));
    if (!result.value("installed").toBool()) result["error"] = QStringLiteral("PresentMonService.exe ou PresentMonAPI2.dll manquant. Réparer le MSI PresentMon.");
    SERVICE_STATUS_PROCESS status{}; DWORD bytes = 0;
    if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytes)) result["serviceRunning"] = status.dwCurrentState == SERVICE_RUNNING;
    else result["error"] = QStringLiteral("PresentMon: service status inaccessible (%1)").arg(GetLastError());
    return result;
#else
    return {{"installed", false}};
#endif
}
QString startPresentMonService() {
#ifdef Q_OS_WIN
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) return QStringLiteral("Service manager inaccessible (%1)").arg(GetLastError());
    const auto managerCleanup = qScopeGuard([&] { CloseServiceHandle(manager); });
    auto service = OpenServiceW(manager, L"PresentMonSharedService", SERVICE_QUERY_STATUS);
    if (!service) return QStringLiteral("PresentMonSharedService inaccessible (%1)").arg(GetLastError());
    SERVICE_STATUS_PROCESS status{}; DWORD bytes = 0;
    const bool running = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytes) && status.dwCurrentState == SERVICE_RUNNING;
    CloseServiceHandle(service);
    if (running) return {};
    service = OpenServiceW(manager, L"PresentMonSharedService", SERVICE_START | SERVICE_QUERY_STATUS);
    if (!service) return QStringLiteral("Service arrêté. Autorisation Windows nécessaire pour démarrer PresentMonSharedService (%1).").arg(GetLastError());
    const auto cleanup = qScopeGuard([&] { CloseServiceHandle(service); });
    if (!StartServiceW(service, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) return QStringLiteral("PresentMonSharedService ne démarre pas (%1)").arg(GetLastError());
    for (int i = 0; i < 50; ++i) {
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytes)) break;
        if (status.dwCurrentState == SERVICE_RUNNING) return {};
        if (status.dwCurrentState == SERVICE_STOPPED) break;
        QThread::msleep(100);
    }
    return QStringLiteral("PresentMonSharedService indisponible après la demande de démarrage.");
#else
    return QStringLiteral("PresentMon service is only supported on Windows");
#endif
}
struct PresentMonSession::Impl {
    QLibrary library;
    PM_SESSION_HANDLE session = nullptr;
    PM_DYNAMIC_QUERY_HANDLE query = nullptr;
    PM_QUERY_ELEMENT element{};
    quint32 pid = 0;
    bool tracking = false;
    QString problem;
    decltype(&pmCloseSession) close = nullptr;
    decltype(&pmStopTrackingProcess) stop = nullptr;
    decltype(&pmFreeDynamicQuery) freeQuery = nullptr;
    decltype(&pmPollDynamicQuery) pollQuery = nullptr;
    ~Impl() {
        if (query && freeQuery) freeQuery(query);
        if (session && tracking && stop) stop(session, pid);
        if (session && close) close(session);
    }
};
PresentMonSession::PresentMonSession() : m(std::make_unique<Impl>()) {}
PresentMonSession::~PresentMonSession() = default;
bool PresentMonSession::start(const QString &apiPath, quint32 pid) {
    m = std::make_unique<Impl>(); m->pid = pid;
    if (!QFileInfo(apiPath).isAbsolute() || !QFileInfo(apiPath).isFile() || !pid) { m->problem = "PresentMonAPI2.dll or game PID invalid"; return false; }
    m->library.setFileName(apiPath);
    if (!m->library.load()) { m->problem = QStringLiteral("PresentMonAPI2.dll inaccessible: %1").arg(m->library.errorString()); return false; }
#define PM_RESOLVE(name) reinterpret_cast<decltype(&name)>(m->library.resolve(#name))
    const auto version = PM_RESOLVE(pmGetApiVersion);
    const auto open = PM_RESOLVE(pmOpenSession);
    const auto track = PM_RESOLVE(pmStartTrackingProcess);
    const auto registerQuery = PM_RESOLVE(pmRegisterDynamicQuery);
    m->close = PM_RESOLVE(pmCloseSession); m->stop = PM_RESOLVE(pmStopTrackingProcess);
    m->freeQuery = PM_RESOLVE(pmFreeDynamicQuery); m->pollQuery = PM_RESOLVE(pmPollDynamicQuery);
#undef PM_RESOLVE
    if (!version || !open || !track || !registerQuery || !m->close || !m->stop || !m->freeQuery || !m->pollQuery) { m->problem = "PresentMon API: required entry point missing"; return false; }
    PM_VERSION api{};
    if (version(&api) != PM_STATUS_SUCCESS || api.major != PM_API_VERSION_MAJOR) { m->problem = "PresentMon API version incompatible. Update the MSI."; return false; }
    auto status = open(&m->session);
    if (status != PM_STATUS_SUCCESS) { m->problem = QStringLiteral("PresentMon service/API connection failed (%1)").arg(int(status)); return false; }
    status = track(m->session, pid);
    if (status != PM_STATUS_SUCCESS) { m->problem = QStringLiteral("PresentMon game tracking failed (%1)").arg(int(status)); return false; }
    m->tracking = true;
    m->element.metric = PM_METRIC_PRESENTED_FPS; m->element.stat = PM_STAT_AVG;
    status = registerQuery(m->session, &m->query, &m->element, 1, 1000.0, 0.0);
    if (status != PM_STATUS_SUCCESS || m->element.dataSize != sizeof(double) || m->element.dataOffset > 1016) {
        m->problem = QStringLiteral("PresentMon FPS query unavailable (%1)").arg(int(status)); return false;
    }
    return true;
}
std::optional<double> PresentMonSession::poll() {
    if (!m->query || !m->problem.isEmpty()) return {};
    alignas(double) std::array<uint8_t, 32768> buffer{};
    uint32_t swaps = 32;
    const auto status = m->pollQuery(m->query, m->pid, buffer.data(), &swaps);
    if (status != PM_STATUS_SUCCESS || swaps > 32) { m->problem = QStringLiteral("PresentMon FPS polling failed (%1)").arg(int(status)); return {}; }
    const auto stride = m->element.dataOffset + m->element.dataSize;
    double highest = 0;
    for (uint32_t i = 0; i < swaps; ++i) {
        double fps = 0;
        std::memcpy(&fps, buffer.data() + i * stride + m->element.dataOffset, sizeof(fps));
        if (std::isfinite(fps) && fps > highest && fps <= 10000) highest = fps;
    }
    return highest > 0 ? std::optional<double>(highest) : std::nullopt;
}
QString PresentMonSession::error() const { return m->problem; }
}
