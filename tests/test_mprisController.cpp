// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <MprisController.h>

#include <QCoreApplication>
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
    [[nodiscard]] qlonglong position() const { return m_position; }
    void setPosition(qlonglong microseconds) { m_position = microseconds; }

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

    void theTrackIsReadWithItsTimeline()
    {
        FakePlayer player(uniqueName("m1"), QStringLiteral("Playing"));
        player.setMetadata({{"xesam:title", "A Song"},
                            {"xesam:artist", QStringList{"First", "Second"}},
                            {"mpris:artUrl", "https://example.org/cover.jpg"},
                            {"mpris:length", qlonglong(200'000'000)}});
        player.setPosition(61'500'000);
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("m1"));
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
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("m2"));
        QTRY_COMPARE_WITH_TIMEOUT(controller.nowPlaying().artist, QStringLiteral("Solo"), 4000);
    }

    void metadataFromAPlayerIsCleaned()
    {
        FakePlayer player(uniqueName("m3"), QStringLiteral("Playing"));
        player.setMetadata({{"xesam:title", QStringLiteral("Line1\nLine2\x01\ttab")},
                            {"xesam:artist", QStringList{QString(500, QLatin1Char('x'))}}});
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("m3"));
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
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("m4"));
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
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("m5"));
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
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("m8"));
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
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("m9"));
        QTRY_COMPARE_WITH_TIMEOUT(controller.nowPlaying().title, QStringLiteral("Real Title"), 4000);
    }

    void aStreamAddressGivesItsLastPart()
    {
        FakePlayer player(uniqueName("m10"), QStringLiteral("Playing"));
        player.setMetadata({{"xesam:url", "https://radio.example.org/live/jazz_fm.mp3?token=abc"}});
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("m10"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.nowPlaying().isEmpty(), 4000);
        QCOMPARE(controller.nowPlaying().title, QStringLiteral("jazz fm"));
    }

    void aUselessAddressGivesNoTitle()
    {
        FakePlayer player(uniqueName("m11"), QStringLiteral("Playing"));
        player.setMetadata({{"xesam:url", "file:///"}});
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("m11"));
        QTRY_COMPARE_WITH_TIMEOUT(int(controller.state()), int(MediaController::State::Playing), 4000);
        QVERIFY(controller.nowPlaying().isEmpty());
    }

    void withoutAPlayerTheTrackIsEmpty()
    {
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("m7none"));
        QTest::qWait(200);
        QVERIFY(controller.nowPlaying().isEmpty());
    }

    void theIconNameComesFromTheDesktopEntry()
    {
        QTemporaryDir data;
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
        MprisController controller(QDBusConnection::sessionBus(), uniqueName("m12"));
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
