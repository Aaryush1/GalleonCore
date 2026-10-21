// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Bytes.h"
#include "FakeTransport.h"

#include <galleon/StreamDeck.h>

#include <QSignalSpy>
#include <QTest>
#include <QThread>

#include <memory>
#include <utility>

using galleon::StreamDeck;

namespace {

struct Fixture
{
    FakeTransport *transport; // owned by deck
    std::unique_ptr<StreamDeck> deck;

    Fixture()
    {
        auto fake = std::make_unique<FakeTransport>();
        transport = fake.get();
        deck = std::make_unique<StreamDeck>(std::move(fake));
    }
};

} // namespace

class TestStreamDeck : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void openFailsWhenTransportFails()
    {
        Fixture f;
        f.transport->openSucceeds = false;
        QVERIFY(!f.deck->open());
        QVERIFY(!f.deck->isOpen());
        QVERIFY(f.transport->featureReports.empty());
    }

    void openSendsKeepAliveImmediately()
    {
        Fixture f;
        QVERIFY(f.deck->open());
        QCOMPARE(f.transport->featureReports.size(), std::size_t{1});
    }

    void keepAliveRepeatsWhileOpen()
    {
        Fixture f;
        f.deck->setKeepAliveInterval(10);
        QVERIFY(f.deck->open());
        QTRY_VERIFY_WITH_TIMEOUT(f.transport->featureReports.size() >= 4, 2000);
    }

    void slowImageTransfersDoNotWaitForTheEventLoopToKeepHostMode()
    {
        Fixture f;
        f.deck->setKeepAliveInterval(40);
        QVERIFY(f.deck->open());
        std::size_t keepAlivesBeforeLastWrite = 0;
        f.transport->beforeWrite = [&] {
            QThread::msleep(10); // Blocking USB writes: Qt's keep-alive timer cannot run here.
            keepAlivesBeforeLastWrite = f.transport->featureReports.size();
        };
        QImage image(160, 160, QImage::Format_RGB32);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x)
                image.setPixel(x, y, qRgb((x * 17 + y * 13) % 256, (x * 11 + y * 29) % 256, (x * 31 + y * 7) % 256));
        }
        QVERIFY(f.deck->setKeyImage(0, image));
        QVERIFY(keepAlivesBeforeLastWrite > 1); // A keep-alive was sent inside the transfer, without qWait/processEvents.
    }

    void keepAliveStopsAfterClose()
    {
        Fixture f;
        f.deck->setKeepAliveInterval(10);
        QVERIFY(f.deck->open());
        f.deck->close();
        const auto sent = f.transport->featureReports.size();
        QTest::qWait(100);
        QCOMPARE(f.transport->featureReports.size(), sent);
    }

    void disconnectIsDetected()
    {
        Fixture f;
        const QSignalSpy spy(f.deck.get(), &StreamDeck::disconnected);
        QVERIFY(f.deck->open());
        f.transport->disconnected = true;
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
        QVERIFY(!f.deck->isOpen());
    }

    void unknownReportsAreIgnored()
    {
        Fixture f;
        const QSignalSpy pressed(f.deck.get(), &StreamDeck::keyPressed);
        QVERIFY(f.deck->open());
        f.transport->pendingReads.push_back({0x01, 0x02, 0x03});
        QTRY_VERIFY_WITH_TIMEOUT(f.transport->pendingReads.empty(), 2000);
        QCOMPARE(pressed.count(), 0);
    }

    void keysEmitPressAndRelease()
    {
        Fixture f;
        const QSignalSpy pressed(f.deck.get(), &StreamDeck::keyPressed);
        const QSignalSpy released(f.deck.get(), &StreamDeck::keyReleased);
        QVERIFY(f.deck->open());

        std::vector<std::uint8_t> report = {0x01, 0x00, 0x0c, 0x00};
        report.resize(4 + galleon::protocol::KeyCount, 0);
        report[4 + 6] = 1;
        f.transport->pendingReads.push_back(report);
        QTRY_COMPARE_WITH_TIMEOUT(pressed.count(), 1, 2000);
        QCOMPARE(pressed.at(0).at(0).toInt(), 6);

        report[4 + 6] = 0;
        f.transport->pendingReads.push_back(report);
        QTRY_COMPARE_WITH_TIMEOUT(released.count(), 1, 2000);
        QCOMPARE(released.at(0).at(0).toInt(), 6);
    }

    void dialsEmitRotationAndPress()
    {
        Fixture f;
        const QSignalSpy rotated(f.deck.get(), &StreamDeck::dialRotated);
        const QSignalSpy pressed(f.deck.get(), &StreamDeck::dialPressed);
        QVERIFY(f.deck->open());

        f.transport->pendingReads.push_back({0x01, 0x03, 0x03, 0x00, 0x01, 0xfd, 0x00}); // dial 0: -3
        QTRY_COMPARE_WITH_TIMEOUT(rotated.count(), 1, 2000);
        QCOMPARE(rotated.at(0).at(0).toInt(), 0);
        QCOMPARE(rotated.at(0).at(1).toInt(), -3);

        f.transport->pendingReads.push_back({0x01, 0x03, 0x03, 0x00, 0x00, 0x00, 0x01}); // dial 1 pressed
        QTRY_COMPARE_WITH_TIMEOUT(pressed.count(), 1, 2000);
        QCOMPARE(pressed.at(0).at(0).toInt(), 1);
    }

    void malformedReportsEmitNothing()
    {
        Fixture f;
        const QSignalSpy pressed(f.deck.get(), &StreamDeck::keyPressed);
        QVERIFY(f.deck->open());
        std::vector<std::uint8_t> report = {0x01, 0x00, 0x0c, 0x00};
        report.resize(4 + galleon::protocol::KeyCount, 7); // bytes that are not 0 or 1
        f.transport->pendingReads.push_back(report);
        QTRY_VERIFY_WITH_TIMEOUT(f.transport->pendingReads.empty(), 2000);
        QCOMPARE(pressed.count(), 0);
    }

    void aReopenedDeckStartsWithNothingPressed()
    {
        Fixture f;
        const QSignalSpy pressed(f.deck.get(), &StreamDeck::keyPressed);
        QVERIFY(f.deck->open());
        std::vector<std::uint8_t> report = {0x01, 0x00, 0x0c, 0x00};
        report.resize(4 + galleon::protocol::KeyCount, 0);
        report[4] = 1;
        f.transport->pendingReads.push_back(report);
        QTRY_COMPARE_WITH_TIMEOUT(pressed.count(), 1, 2000);

        f.deck->close();
        QVERIFY(f.deck->open());
        f.transport->pendingReads.push_back(report); // key 0 still down on the new connection
        QTRY_COMPARE_WITH_TIMEOUT(pressed.count(), 2, 2000);
    }

    void imagesAreRejectedWhenClosed()
    {
        Fixture f;
        const QImage image(72, 72, QImage::Format_RGB32);
        QVERIFY(!f.deck->setKeyImage(0, image));
        QVERIFY(!f.deck->setScreenImage(image));
    }

    void screenImageIsSentAsJpegReports()
    {
        Fixture f;
        QVERIFY(f.deck->open());
        QImage image(300, 100, QImage::Format_RGB32); // not the screen size: it is scaled
        image.fill(Qt::red);
        QVERIFY(f.deck->setScreenImage(image));

        QVERIFY(!f.transport->writes.empty());
        QByteArray joined;
        for (const auto & r : f.transport->writes) {
            QCOMPARE(r.size(), galleon::protocol::OutputReportSize);
            QCOMPARE(int(r[0]), 0x02);
            QCOMPARE(int(r[1]), 0x0c);
            QCOMPARE(int(r[6] | (r[7] << 8)), galleon::protocol::ScreenWidth);
            QCOMPARE(int(r[8] | (r[9] << 8)), galleon::protocol::ScreenHeight);
            const int length = r[13] | (r[14] << 8);
            joined.append(bytesOf(r, 16, static_cast<std::size_t>(length)));
        }
        // A JPEG of the exact screen size.
        QVERIFY(joined.startsWith("\xff\xd8"));
        QVERIFY(joined.endsWith("\xff\xd9"));
        QImage decoded;
        QVERIFY(decoded.loadFromData(joined, "JPEG"));
        QCOMPARE(decoded.width(), galleon::protocol::ScreenWidth);
        QCOMPARE(decoded.height(), galleon::protocol::ScreenHeight);
        QVERIFY(qRed(decoded.pixel(10, 10)) > 200); // still red after the JPEG round trip
    }

    void keyImageIsSentAsJpegReports()
    {
        Fixture f;
        QVERIFY(f.deck->open());
        QImage image(160, 160, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(f.deck->setKeyImage(7, image));

        QVERIFY(!f.transport->writes.empty());
        QByteArray joined;
        for (const auto &r : f.transport->writes) {
            QCOMPARE(int(r[0]), 0x02);
            QCOMPARE(int(r[1]), 0x07);
            QCOMPARE(int(r[2]), 7);
            joined.append(bytesOf(r, 8, static_cast<std::size_t>(r[4] | (r[5] << 8))));
        }
        QImage decoded;
        QVERIFY(decoded.loadFromData(joined, "JPEG"));
        QCOMPARE(decoded.size(), QSize(160, 160));
    }

    void dialRingIsFourFeatureReports()
    {
        Fixture f;
        QVERIFY(f.deck->open());
        const auto before = f.transport->featureReports.size();
        QVERIFY(f.deck->setDialRing(1, 0, 255, 0));
        QCOMPARE(f.transport->featureReports.size(), before + 4);
        QCOMPARE(int(f.transport->featureReports.back()[4]), 255);
        QVERIFY(!f.deck->setDialRing(2, 0, 0, 0));
        QCOMPARE(f.transport->featureReports.size(), before + 4);
    }

    void everyWaitingReportIsReadInOneTick()
    {
        Fixture f;
        QVERIFY(f.deck->open());
        int turns = 0;
        QObject::connect(f.deck.get(), &StreamDeck::dialRotated, [&turns](int, int) { ++turns; });
        // Twelve dial reports waiting at once, as when the module sends faster than the timer ticks.
        for (int i = 0; i < 12; ++i)
            f.transport->pendingReads.push_back({0x01, 0x03, 0x03, 0x00, 0x01, 0x01, 0x00});
        QTest::qWait(25); // two ticks at most: one report per tick would have read two or three
        QVERIFY(f.transport->pendingReads.empty());
        QCOMPARE(turns, 12);
    }

    void everyPictureIsSentTwiceUnlessItStreams()
    {
        // The module shows a picture only when a second transfer follows it (measured on the
        // unit, 2026-10-19): a picture is sent twice in a row, a streamed one (video) once.
        Fixture f;
        QVERIFY(f.deck->open());
        QImage image(720, 384, QImage::Format_RGB32);
        image.fill(Qt::darkMagenta);
        const auto count = [&f](int command) {
            std::size_t n = 0;
            for (const auto &r : f.transport->writes) {
                if (r.size() > 3 && r[0] == 0x02 && std::cmp_equal(r[1], command))
                    ++n;
            }
            return n;
        };
        QVERIFY(f.deck->setScreenImage(image));
        const std::size_t twice = count(0x0c);
        QVERIFY(twice >= 2 && twice % 2 == 0);
        const auto first = std::vector<std::uint8_t>(f.transport->writes[0]);
        QCOMPARE(f.transport->writes[twice / 2], first); // the second copy is the same bytes

        f.transport->writes.clear();
        QVERIFY(f.deck->setScreenImage(image, 85, false));
        QCOMPARE(count(0x0c), twice / 2); // once

        f.transport->writes.clear();
        QImage key(160, 160, QImage::Format_RGB32);
        key.fill(Qt::red);
        QVERIFY(f.deck->setKeyImage(3, key));
        const std::size_t keyTwice = count(0x07);
        QVERIFY(keyTwice >= 2 && keyTwice % 2 == 0);
        f.transport->writes.clear();
        QVERIFY(f.deck->setKeyImage(3, key, false));
        QCOMPARE(count(0x07), keyTwice / 2);
    }

    void aLowerQualityMakesASmallerScreenPicture()
    {
        Fixture f;
        QVERIFY(f.deck->open());
        QImage detail(720, 384, QImage::Format_RGB32);
        for (int y = 0; y < detail.height(); ++y) {
            for (int x = 0; x < detail.width(); ++x)
                detail.setPixel(x, y, qRgb((x * 7 + y) % 256, (x ^ y) % 256, (x * y) % 256));
        }
        const auto before = f.transport->writes.size();
        QVERIFY(f.deck->setScreenImage(detail)); // 90
        const auto normal = f.transport->writes.size() - before;
        QVERIFY(f.deck->setScreenImage(detail, 60));
        const auto light = f.transport->writes.size() - before - normal;
        QVERIFY2(light < normal, "a lower quality needs fewer reports");
        QVERIFY(!f.deck->setScreenImage(detail, 0));
        QVERIFY(!f.deck->setScreenImage(detail, 101));
    }

    void brightnessIsSentAsFeatureReport()
    {
        Fixture f;
        QVERIFY(f.deck->open());
        const auto before = f.transport->featureReports.size();
        QVERIFY(f.deck->setBrightness(40));
        QCOMPARE(f.transport->featureReports.size(), before + 1);
        const auto &report = f.transport->featureReports.back();
        QCOMPARE(int(report[0]), 0x03);
        QCOMPARE(int(report[1]), 0x08);
        QCOMPARE(int(report[2]), 40);
    }

    void failedWriteIsReported()
    {
        Fixture f;
        QVERIFY(f.deck->open());
        QImage image(10, 10, QImage::Format_RGB32);
        image.fill(Qt::green);
        f.transport->close(); // writes now fail; the deck sees it is closed
        QVERIFY(!f.deck->setScreenImage(image));
    }

    void outOfRangeArgumentsAreRejected()
    {
        Fixture f;
        QVERIFY(f.deck->open());
        const QImage image(72, 72, QImage::Format_RGB32);
        QVERIFY(!f.deck->setKeyImage(-1, image));
        QVERIFY(!f.deck->setKeyImage(galleon::protocol::KeyCount, image));
        QVERIFY(!f.deck->setKeyImage(0, QImage()));
        QVERIFY(!f.deck->setBrightness(101));
        QVERIFY(f.transport->writes.empty());
    }
};

QTEST_GUILESS_MAIN(TestStreamDeck)
#include "test_streamdeck.moc"
