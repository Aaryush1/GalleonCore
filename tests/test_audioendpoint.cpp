// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <AudioEndpoint.h>
#include <QTest>

#include <limits>
#include <stdexcept>

using namespace galleond;
namespace {
class FakeEndpoint final : public AudioEndpoint
{
public:
    float level = 0.5F;
    bool mute = false;
    bool failRead = false;
    bool failWrite = false;
    int writes = 0;
    float volume() override
    {
        if (failRead) throw std::runtime_error("device removed");
        return level;
    }
    void setVolume(float scalar) override
    {
        if (failWrite) throw std::runtime_error("device removed");
        level = scalar;
        ++writes;
    }
    bool muted() override
    {
        if (failRead) throw std::runtime_error("device removed");
        return mute;
    }
    void setMuted(bool muted) override
    {
        if (failWrite) throw std::runtime_error("device removed");
        mute = muted;
        ++writes;
    }
};
}
class TestAudioEndpoint : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void relativeVolumeUsesPercentagePointsAndClamps_data()
    {
        QTest::addColumn<int>("delta");
        QTest::addColumn<float>("expected");
        QTest::newRow("up") << 10 << 0.6F;
        QTest::newRow("down") << -10 << 0.4F;
        QTest::newRow("upper bound") << 90 << 1.0F;
        QTest::newRow("lower bound") << -90 << 0.0F;
        QTest::newRow("largest positive") << std::numeric_limits<int>::max() << 1.0F;
        QTest::newRow("largest negative") << std::numeric_limits<int>::min() << 0.0F;
    }
    void relativeVolumeUsesPercentagePointsAndClamps()
    {
        QFETCH(int, delta);
        QFETCH(float, expected);
        FakeEndpoint endpoint;
        endpoint.mute = true;
        changeEndpointVolume(endpoint, delta);
        QCOMPARE(endpoint.level, expected);
        QVERIFY(endpoint.mute); // changing volume must not unexpectedly unmute
        QCOMPARE(endpoint.writes, 1);
    }
    void zeroVolumeDoesNotAccessTheDevice()
    {
        FakeEndpoint endpoint;
        endpoint.failRead = true;
        changeEndpointVolume(endpoint, 0);
        QCOMPARE(endpoint.writes, 0);
    }
    void toggleMutePreservesVolumeAndUsesCurrentState()
    {
        FakeEndpoint endpoint;
        toggleEndpointMute(endpoint);
        QVERIFY(endpoint.mute);
        toggleEndpointMute(endpoint);
        QVERIFY(!endpoint.mute);
        QCOMPARE(endpoint.level, 0.5F);
    }
    void invalidReadingsNeverBecomeVolumeWrites()
    {
        for (float value : {-0.1F, 1.1F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            FakeEndpoint endpoint;
            endpoint.level = value;
            QVERIFY_EXCEPTION_THROWN(changeEndpointVolume(endpoint, 1), std::runtime_error);
            QCOMPARE(endpoint.writes, 0);
        }
    }
    void readAndWriteFailuresPropagateAndTheNextCommandCanRecover()
    {
        FakeEndpoint endpoint;
        endpoint.failRead = true;
        QVERIFY_EXCEPTION_THROWN(changeEndpointVolume(endpoint, 10), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(toggleEndpointMute(endpoint), std::runtime_error);
        endpoint.failRead = false;
        endpoint.failWrite = true;
        QVERIFY_EXCEPTION_THROWN(changeEndpointVolume(endpoint, 10), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(toggleEndpointMute(endpoint), std::runtime_error);
        QCOMPARE(endpoint.writes, 0);
        endpoint.failWrite = false;
        changeEndpointVolume(endpoint, 10);
        toggleEndpointMute(endpoint);
        QCOMPARE(endpoint.level, 0.6F);
        QVERIFY(endpoint.mute);
    }
};
QTEST_GUILESS_MAIN(TestAudioEndpoint)
#include "test_audioendpoint.moc"
