// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include <FpsReader.h>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
using galleond::readFpsLog;
class TestFpsReader : public QObject {
    Q_OBJECT
    static bool write(const QString &path, const QByteArray &data) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    }
private Q_SLOTS:
    void mangoHudLiveLogSkipsPartialRecordAndSummary() {
        QTemporaryDir dir;
        QVERIFY(write(dir.filePath("game.csv"), "os,cpu\nLinux,CPU\nfps,frametime,elapsed\n60,16.7,100\n144,6.94,200\n999,"));
        QVERIFY(write(dir.filePath("game_summary.csv"), "Average FPS\n999\n"));
        const auto sample = readFpsLog(dir.path());
        QVERIFY(sample.fps);
        QCOMPARE(*sample.fps, 144.0);
        QVERIFY(!readFpsLog(dir.path(), {}, QDateTime::currentDateTime().addSecs(11)).fps);
    }
    void presentMonFiltersProcessAndSwapchain() {
        QTemporaryDir dir;
        const auto path = dir.filePath("game.csv");
        QVERIFY(write(path, "Application,SwapChainAddress,MsBetweenPresents\n\"my,game.exe\",0x1,10\nother.exe,0x2,5\n\"my,game.exe\",0x9,100\n\"my,game.exe\",0x1,10\n"));
        QVERIFY(!readFpsLog(path).fps);
        const auto sample = readFpsLog(path, "MY,GAME.EXE");
        QVERIFY(sample.fps);
        QCOMPARE(*sample.fps, 100.0);
        QVERIFY(!readFpsLog(path, "not-running.exe").fps);
    }
    void rejectsInvalidAndUnsupportedData() {
        QTemporaryDir dir;
        const auto path = dir.filePath("game.csv");
        for (const auto &value : {QByteArray("nan"), QByteArray("inf"), QByteArray("-1"), QByteArray("10001")}) {
            QVERIFY(write(path, "fps,frametime\n" + value + ",10\n"));
            QVERIFY(!readFpsLog(path).fps);
        }
        QVERIFY(write(path, "fps,frametime\n0,0\n"));
        QCOMPARE(readFpsLog(path).fps.value_or(-1), 0.0);
        QVERIFY(write(path, "Application,MsGPUTime\ngame.exe,10\n"));
        QVERIFY(!readFpsLog(path).fps); // GPU busy time is not frame cadence.
        QVERIFY(!readFpsLog({}).fps);
        QVERIFY(!readFpsLog(dir.filePath("absent.csv")).fps);
    }
    void handlesGrowingLogsWithoutReadingWholeFile() {
        QTemporaryDir dir;
        const auto path = dir.filePath("game.csv");
        QByteArray data("fps,frametime,elapsed\n");
        for (int i = 0; i < 12000; ++i) data += "60,16.7," + QByteArray::number(i) + "\n";
        data += "165,6.06,12000\n";
        QVERIFY(write(path, data));
        QCOMPARE(readFpsLog(path).fps.value_or(-1), 165.0);
    }
};
QTEST_GUILESS_MAIN(TestFpsReader)
#include "test_fpsreader.moc"
