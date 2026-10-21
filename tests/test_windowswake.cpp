// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "FakeTransport.h"
#include <ScreensaverController.h>
#include <galleon/StreamDeck.h>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

class TestWindowsWake : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void wakeDoesNotWaitForAConsoleDecoderToTerminate()
    {
        const QTemporaryDir dir;
        QFile clip(dir.filePath(QStringLiteral("clip.mp4")));
        QVERIFY(clip.open(QIODevice::WriteOnly));
        QVERIFY(clip.write("fake decoder input") > 0);
        clip.close();
        auto transport = std::make_unique<FakeTransport>();
        const auto *observed = transport.get();
        galleon::StreamDeck deck(std::move(transport));
        QVERIFY(deck.open());
        galleond::ScreensaverController saver(&deck, nullptr);
        saver.setFfmpegProgram(QStringLiteral(GALLEON_TEST_DECODER));
        galleond::ScreensaverConfig config;
        config.hasGeometry = true;
        config.screenWidthMm = 90;
        config.screenHeightMm = 48;
        config.keyWidthMm = 18;
        config.keyHeightMm = 18;
        config.gapXMm = 4;
        config.gapYMm = 4;
        config.screenToKeysMm = 10;
        config.videos = QStringList{clip.fileName()};
        saver.setConfig(config, dir.path());
        saver.setIdleTimeoutMs(50);
        QTRY_VERIFY_WITH_TIMEOUT(saver.isActive(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!observed->writes.empty(), 5000);
        saver.setIdleTimeoutMs(60000);
        QElapsedTimer elapsed;
        elapsed.start();
        saver.notifyActivity();
        QTRY_VERIFY_WITH_TIMEOUT(!saver.isActive(), 1000);
        QVERIFY2(elapsed.elapsed() < 500, "Wake waited for FFmpeg termination");
        QTRY_VERIFY_WITH_TIMEOUT(saver.findChildren<QProcess *>().isEmpty(), 5000);
    }
};
QTEST_GUILESS_MAIN(TestWindowsWake)
#include "test_windowswake.moc"
