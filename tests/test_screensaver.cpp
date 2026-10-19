// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FakeTransport.h"

#include <PagesConfig.h>
#include <ScreensaverController.h>
#include <ScreensaverSplitter.h>
#include <galleon/StreamDeck.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace galleond;
using galleon::StreamDeck;

namespace {

// Test geometry (millimetres, invented for the test): a 90 x 48 screen, keys of
// 18 x 18 with gaps of 4, 10 between the screen and the keys.
ScreensaverConfig testConfig(const QString &video = QStringLiteral("clip.mp4"))
{
    ScreensaverConfig config;
    config.video = video;
    config.idleMinutes = 1;
    config.fps = 10;
    config.hasGeometry = true;
    config.screenWidthMm = 90;
    config.screenHeightMm = 48;
    config.keyWidthMm = 18;
    config.keyHeightMm = 18;
    config.gapXMm = 4;
    config.gapYMm = 4;
    config.screenToKeysMm = 10;
    return config;
}

// A frame in which the screen is red, key N has its own colour, and the rest is black.
QImage paintedFrame(const ScreensaverSplitter &splitter)
{
    const QSize size = splitter.canvasSize();
    QImage image(size, QImage::Format_RGB888);
    image.fill(Qt::black);
    const double px = size.width() / 90.0;
    QPainter painter(&image);
    painter.fillRect(QRectF(0, 0, 90 * px, 48 * px), QColor(220, 20, 20));
    const double gridLeft = (90 - 62) / 2.0;
    for (int key = 0; key < 12; ++key) {
        const int column = key % 3;
        const int row = key / 3;
        painter.fillRect(QRectF((gridLeft + column * 22) * px, (58 + row * 22) * px, 18 * px, 18 * px),
                         QColor(key * 20, 100, 200));
    }
    return image;
}

bool near(QRgb a, QRgb b, int tolerance = 12)
{
    return qAbs(qRed(a) - qRed(b)) <= tolerance && qAbs(qGreen(a) - qGreen(b)) <= tolerance
           && qAbs(qBlue(a) - qBlue(b)) <= tolerance;
}

class FakeMedia : public MediaController
{
public:
    [[nodiscard]] State state() const override { return m_state; }
    [[nodiscard]] NowPlaying nowPlaying() const override { return {}; }
    void playPause() override {}
    void next() override {}
    void previous() override {}
    void stop() override {}
    void changeVolume(int) override {}
    void seek(int) override {}
    void setState(State state)
    {
        m_state = state;
        Q_EMIT stateChanged();
    }

private:
    State m_state = State::Stopped;
};

struct Fixture
{
    QTemporaryDir dir;
    FakeTransport *transport = nullptr;
    std::unique_ptr<StreamDeck> deck;
    FakeMedia media;
    std::unique_ptr<ScreensaverController> controller;
    ScreensaverConfig config = testConfig();

    Fixture()
    {
        auto fake = std::make_unique<FakeTransport>();
        transport = fake.get();
        deck = std::make_unique<StreamDeck>(std::move(fake));
        QVERIFY(deck->open());
        QFile video(dir.filePath("clip.mp4"));
        QVERIFY(video.open(QIODevice::WriteOnly));
        video.write("not really a video: the fake FFmpeg does not read it");
        video.close();

        // A stand-in for FFmpeg: it writes the same raw frame over and over.
        const ScreensaverSplitter splitter(config);
        const QImage frame = paintedFrame(splitter);
        QFile raw(dir.filePath("frame.raw"));
        QVERIFY(raw.open(QIODevice::WriteOnly));
        for (int y = 0; y < frame.height(); ++y)
            raw.write(reinterpret_cast<const char *>(frame.constScanLine(y)), frame.width() * 3);
        raw.close();
        QFile script(dir.filePath("fake-ffmpeg"));
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\nfor a in \"$@\"; do echo \"$a\" >> '" + dir.filePath("args.log").toUtf8()
                     + "'; done\nwhile :; do cat '" + dir.filePath("frame.raw").toUtf8() + "'; sleep 0.05; done\n");
        script.setPermissions(script.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
        script.close();

        controller = std::make_unique<ScreensaverController>(deck.get(), &media);
        controller->setFfmpegProgram(dir.filePath("fake-ffmpeg"));
    }

    void configure(const ScreensaverConfig &c) { controller->setConfig(c, dir.path()); }

    [[nodiscard]] int screenReports() const
    {
        int count = 0;
        for (const auto &r : transport->writes)
            count += r.size() > 3 && r[0] == 0x02 && r[1] == 0x0c ? 1 : 0;
        return count;
    }
    [[nodiscard]] QSet<int> keysDrawn() const
    {
        QSet<int> keys;
        for (const auto &r : transport->writes) {
            if (r.size() > 3 && r[0] == 0x02 && r[1] == 0x07)
                keys.insert(r[2]);
        }
        return keys;
    }
};

} // namespace

class TestScreensaver : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theCanvasIsAsWideAsTheScreenAndAsHighAsTheModule()
    {
        const ScreensaverSplitter splitter(testConfig());
        QVERIFY(splitter.isValid());
        QCOMPARE(splitter.canvasSize().width(), 720);
        QCOMPARE(splitter.canvasSize().height(), 1136); // 720 * 142 / 90
    }

    void withoutGeometryTheSplitterIsNotValid()
    {
        ScreensaverConfig config = testConfig();
        config.hasGeometry = false;
        QVERIFY(!ScreensaverSplitter(config).isValid());
        QVERIFY(!config.enabled());
    }

    void eachPartIsTheRegionBehindIt()
    {
        const ScreensaverSplitter splitter(testConfig());
        const auto parts = splitter.split(paintedFrame(splitter));
        QVERIFY(parts.isValid());
        QCOMPARE(parts.screen.size(), QSize(720, 384));
        QVERIFY(near(parts.screen.pixel(360, 190), qRgb(220, 20, 20)));
        QVERIFY(near(parts.screen.pixel(2, 2), qRgb(220, 20, 20)));
        for (int key = 0; key < 12; ++key) {
            QCOMPARE(parts.keys[static_cast<std::size_t>(key)].size(), QSize(160, 160));
            // The whole key is its colour: the gaps between keys are left out.
            const QRgb expected = qRgb(key * 20, 100, 200);
            QVERIFY2(near(parts.keys[static_cast<std::size_t>(key)].pixel(3, 3), expected), qPrintable(QString::number(key)));
            QVERIFY2(near(parts.keys[static_cast<std::size_t>(key)].pixel(80, 80), expected), qPrintable(QString::number(key)));
            QVERIFY2(near(parts.keys[static_cast<std::size_t>(key)].pixel(156, 156), expected), qPrintable(QString::number(key)));
        }
    }

    void aFrameOfAnotherSizeButTheSameProportionsIsCut()
    {
        const ScreensaverSplitter splitter(testConfig());
        const QImage big = paintedFrame(splitter).scaled(1440, 2272);
        QVERIFY(splitter.split(big).isValid());
        QVERIFY(!splitter.split(QImage(720, 700, QImage::Format_RGB888)).isValid()); // wrong proportions
        QVERIFY(!splitter.split(QImage()).isValid());
    }

    void theConfigIsParsedAndChecked()
    {
        const auto parse = [](const QJsonObject &saver) {
            QJsonObject root = QJsonDocument::fromJson(defaultPagesJson()).object();
            root.insert("screensaver", saver);
            return parsePagesConfig(QJsonDocument(root).toJson());
        };
        const QJsonObject geometry{{"screen_mm", QJsonArray{90, 48}}, {"key_mm", QJsonArray{18, 18}},
                                   {"gap_mm", QJsonArray{4, 4}}, {"screen_to_keys_mm", 10}};
        const auto good = parse({{"video", "clip.mp4"}, {"idle_minutes", 3}, {"fps", 12}, {"geometry", geometry}});
        QVERIFY2(good.config.has_value(), qPrintable(good.errors.join('\n')));
        QVERIFY(good.config->screensaver.enabled());
        QCOMPARE(good.config->screensaver.idleMinutes, 3);
        QCOMPARE(good.config->screensaver.fps, 12);

        // A video without geometry parses, but the screensaver is not enabled.
        const auto noGeometry = parse({{"video", "clip.mp4"}});
        QVERIFY(noGeometry.config.has_value());
        QVERIFY(!noGeometry.config->screensaver.enabled());

        QVERIFY(!parse({{"video", "clip.mp4"}, {"fps", 60}}).config.has_value());
        QVERIFY(!parse({{"video", "clip.mp4"}, {"idle_minutes", 0}}).config.has_value());
        QVERIFY(!parse({{"video", 3}}).config.has_value());
        QVERIFY(!parse({{"video", QString(500, 'x')}}).config.has_value());
        QVERIFY(!parse({{"colour", "red"}}).config.has_value());
        QVERIFY(!parse({{"geometry", QJsonObject{{"screen_mm", QJsonArray{90}}}}}).config.has_value());
        QVERIFY(!parse({{"geometry", QJsonObject{{"screen_mm", QJsonArray{0, 48}}, {"key_mm", QJsonArray{18, 18}},
                                                 {"gap_mm", QJsonArray{4, 4}}, {"screen_to_keys_mm", 10}}}}).config.has_value());
    }

    void afterTheIdleTimeTheVideoIsShownOnTheScreenAndTheKeys()
    {
        Fixture f;
        QSignalSpy active(f.controller.get(), &ScreensaverController::activeChanged);
        f.configure(f.config);
        f.controller->setIdleTimeoutMs(100);
        QVERIFY(!f.controller->isActive());
        QTRY_VERIFY_WITH_TIMEOUT(f.controller->isActive(), 3000);
        QCOMPARE(active.count(), 1);
        QVERIFY(active.first().first().toBool());
        QTRY_VERIFY_WITH_TIMEOUT(f.screenReports() > 0 && f.keysDrawn().size() == 12, 5000);

        // FFmpeg was asked for the canvas size, at the configured rate, from the file only.
        QFile log(f.dir.filePath("args.log"));
        QVERIFY(log.open(QIODevice::ReadOnly));
        const QString args = QString::fromUtf8(log.readAll());
        QVERIFY(args.contains("scale=720:1136"));
        QVERIFY(args.contains("fps=10"));
        QVERIFY(args.contains("-protocol_whitelist\nfile"));
        QVERIFY(args.contains("-stream_loop\n-1"));
        QVERIFY(args.contains(f.dir.filePath("clip.mp4")));
    }

    void aKeyPressWakesTheModule()
    {
        Fixture f;
        QSignalSpy active(f.controller.get(), &ScreensaverController::activeChanged);
        f.configure(f.config);
        f.controller->setIdleTimeoutMs(100);
        QTRY_VERIFY_WITH_TIMEOUT(f.controller->isActive(), 3000);

        f.transport->pendingReads.push_back([] {
            std::vector<std::uint8_t> report = {0x01, 0x00, 0x0c, 0x00};
            report.resize(4 + galleon::protocol::KeyCount, 0);
            report[4] = 1;
            return report;
        }());
        QTRY_VERIFY_WITH_TIMEOUT(!f.controller->isActive(), 3000);
        QCOMPARE(active.count(), 2);
        QVERIFY(!active.last().first().toBool());
    }

    void aTrackPlayingKeepsTheScreensaverAway()
    {
        Fixture f;
        f.media.setState(MediaController::State::Playing);
        f.configure(f.config);
        f.controller->setIdleTimeoutMs(100);
        QTest::qWait(600);
        QVERIFY(!f.controller->isActive());

        // ... unless it is allowed over a track.
        ScreensaverConfig allowed = f.config;
        allowed.whilePlaying = true;
        f.configure(allowed);
        f.controller->setIdleTimeoutMs(100);
        QTRY_VERIFY_WITH_TIMEOUT(f.controller->isActive(), 3000);
    }

    void aTrackStartingWakesIt()
    {
        Fixture f;
        f.configure(f.config);
        f.controller->setIdleTimeoutMs(100);
        QTRY_VERIFY_WITH_TIMEOUT(f.controller->isActive(), 3000);
        f.media.setState(MediaController::State::Playing);
        QTRY_VERIFY_WITH_TIMEOUT(!f.controller->isActive(), 3000);
    }

    void aMissingOrWrongVideoLeavesTheScreensaverOff()
    {
        Fixture f;
        QSignalSpy active(f.controller.get(), &ScreensaverController::activeChanged);
        ScreensaverConfig config = f.config;
        config.video = QStringLiteral("nothing-here.mp4");
        f.configure(config);
        f.controller->setIdleTimeoutMs(100);
        QTest::qWait(500);
        QVERIFY(!f.controller->isActive());

        QFile text(f.dir.filePath("notes.txt")); // not a video extension
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.write("x");
        text.close();
        config.video = QStringLiteral("notes.txt");
        f.configure(config);
        f.controller->setIdleTimeoutMs(100);
        QTest::qWait(500);
        QVERIFY(!f.controller->isActive());
        QCOMPARE(active.count(), 0);
    }

    void withoutGeometryNothingStarts()
    {
        Fixture f;
        ScreensaverConfig config = f.config;
        config.hasGeometry = false;
        f.configure(config);
        f.controller->setIdleTimeoutMs(100);
        QTest::qWait(500);
        QVERIFY(!f.controller->isActive());
    }

    void aRealVideoIsDecodedAndLoopedByFfmpeg()
    {
        if (QStandardPaths::findExecutable("ffmpeg").isEmpty())
            QSKIP("ffmpeg is not installed");
        Fixture f;
        // A 1 second vertical clip made of FFmpeg's own test pattern (no outside material).
        QProcess make;
        make.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "testsrc2=size=270x480:rate=10:duration=1",
                              "-pix_fmt", "yuv420p", "-y", f.dir.filePath("real.mp4")});
        QVERIFY(make.waitForFinished(20000));
        if (make.exitCode() != 0)
            QSKIP("this ffmpeg cannot encode the test clip");
        f.controller->setFfmpegProgram("ffmpeg");
        ScreensaverConfig config = f.config;
        config.video = "real.mp4";
        f.configure(config);
        f.controller->setIdleTimeoutMs(100);
        QTRY_VERIFY_WITH_TIMEOUT(f.controller->isActive(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(f.screenReports() > 0 && f.keysDrawn().size() == 12, 10000);
        // The clip lasts one second: frames keep coming after it, so it loops.
        const int before = f.screenReports();
        QTest::qWait(2500);
        QVERIFY(f.screenReports() > before);
        QVERIFY(f.controller->isActive());
    }

    void aFailingFfmpegGivesUpAndLeavesTheNormalScreen()
    {
        Fixture f;
        QFile script(f.dir.filePath("failing-ffmpeg"));
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\nexit 1\n");
        script.setPermissions(script.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
        script.close();
        f.controller->setFfmpegProgram(f.dir.filePath("failing-ffmpeg"));
        f.configure(f.config);
        f.controller->setIdleTimeoutMs(100);
        QTest::qWait(1500);
        QVERIFY(!f.controller->isActive());
    }
};

QTEST_MAIN(TestScreensaver)
#include "test_screensaver.moc"
