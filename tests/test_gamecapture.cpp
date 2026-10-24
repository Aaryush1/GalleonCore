// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include <GameCapture.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QSettings>
#include <QScopeGuard>
using namespace galleond;
class TestGameCapture : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void msiConsoleDiscoveryNeverPicksTheGui() {
        QTemporaryDir dir;
        for (const auto &name : {"PresentMon.exe", "PresentMon-2.6.0-x64.exe"}) {
            QFile file(dir.filePath(name)); QVERIFY(file.open(QIODevice::WriteOnly));
        }
        QCOMPARE(GameCapture::findPresentMon({}, {dir.path()}), dir.filePath("PresentMon-2.6.0-x64.exe"));
        QFile::remove(dir.filePath("PresentMon-2.6.0-x64.exe"));
        QVERIFY(GameCapture::findPresentMon({}, {dir.path()}).isEmpty());
        QVERIFY(GameCapture::findPresentMon(dir.filePath("missing.exe")).isEmpty());
    }
    void invalidPathsFallThroughAndInstallationRootsAreChecked() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("custom install/PresentMonConsoleApplication"));
        const auto path = dir.filePath("custom install/PresentMonConsoleApplication/PresentMon-2.6.0-x64.exe");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        QCOMPARE(GameCapture::findPresentMon(dir.filePath("missing.exe"), {dir.filePath("stale"), dir.filePath("custom install")}), path);
        QFile::remove(path);
        QVERIFY(GameCapture::findPresentMon({}, {dir.filePath("custom install")}).isEmpty());
    }
    void gamesAreSelectedWithoutTargetingDesktopOrLaunchers() {
        QVERIFY(GameCapture::isGamePath("D:\\SteamLibrary\\steamapps\\common\\Counter-Strike Global Offensive\\game\\bin\\win64\\cs2.exe"));
        QVERIFY(!GameCapture::isGamePath("C:/Windows/explorer.exe"));
        QVERIFY(!GameCapture::isGamePath("D:/steamapps/common/game/crashhandler.exe"));
        QVERIFY(!GameCapture::isGamePath("D:/steamapps/common/Steamworks Shared/redist.exe"));
        QVERIFY(GameCapture::isGamePath("D:/Epic/MyGame/game.exe", {"Game.exe"}));
        QVERIFY(!GameCapture::isGamePath("D:/Epic/MyGame/game.exe"));
    }
    void captureLifecycleAndBoundedLiveCsv() {
        QTemporaryDir dir;
        const auto path = dir.filePath("capture.csv");
        GameCapture capture(path);
        const auto program = QStringLiteral(GALLEON_TEST_CAPTURE);
        capture.tick(true, {}, program);
        QCOMPARE(capture.status().value("state").toString(), QStringLiteral("waiting"));
        QVERIFY(!capture.status().value("running").toBool());
        QVERIFY(!capture.status().value("canStart").toBool());
        capture.tick(true, {12, "fixture.exe"}, program);
        QTRY_VERIFY_WITH_TIMEOUT(capture.sample().fps.has_value(), 5000);
        QCOMPARE(capture.sample().fps.value_or(0), 100.0);
        QCOMPARE(capture.status().value("state").toString(), QStringLiteral("capturing"));
        QVERIFY(capture.status().value("running").toBool());
        QVERIFY(!capture.status().value("canStart").toBool());
        // Polling the same game must preserve the child rather than restarting it.
        const auto token = capture.sample().token;
        capture.tick(true, {12, "fixture.exe"}, program);
        QTRY_VERIFY(capture.sample().token != token);
        capture.tick(false, {}, program);
        QVERIFY(!capture.sample().fps);
        QVERIFY(!QFile::exists(path));
        QCOMPARE(capture.status().value("state").toString(), QStringLiteral("disabled"));
    }
    void highFrameRateCaptureRetainsBoundedCache() {
        QTemporaryDir dir;
        const auto path = dir.filePath("capture.csv");
        GameCapture capture(path);
        capture.tick(true, {13, "fixture.exe"}, QStringLiteral(GALLEON_TEST_CAPTURE));
        QTRY_VERIFY(capture.sample().fps.has_value());
        QVERIFY(QFileInfo(path).size() < 70000);
        QCOMPARE(capture.sample().fps.value_or(0), 100.0);
    }
#ifdef Q_OS_WIN
    void officialRegistryKeyThenPathThenDefaultFolder() {
        if (qEnvironmentVariable("GITHUB_ACTIONS") != QStringLiteral("true"))
            QSKIP("Registry fixture is isolated to the disposable Windows CI runner");
        QTemporaryDir dir;
        const auto oldPath = qgetenv("PATH"), oldProgramFiles = qgetenv("ProgramW6432"), oldFiles = qgetenv("ProgramFiles");
        QSettings registry(QStringLiteral("HKEY_CURRENT_USER\\Software\\Intel\\PresentMon"), QSettings::Registry64Format);
        const auto key = QStringLiteral("PresentMonInstallFolder");
        const bool existed = registry.contains(key); const auto previous = registry.value(key);
        const auto restore = qScopeGuard([&] {
            if (existed) registry.setValue(key, previous); else registry.remove(key);
            registry.sync();
            qputenv("PATH", oldPath); qputenv("ProgramW6432", oldProgramFiles); qputenv("ProgramFiles", oldFiles);
        });
        const auto create = [&](const QString &folder) {
            QDir().mkpath(folder);
            const auto path = folder + "/PresentMon-2.6.0-x64.exe";
            QFile file(path); if (!file.open(QIODevice::WriteOnly)) return QString();
            return path;
        };
        const auto installed = dir.filePath("custom/PresentMonConsoleApplication");
        const auto registered = create(installed);
        const auto fromPath = create(dir.filePath("on-path"));
        const auto fromDefault = create(dir.filePath("programs/Intel/PresentMon/PresentMonConsoleApplication"));
        QVERIFY(!registered.isEmpty() && !fromPath.isEmpty() && !fromDefault.isEmpty());
        registry.setValue(key, dir.filePath("custom")); registry.sync();
        qputenv("PATH", dir.filePath("on-path").toUtf8());
        qputenv("ProgramW6432", dir.filePath("programs").toUtf8());
        qputenv("ProgramFiles", dir.filePath("programs").toUtf8());
        QCOMPARE(GameCapture::findPresentMon(), registered);
        QVERIFY(QFile::remove(registered));
        QCOMPARE(GameCapture::findPresentMon(), fromPath);
        QVERIFY(QFile::remove(fromPath));
        QCOMPARE(GameCapture::findPresentMon(), fromDefault);
    }
    void nativeDetectionFindsAProcessInASteamLibrary() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("steamapps/common/TestGame"));
        const auto executable = dir.filePath("steamapps/common/TestGame/fixture.exe");
        QVERIFY(QFile::copy(QStringLiteral(GALLEON_TEST_CAPTURE), executable));
        QProcess process;
        process.start(executable, {"--process_id", "12"});
        QVERIFY(process.waitForStarted());
        const auto pid = quint32(process.processId());
        QTRY_COMPARE(GameCapture::detectGame({}, pid).pid, pid);
        process.kill(); QVERIFY(process.waitForFinished());
        QVERIFY(GameCapture::detectGame({}, pid).pid != pid);
    }
#endif
    void unusedObserverDoesNotDeleteTheDaemonsCapture() {
        QTemporaryDir dir;
        const auto path = dir.filePath("capture.csv");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("owned by daemon"); file.close();
        { GameCapture observer(path); }
        QVERIFY(QFile::exists(path));
    }
    void errorsAreVisibleAndDoNotRestartInALoop() {
        QTemporaryDir dir;
        GameCapture capture(dir.filePath("capture.csv"));
        const auto program = QStringLiteral(GALLEON_TEST_CAPTURE);
        capture.tick(true, {99, "fixture.exe"}, program);
        QTRY_COMPARE(capture.status().value("state").toString(), QStringLiteral("error"));
        QVERIFY(capture.status().value("message").toString().contains("permissions"));
        capture.tick(true, {99, "fixture.exe"}, program);
        QCOMPARE(capture.status().value("state").toString(), QStringLiteral("error"));
        QVERIFY(!capture.sample().fps);
        QVERIFY(capture.status().value("canStart").toBool());
        capture.retry();
        capture.tick(true, {99, "fixture.exe"}, program);
        QCOMPARE(capture.status().value("state").toString(), QStringLiteral("starting"));
    }
};
QTEST_GUILESS_MAIN(TestGameCapture)
#include "test_gamecapture.moc"
