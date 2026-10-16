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
