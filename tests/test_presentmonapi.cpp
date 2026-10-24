// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include <PresentMonService.h>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>
#include <QScopeGuard>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
using namespace galleond;
class TestPresentMonApi : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void cleanup() { qunsetenv("GALLEON_PM_TEST_MODE"); }
    void servicePathsAreParsedAndBothFilesAreRequired() {
        QTemporaryDir dir;
        const auto exe = dir.filePath("PresentMonService.exe");
        QFile service(exe); QVERIFY(service.open(QIODevice::WriteOnly)); service.close();
        auto data = presentMonInstallationFromImagePath('"' + exe + "\" --service");
        QVERIFY(!data.value("installed").toBool());
        QFile api(dir.filePath("PresentMonAPI2.dll")); QVERIFY(api.open(QIODevice::WriteOnly)); api.close();
        data = presentMonInstallationFromImagePath('"' + exe + "\" --service");
        QVERIFY(data.value("installed").toBool());
        QCOMPARE(data.value("servicePath").toString(), exe);
        QVERIFY(presentMonInstallationFromImagePath("relative/PresentMonService.exe").isEmpty());
        QVERIFY(presentMonInstallationFromImagePath(dir.filePath("PresentMon.exe")).isEmpty());
    }
#ifdef Q_OS_WIN
    void discoversSharedServiceAndValidatesItsActualDirectory() {
        if (qEnvironmentVariable("GITHUB_ACTIONS") != QStringLiteral("true"))
            QSKIP("Service fixture is restricted to the disposable Windows CI runner");
        const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
        QVERIFY(manager);
        const auto managerCleanup = qScopeGuard([&] { CloseServiceHandle(manager); });
        const auto existing = OpenServiceW(manager, L"PresentMonSharedService", SERVICE_QUERY_STATUS);
        if (existing) { CloseServiceHandle(existing); QSKIP("Never replace an existing PresentMon installation"); }
        QCOMPARE(GetLastError(), DWORD(ERROR_SERVICE_DOES_NOT_EXIST));
        QTemporaryDir dir;
        const auto executable = dir.filePath("PresentMonService.exe"), api = dir.filePath("PresentMonAPI2.dll");
        QFile serviceFile(executable); QVERIFY(serviceFile.open(QIODevice::WriteOnly)); serviceFile.close();
        QVERIFY(QFile::copy(QStringLiteral(GALLEON_TEST_PM_LIBRARY), api));
        const auto command = '"' + QDir::toNativeSeparators(executable) + "\" --fixture";
        const auto service = CreateServiceW(manager, L"PresentMonSharedService", L"Galleon CI fixture", DELETE | SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS,
            SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
            reinterpret_cast<LPCWSTR>(command.utf16()), nullptr, nullptr, nullptr, nullptr, nullptr);
        QVERIFY(service);
        const auto cleanup = qScopeGuard([&] { DeleteService(service); CloseServiceHandle(service); });
        auto installation = findPresentMonService();
        QVERIFY(installation.value("installed").toBool());
        QVERIFY(!installation.value("serviceRunning").toBool());
        QCOMPARE(installation.value("apiPath").toString(), api);
        QVERIFY(QFile::remove(api));
        installation = findPresentMonService();
        QVERIFY(!installation.value("installed").toBool());
        QVERIFY(!installation.value("error").toString().isEmpty());
    }
#endif
    void pollsFpsDirectlyFromTheApi() {
        PresentMonSession session;
        QVERIFY2(session.start(QStringLiteral(GALLEON_TEST_PM_LIBRARY), 42), qPrintable(session.error()));
        QCOMPARE(session.poll().value_or(0), 144.0);
        qputenv("GALLEON_PM_TEST_MODE", "nan");
        QCOMPARE(session.poll().value_or(0), 120.0);
        qputenv("GALLEON_PM_TEST_MODE", "poll-error");
        QVERIFY(!session.poll()); QVERIFY(session.error().contains("polling"));
    }
    void apiErrorsAndBufferBoundsAreExplicit() {
        for (const auto &mode : {"version", "offline", "layout"}) {
            qputenv("GALLEON_PM_TEST_MODE", mode);
            PresentMonSession session;
            QVERIFY(!session.start(QStringLiteral(GALLEON_TEST_PM_LIBRARY), 42));
            QVERIFY(!session.error().isEmpty());
        }
        qunsetenv("GALLEON_PM_TEST_MODE");
        PresentMonSession session;
        QVERIFY(session.start(QStringLiteral(GALLEON_TEST_PM_LIBRARY), 42));
        qputenv("GALLEON_PM_TEST_MODE", "overcount");
        QVERIFY(!session.poll()); QVERIFY(!session.error().isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestPresentMonApi)
#include "test_presentmonapi.moc"
