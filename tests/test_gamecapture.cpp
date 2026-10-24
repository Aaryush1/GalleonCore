// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include <GameCapture.h>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
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
        capture.tick(true, {12, "fixture.exe"}, program);
        QTRY_VERIFY_WITH_TIMEOUT(capture.sample().fps.has_value(), 5000);
        QCOMPARE(capture.sample().fps.value_or(0), 100.0);
        QCOMPARE(capture.status().value("state").toString(), QStringLiteral("capturing"));
        // Polling the same game must preserve the child rather than restarting it.
        const auto token = capture.sample().token;
        capture.tick(true, {12, "fixture.exe"}, program);
        QTRY_VERIFY(capture.sample().token != token);
        capture.tick(false, {}, program);
        QVERIFY(!capture.sample().fps);
        QVERIFY(!QFile::exists(path));
        QCOMPARE(capture.status().value("state").toString(), QStringLiteral("disabled"));
    }
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
    }
};
QTEST_GUILESS_MAIN(TestGameCapture)
#include "test_gamecapture.moc"
