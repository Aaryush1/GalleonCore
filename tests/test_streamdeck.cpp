// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FakeTransport.h"

#include <galleon/StreamDeck.h>

#include <QSignalSpy>
#include <QTest>

#include <memory>

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
        for (std::size_t i = 0; i < f.transport->writes.size(); ++i) {
            const auto &r = f.transport->writes[i];
            QCOMPARE(r.size(), galleon::protocol::OutputReportSize);
            QCOMPARE(int(r[0]), 0x02);
            QCOMPARE(int(r[1]), 0x0c);
            QCOMPARE(int(r[6] | (r[7] << 8)), galleon::protocol::ScreenWidth);
            QCOMPARE(int(r[8] | (r[9] << 8)), galleon::protocol::ScreenHeight);
            const int length = r[13] | (r[14] << 8);
            joined.append(reinterpret_cast<const char *>(r.data()) + 16, length);
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
            joined.append(reinterpret_cast<const char *>(r.data()) + 8, r[4] | (r[5] << 8));
        }
        QImage decoded;
        QVERIFY(decoded.loadFromData(joined, "JPEG"));
        QCOMPARE(decoded.size(), QSize(160, 160));
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
