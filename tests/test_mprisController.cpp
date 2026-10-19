// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <MprisController.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDBusAbstractAdaptor>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QDBusConnection>
#include <QSignalSpy>
#include <QTest>

using galleond::MediaController;
using galleond::MprisController;

namespace {

// The root interface of an MPRIS player, org.mpris.MediaPlayer2.
class RootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(bool Fullscreen READ fullscreen WRITE setFullscreen)

public:
    int raiseCount = 0;
    [[nodiscard]] bool fullscreen() const { return m_fullscreen; }
    void setFullscreen(bool on) { m_fullscreen = on; }

public Q_SLOTS:
    void Raise() { ++raiseCount; }

public:
    RootAdaptor(QObject *parent, QString entry)
        : QDBusAbstractAdaptor(parent)
        , m_entry(std::move(entry))
    {
    }
    [[nodiscard]] QString desktopEntry() const { return m_entry; }
    [[nodiscard]] static QString identity() { return QStringLiteral("Fake Player"); }

private:
    QString m_entry;
    bool m_fullscreen = false;
};

// A music player as MPRIS describes it, on the session bus under a private name.
class FakePlayer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)

public:
    explicit FakePlayer(QString name, QString status = QStringLiteral("Paused"), QString desktopEntry = {})
        : m_name(std::move(name))
        , m_status(std::move(status))
    {
        if (!desktopEntry.isEmpty())
            new RootAdaptor(this, std::move(desktopEntry)); // exported with the object
        // Its own connection: two players of one process would otherwise share
        // one object path.
        m_connectionName = m_name;
        auto bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_connectionName);
        m_registered = bus.registerService(m_name)
                       && bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), this,
                                             QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties | QDBusConnection::ExportAdaptors);
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
    [[nodiscard]] QVariantMap metadata() const { return m_metadata; }
    void setMetadata(const QVariantMap &metadata) { m_metadata = metadata; }
    [[nodiscard]] bool shuffle() const { return m_shuffle; }
    void setShuffle(bool on) { m_shuffle = on; }
    [[nodiscard]] QString loopStatus() const { return m_loop; }
    void setLoopStatus(const QString &status) { m_loop = status; }
    [[nodiscard]] RootAdaptor *root() const { return findChild<RootAdaptor *>(); }
    [[nodiscard]] qlonglong position() const { return m_position; }
    void setPosition(qlonglong microseconds) { m_position = microseconds; }

    // What a real player sends when its track or status changes.
    void notifyChanged()
    {
        auto bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_connectionName);
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/org/mpris/MediaPlayer2"),
                                                         QStringLiteral("org.freedesktop.DBus.Properties"),
                                                         QStringLiteral("PropertiesChanged"));
        signal << QStringLiteral("org.mpris.MediaPlayer2.Player") << QVariantMap{{"PlaybackStatus", m_status}} << QStringList();
        bus.send(signal);
    }

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
    QVariantMap m_metadata;
    qlonglong m_position = 0;
    bool m_shuffle = false;
    QString m_loop = QStringLiteral("None");
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
        controller.toggleShuffle(); // nor these, without any player
        controller.cycleRepeat();
        controller.toggleFullscreen();
        controller.raisePlayer();
        controller.restartTrack();
        controller.toggleMute();
    }

    void withoutAPlayerTheDialSetsTheSystemVolumeThroughAFixedCommand()
    {
        QTemporaryDir const dir;
        const QString log = dir.filePath("calls.log");
        const QString script = dir.filePath("fake-wpctl");
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\necho \"$@\" >> '" + log.toUtf8() + "'\n");
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
        file.close();

        MprisController controller(QDBusConnection::sessionBus(), uniqueName("sysvol"));
        // A test controller never touches the real system volume by itself.
        controller.changeVolume(5);
        QTest::qWait(200);
        QVERIFY(!QFile::exists(log));

        controller.setSystemVolumeProgram(script);
        controller.changeVolume(2);
        controller.changeVolume(2);
        controller.changeVolume(2); // a fast turn: one command for the whole burst
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(log), 3000);
        QTest::qWait(300);
        QFile calls(log);
        QVERIFY(calls.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(calls.readAll()).trimmed().split('\n');
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines[0], QStringLiteral("set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 6%+"));

        controller.changeVolume(-4);
        QTRY_VERIFY_WITH_TIMEOUT(QFile(log).size() > 50, 3000);
        QFile again(log);
        QVERIFY(again.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(again.readAll()).trimmed().endsWith(QStringLiteral("@DEFAULT_AUDIO_SINK@ 4%-")));
    }

    void shuffleRepeatFullscreenRaiseAndRestartReachThePlayer()
    {
        FakePlayer const player(uniqueName("ctl"), QStringLiteral("Playing"), QStringLiteral("fake"));
        QVERIFY(player.registered());
        QVERIFY(player.root());
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("ctl"));
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Playing), 4000);

        QSignalSpy const changed(&controller, &MediaController::stateChanged);
        QVERIFY(!controller.shuffleOn());
        controller.toggleShuffle();
        QTRY_VERIFY_WITH_TIMEOUT(player.shuffle(), 3000);
        // The state is read again at once: the key shows it, and the keys are told to redraw.
        QTRY_VERIFY_WITH_TIMEOUT(controller.shuffleOn(), 3000);
        QVERIFY(changed.count() >= 1);
        controller.toggleShuffle();
        QTRY_VERIFY_WITH_TIMEOUT(!player.shuffle(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.shuffleOn(), 3000);

        // Repeat goes round: none, the playlist, the track, none.
        QCOMPARE(int(controller.loopMode()), int(MediaController::Loop::None));
        controller.cycleRepeat();
        QTRY_COMPARE_WITH_TIMEOUT(player.loopStatus(), QStringLiteral("Playlist"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.loopMode()), int(MediaController::Loop::Playlist), 3000);
        controller.cycleRepeat();
        QTRY_COMPARE_WITH_TIMEOUT(player.loopStatus(), QStringLiteral("Track"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.loopMode()), int(MediaController::Loop::Track), 3000);
        controller.cycleRepeat();
        QTRY_COMPARE_WITH_TIMEOUT(player.loopStatus(), QStringLiteral("None"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.loopMode()), int(MediaController::Loop::None), 3000);

        controller.toggleFullscreen();
        QTRY_VERIFY_WITH_TIMEOUT(player.root()->fullscreen(), 3000);
        controller.toggleFullscreen();
        QTRY_VERIFY_WITH_TIMEOUT(!player.root()->fullscreen(), 3000);

        controller.raisePlayer();
        QTRY_COMPARE_WITH_TIMEOUT(player.root()->raiseCount, 1, 3000);

        // Back to the start: a seek further back than the start (MPRIS goes to 0).
        controller.restartTrack();
        QTRY_VERIFY_WITH_TIMEOUT(player.lastSeek < -3600LL * 1000000LL, 3000);
    }

    void muteSetsTheSystemSoundThroughAFixedCommand()
    {
        QTemporaryDir const dir;
        const QString log = dir.filePath("calls.log");
        const QString script = dir.filePath("fake-wpctl");
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\necho \"$@\" >> '" + log.toUtf8() + "'\n");
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
        file.close();
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("mute"));
        controller.toggleMute(); // a test controller never touches the sound of the machine by itself
        QTest::qWait(200);
        QVERIFY(!QFile::exists(log));
        controller.setSystemVolumeProgram(script);
        controller.toggleMute();
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(log), 3000);
        QFile calls(log);
        QVERIFY(calls.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(calls.readAll()).trimmed(), QStringLiteral("set-mute @DEFAULT_AUDIO_SINK@ toggle"));
    }

    void aPlayerIsFoundWithItsState()
    {
        FakePlayer const player(uniqueName("a"), QStringLiteral("Paused"));
        QVERIFY(player.registered());
        MprisController const controller(QDBusConnection::sessionBus(), uniqueName("a"));
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Paused), 4000);
    }

    void commandsReachThePlayerAndTheStateFollows()
    {
        FakePlayer const player(uniqueName("b"), QStringLiteral("Paused"));
        QVERIFY(player.registered());
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("b"));
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Paused), 4000);

        QSignalSpy const changed(&controller, &MediaController::stateChanged);
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
        FakePlayer const player(uniqueName("c"));
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("c"));
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Paused), 4000);
        controller.seek(-10);
        QTRY_COMPARE_WITH_TIMEOUT(player.lastSeek, qlonglong(-10000000), 4000);
        controller.seek(5);
        QTRY_COMPARE_WITH_TIMEOUT(player.lastSeek, qlonglong(5000000), 4000);
    }

    void volumeMovesInPercentAndStaysInRange()
    {
        FakePlayer const player(uniqueName("d"));
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
        FakePlayer const paused(prefix + QStringLiteral("1"), QStringLiteral("Paused"));
        FakePlayer const playing(prefix + QStringLiteral("2"), QStringLiteral("Playing"));
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

    void theTrackIsReadWithItsTimeline()
    {
        FakePlayer player(uniqueName("m1"), QStringLiteral("Playing"));
        player.setMetadata({{"xesam:title", "A Song"},
                            {"xesam:artist", QStringList{"First", "Second"}},
                            {"mpris:artUrl", "https://example.org/cover.jpg"},
                            {"mpris:length", qlonglong(200'000'000)}});
        player.setPosition(61'500'000);
        MprisController const controller(QDBusConnection::sessionBus(), uniqueName("m1"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.nowPlaying().isEmpty(), 4000);

        const galleond::NowPlaying track = controller.nowPlaying();
        QCOMPARE(track.title, QStringLiteral("A Song"));
        QCOMPARE(track.artist, QStringLiteral("First, Second"));
        QCOMPARE(track.artUrl, QStringLiteral("https://example.org/cover.jpg"));
        QCOMPARE(track.durationSeconds, 200.0);
        QVERIFY(qAbs(track.positionSeconds - 61.5) < 0.001);
    }

    void aSingleStringArtistIsAccepted()
    {
        FakePlayer player(uniqueName("m2"), QStringLiteral("Playing"));
        player.setMetadata({{"xesam:title", "T"}, {"xesam:artist", "Solo"}});
        MprisController const controller(QDBusConnection::sessionBus(), uniqueName("m2"));
        QTRY_COMPARE_WITH_TIMEOUT(controller.nowPlaying().artist, QStringLiteral("Solo"), 4000);
    }

    void metadataFromAPlayerIsCleaned()
    {
        FakePlayer player(uniqueName("m3"), QStringLiteral("Playing"));
        player.setMetadata({{"xesam:title", QStringLiteral("Line1\nLine2\x01\ttab")},
                            {"xesam:artist", QStringList{QString(500, QLatin1Char('x'))}}});
        MprisController const controller(QDBusConnection::sessionBus(), uniqueName("m3"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.nowPlaying().isEmpty(), 4000);
        QCOMPARE(controller.nowPlaying().title, QStringLiteral("Line1 Line2 tab"));
        QCOMPARE(controller.nowPlaying().artist.size(), galleond::MaxMetadataLength);
    }

    void oddMetadataNeverBreaksAnything()
    {
        FakePlayer player(uniqueName("m4"), QStringLiteral("Playing"));
        player.setMetadata({{"xesam:title", 42},
                            {"xesam:artist", QVariantList{1, 2}},
                            {"mpris:length", "not a number"},
                            {"mpris:artUrl", QString(5000, QLatin1Char('u'))}});
        player.setPosition(-5);
        MprisController const controller(QDBusConnection::sessionBus(), uniqueName("m4"));
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Playing), 4000);
        const galleond::NowPlaying track = controller.nowPlaying();
        QCOMPARE(track.durationSeconds, 0.0);
        QCOMPARE(track.positionSeconds, 0.0);
        QVERIFY(track.artUrl.size() <= 2048);
    }

    void thePositionIsNeverBeyondTheDuration()
    {
        FakePlayer player(uniqueName("m5"), QStringLiteral("Playing"));
        player.setMetadata({{"xesam:title", "T"}, {"mpris:length", qlonglong(10'000'000)}});
        player.setPosition(99'000'000);
        MprisController const controller(QDBusConnection::sessionBus(), uniqueName("m5"));
        QTRY_VERIFY_WITH_TIMEOUT(controller.nowPlaying().durationSeconds > 0, 4000);
        QCOMPARE(controller.nowPlaying().positionSeconds, 10.0);
    }

    void theChangeOfTrackAndTheTimelineAreAnnounced()
    {
        FakePlayer player(uniqueName("m6"), QStringLiteral("Playing"));
        player.setMetadata({{"xesam:title", "One"}, {"mpris:length", qlonglong(100'000'000)}});
        player.setPosition(1'000'000);
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("m6"));
        QTRY_COMPARE_WITH_TIMEOUT(controller.nowPlaying().title, QStringLiteral("One"), 4000);

        QSignalSpy changed(&controller, &MediaController::nowPlayingChanged);
        player.setPosition(5'000'000); // the timeline moves
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 4000);
        changed.clear();
        player.setMetadata({{"xesam:title", "Two"}});
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 4000);
        QCOMPARE(controller.nowPlaying().title, QStringLiteral("Two"));
    }

    void aVideoWithoutTagsGetsItsFileNameAsTitle()
    {
        // What VLC sends for an untagged file: an address and a length, no title.
        FakePlayer player(uniqueName("m8"), QStringLiteral("Playing"));
        player.setMetadata({{"xesam:url", "file:///run/media/paul/Bureau%20ext-%2B/Download/Quand%20tu%20es%20l%C3%A0%20Stems/Quand-tu-es-la_sous-titres.mp4"},
                            {"mpris:length", qlonglong(263'600'000)}});
        MprisController const controller(QDBusConnection::sessionBus(), uniqueName("m8"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.nowPlaying().isEmpty(), 4000);
        QCOMPARE(controller.nowPlaying().title, QStringLiteral("Quand-tu-es-la sous-titres")); // no extension, _ as a space
        QVERIFY(controller.nowPlaying().artist.isEmpty());
        QVERIFY(controller.nowPlaying().url.startsWith("file:///run/media/paul/"));
        QCOMPARE(controller.nowPlaying().durationSeconds, 263.6);
    }

    void aRealTitleIsNeverReplacedByTheFileName()
    {
        FakePlayer player(uniqueName("m9"), QStringLiteral("Playing"));
        player.setMetadata({{"xesam:title", "Real Title"}, {"xesam:url", "file:///music/other-name.flac"}});
        MprisController const controller(QDBusConnection::sessionBus(), uniqueName("m9"));
        QTRY_COMPARE_WITH_TIMEOUT(controller.nowPlaying().title, QStringLiteral("Real Title"), 4000);
    }

    void aStreamAddressGivesItsLastPart()
    {
        FakePlayer player(uniqueName("m10"), QStringLiteral("Playing"));
        player.setMetadata({{"xesam:url", "https://radio.example.org/live/jazz_fm.mp3?token=abc"}});
        MprisController const controller(QDBusConnection::sessionBus(), uniqueName("m10"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.nowPlaying().isEmpty(), 4000);
        QCOMPARE(controller.nowPlaying().title, QStringLiteral("jazz fm"));
    }

    void aUselessAddressGivesNoTitle()
    {
        FakePlayer player(uniqueName("m11"), QStringLiteral("Playing"));
        player.setMetadata({{"xesam:url", "file:///"}});
        MprisController const controller(QDBusConnection::sessionBus(), uniqueName("m11"));
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Playing), 4000);
        QVERIFY(controller.nowPlaying().isEmpty());
    }

    void aChangeOfTrackIsSeenAtOnceThroughTheNotification()
    {
        FakePlayer player(uniqueName("n1"), QStringLiteral("Playing"));
        player.setMetadata({{"xesam:title", "First"}});
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("n1"));
        controller.setPollIntervals(60000, 60000); // the polling cannot be what sees the change
        QTRY_COMPARE_WITH_TIMEOUT(controller.nowPlaying().title, QStringLiteral("First"), 4000);

        QElapsedTimer clock;
        clock.start();
        player.setMetadata({{"xesam:title", "Second"}});
        player.notifyChanged();
        QTRY_COMPARE_WITH_TIMEOUT(controller.nowPlaying().title, QStringLiteral("Second"), 500);
        QVERIFY2(clock.elapsed() < 500, qPrintable(QString::number(clock.elapsed())));
    }

    void aStatusChangeIsSeenAtOnce()
    {
        FakePlayer player(uniqueName("n2"), QStringLiteral("Playing"));
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("n2"));
        controller.setPollIntervals(60000, 60000);
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Playing), 4000);
        QSignalSpy const changed(&controller, &MediaController::stateChanged);
        player.setStatus(QStringLiteral("Paused"));
        player.notifyChanged();
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Paused), 500);
        QVERIFY(changed.count() >= 1);
    }

    void aPlayerThatStartsOrQuitsIsSeenAtOnce()
    {
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("n3"));
        controller.setPollIntervals(60000, 60000);
        QTest::qWait(100);
        QCOMPARE(int(controller.state()), int(MediaController::State::NoPlayer));

        auto player = std::make_unique<FakePlayer>(uniqueName("n3"), QStringLiteral("Playing"));
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Playing), 800); // found without polling
        player.reset();
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::NoPlayer), 800);
    }

    void aBurstOfNotificationsIsReadOnce()
    {
        FakePlayer player(uniqueName("n4"), QStringLiteral("Playing"));
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("n4"));
        controller.setPollIntervals(60000, 60000);
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Playing), 4000);
        for (int i = 0; i < 50; ++i)
            player.notifyChanged(); // a flood must not make the controller misbehave
        QTest::qWait(200);
        QCOMPARE(int(controller.state()), int(MediaController::State::Playing));
    }

    void withoutAPlayerTheTrackIsEmpty()
    {
        MprisController const controller(QDBusConnection::sessionBus(), uniqueName("m7none"));
        QTest::qWait(200);
        QVERIFY(controller.nowPlaying().isEmpty());
    }

    void theIconNameComesFromTheDesktopEntry()
    {
        QTemporaryDir const data;
        QDir().mkpath(data.filePath("applications"));
        QFile entry(data.filePath("applications/galleontest-player.desktop"));
        QVERIFY(entry.open(QIODevice::WriteOnly));
        entry.write("[Desktop Entry]\nType=Application\nName=Test\nIcon=galleon-test-icon\n");
        entry.close();
        QFile pathIcon(data.filePath("applications/galleontest-path.desktop"));
        QVERIFY(pathIcon.open(QIODevice::WriteOnly));
        pathIcon.write("[Desktop Entry]\nName=Test\nIcon=/usr/share/pixmaps/evil.png\n");
        pathIcon.close();
        const QByteArray previous = qgetenv("XDG_DATA_DIRS");
        qputenv("XDG_DATA_DIRS", data.path().toUtf8());

        QCOMPARE(MprisController::iconNameForDesktopEntry("galleontest-player"), QStringLiteral("galleon-test-icon"));
        QCOMPARE(MprisController::iconNameForDesktopEntry("galleontest-path"), QStringLiteral("galleontest-path")); // a path is not used
        QCOMPARE(MprisController::iconNameForDesktopEntry("vlc"), QStringLiteral("vlc")); // no file: the entry itself
        QVERIFY(MprisController::iconNameForDesktopEntry("../../etc/passwd").isEmpty());
        QVERIFY(MprisController::iconNameForDesktopEntry("a b").isEmpty());
        QVERIFY(MprisController::iconNameForDesktopEntry({}).isEmpty());
        QVERIFY(MprisController::iconNameForDesktopEntry(QString(101, QLatin1Char('x'))).isEmpty());

        qputenv("XDG_DATA_DIRS", previous);
    }

    void thePlayerIconIsReadFromTheRootInterface()
    {
        FakePlayer player(uniqueName("m12"), QStringLiteral("Playing"), QStringLiteral("vlc"));
        player.setMetadata({{"xesam:url", "file:///videos/film.mp4"}});
        MprisController const controller(QDBusConnection::sessionBus(), uniqueName("m12"));
        QTRY_COMPARE_WITH_TIMEOUT(controller.nowPlaying().playerIcon, QStringLiteral("vlc"), 5000);
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
