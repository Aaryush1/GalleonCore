// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <MprisController.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QSignalSpy>
#include <QTest>

using galleond::MediaController;
using galleond::MprisController;

namespace {

// A music player as MPRIS describes it, on the session bus under a private name.
class FakePlayer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)

public:
    explicit FakePlayer(QString name, QString status = QStringLiteral("Paused"))
        : m_name(std::move(name))
        , m_status(std::move(status))
    {
        // Its own connection: two players of one process would otherwise share
        // one object path.
        m_connectionName = m_name;
        auto bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_connectionName);
        m_registered = bus.registerService(m_name)
                       && bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), this,
                                             QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties);
    }
    ~FakePlayer() override
    {
        {
            auto bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_connectionName);
            bus.unregisterObject(QStringLiteral("/org/mpris/MediaPlayer2"));
            bus.unregisterService(m_name);
        }
        QDBusConnection::disconnectFromBus(m_connectionName);
    }

    [[nodiscard]] bool registered() const { return m_registered; }
    [[nodiscard]] QString playbackStatus() const { return m_status; }
    [[nodiscard]] double volume() const { return m_volume; }
    void setVolume(double volume) { m_volume = volume; }
    void setStatus(const QString &status) { m_status = status; }

    int playPauseCount = 0;
    int nextCount = 0;
    int previousCount = 0;
    int stopCount = 0;
    qlonglong lastSeek = 0;

public Q_SLOTS:
    void PlayPause()
    {
        ++playPauseCount;
        m_status = m_status == QLatin1String("Playing") ? QStringLiteral("Paused") : QStringLiteral("Playing");
    }
    void Next() { ++nextCount; }
    void Previous() { ++previousCount; }
    void Stop()
    {
        ++stopCount;
        m_status = QStringLiteral("Stopped");
    }
    void Seek(qlonglong offset) { lastSeek = offset; }

private:
    QString m_name;
    QString m_connectionName;
    QString m_status;
    double m_volume = 0.5;
    bool m_registered = false;
};

QString uniqueName(const QString &suffix)
{
    return QStringLiteral("org.mpris.MediaPlayer2.galleontest%1x%2").arg(QCoreApplication::applicationPid()).arg(suffix);
}

} // namespace

class TestMprisController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("no D-Bus session bus");
    }

    void withoutAPlayerThereIsNoPlayer()
    {
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("none"));
        QTest::qWait(300);
        QCOMPARE(int(controller.state()), int(MediaController::State::NoPlayer));
        controller.playPause(); // must not crash or wait
        controller.changeVolume(5);
        controller.seek(5);
    }

    void aPlayerIsFoundWithItsState()
    {
        FakePlayer player(uniqueName("a"), QStringLiteral("Paused"));
        QVERIFY(player.registered());
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("a"));
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Paused), 4000);
    }

    void commandsReachThePlayerAndTheStateFollows()
    {
        FakePlayer player(uniqueName("b"), QStringLiteral("Paused"));
        QVERIFY(player.registered());
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("b"));
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Paused), 4000);

        QSignalSpy changed(&controller, &MediaController::stateChanged);
        controller.playPause();
        QTRY_COMPARE_WITH_TIMEOUT(player.playPauseCount, 1, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Playing), 4000);
        QVERIFY(changed.count() >= 1);

        controller.next();
        controller.previous();
        QTRY_COMPARE_WITH_TIMEOUT(player.nextCount, 1, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(player.previousCount, 1, 4000);

        controller.stop();
        QTRY_COMPARE_WITH_TIMEOUT(player.stopCount, 1, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Stopped), 4000);
    }

    void seekIsSentInMicroseconds()
    {
        FakePlayer player(uniqueName("c"));
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("c"));
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Paused), 4000);
        controller.seek(-10);
        QTRY_COMPARE_WITH_TIMEOUT(player.lastSeek, qlonglong(-10000000), 4000);
        controller.seek(5);
        QTRY_COMPARE_WITH_TIMEOUT(player.lastSeek, qlonglong(5000000), 4000);
    }

    void volumeMovesInPercentAndStaysInRange()
    {
        FakePlayer player(uniqueName("d"));
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("d"));
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Paused), 4000);

        controller.changeVolume(10);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(player.volume() - 0.6) < 0.001, 4000);
        controller.changeVolume(-20);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(player.volume() - 0.4) < 0.001, 4000);
        for (int i = 0; i < 12; ++i)
            controller.changeVolume(-10);
        QTRY_VERIFY_WITH_TIMEOUT(player.volume() >= 0.0 && player.volume() < 0.001, 4000);
        for (int i = 0; i < 12; ++i)
            controller.changeVolume(10);
        QTRY_VERIFY_WITH_TIMEOUT(player.volume() <= 1.0 && player.volume() > 0.999, 4000);
    }

    void thePlayingPlayerIsChosenOverAPausedOne()
    {
        const QString prefix = QStringLiteral("org.mpris.MediaPlayer2.galleontest%1y").arg(QCoreApplication::applicationPid());
        FakePlayer paused(prefix + QStringLiteral("1"), QStringLiteral("Paused"));
        FakePlayer playing(prefix + QStringLiteral("2"), QStringLiteral("Playing"));
        MprisController controller(QDBusConnection::sessionBus(), prefix);
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Playing), 4000);

        controller.next();
        QTRY_COMPARE_WITH_TIMEOUT(playing.nextCount, 1, 4000);
        QCOMPARE(paused.nextCount, 0);
    }

    void aPlayerThatQuitsLeavesNoPlayer()
    {
        auto player = std::make_unique<FakePlayer>(uniqueName("e"), QStringLiteral("Playing"));
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("e"));
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Playing), 4000);
        player.reset();
        controller.refreshNow();
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::NoPlayer), 6000);
    }

    void stateNamesMatchTheIconStates()
    {
        QCOMPARE(MediaController::stateName(MediaController::State::Playing), QStringLiteral("playing"));
        QCOMPARE(MediaController::stateName(MediaController::State::Paused), QStringLiteral("paused"));
        QCOMPARE(MediaController::stateName(MediaController::State::Stopped), QStringLiteral("stopped"));
        QCOMPARE(MediaController::stateName(MediaController::State::NoPlayer), QStringLiteral("stopped"));
    }
};

QTEST_GUILESS_MAIN(TestMprisController)
#include "test_mprisController.moc"
