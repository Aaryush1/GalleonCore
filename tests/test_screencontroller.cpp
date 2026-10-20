// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Bytes.h"
#include "FakeTransport.h"

#include <ScreenController.h>
#include <ScreenRenderer.h>
#include <galleon/StreamDeck.h>

#include <QDir>
#include <QIcon>
#include <QImage>
#include <QTemporaryDir>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace galleond;
using galleon::StreamDeck;

namespace {

// These tests write the saved wallpaper: keep it away from the user's real
// ~/.config, even when the binary is run by hand and not through ctest.
// Before anything reads a setting: the tests keep to their own configuration folder.
void isolateSettings()
{
    qputenv("XDG_CONFIG_HOME", (QDir::tempPath() + QStringLiteral("/galleoncore-test-config")).toUtf8());
}
Q_CONSTRUCTOR_FUNCTION(isolateSettings)


const QString &widgetsDir()
{
    static const QString dir = QStringLiteral(GALLEON_SOURCE_WIDGETS);
    return dir;
}

class FakeMedia : public MediaController
{
public:
    [[nodiscard]] State state() const override { return m_state; }
    [[nodiscard]] NowPlaying nowPlaying() const override { return m_track; }
    void playPause() override {}
    void next() override {}
    void previous() override {}
    void stop() override {}
    void changeVolume(int /*percent*/) override {}
    void seek(int /*seconds*/) override {}

    void set(State state, const NowPlaying &track)
    {
        m_state = state;
        m_track = track;
        Q_EMIT stateChanged();
        Q_EMIT nowPlayingChanged();
    }

private:
    State m_state = State::NoPlayer;
    NowPlaying m_track;
};

bool near(QRgb a, QRgb b, int tolerance = 28) // JPEG changes colours a little
{
    return qAbs(qRed(a) - qRed(b)) <= tolerance && qAbs(qGreen(a) - qGreen(b)) <= tolerance
           && qAbs(qBlue(a) - qBlue(b)) <= tolerance;
}

QRgb pixelOf(const QImage &image, int x, int y)
{
    return image.pixel(x, y);
}

struct Fixture
{
    QTemporaryDir dir;
    FakeTransport *transport;
    std::unique_ptr<StreamDeck> deck;
    std::unique_ptr<ScreenRenderer> wallpaperRenderer;
    std::unique_ptr<ScreenRenderer> nowPlayingRenderer;
    FakeMedia media;
    std::unique_ptr<ScreenController> controller;

    Fixture()
    {
        // One wallpaper: a flat green, easy to tell from the dark track screen.
        QImage green(1600, 900, QImage::Format_RGB32);
        green.fill(QColor(30, 160, 60));
        QDir().mkpath(dir.filePath("wall/style"));
        QVERIFY(green.save(dir.filePath("wall/style/green.png")));

        auto fake = std::make_unique<FakeTransport>();
        transport = fake.get();
        deck = std::make_unique<StreamDeck>(std::move(fake));
        QVERIFY(deck->open());
        wallpaperRenderer = std::make_unique<ScreenRenderer>(widgetsDir() + "/WallpaperClock.qml");
        nowPlayingRenderer = std::make_unique<ScreenRenderer>(widgetsDir() + "/NowPlaying.qml");
        controller = std::make_unique<ScreenController>(deck.get(), wallpaperRenderer.get(),
                                                        WallpaperCatalog(dir.filePath("wall")), widgetsDir() + "/WallpaperClock.qml",
                                                        &media, nowPlayingRenderer.get());
    }

    [[nodiscard]] QString wallpaper() const { return QStringLiteral("style/green.png"); }

    // The last complete screen frame the deck was sent, decoded; null if none.
    [[nodiscard]] QImage lastScreen() const
    {
        const auto &writes = transport->writes;
        qsizetype last = -1;
        for (qsizetype i = static_cast<qsizetype>(writes.size()) - 1; i >= 0; --i) {
            const auto &r = writes[static_cast<std::size_t>(i)];
            if (r.size() > 10 && r[0] == 0x02 && r[1] == 0x0c && r[10] == 1) {
                last = i;
                break;
            }
        }
        if (last < 0)
            return {};
        QByteArray jpeg;
        for (qsizetype i = last; i >= 0; --i) {
            const auto &r = writes[static_cast<std::size_t>(i)];
            if (r.size() < 16 || r[1] != 0x0c)
                break;
            const int length = r[13] | (r[14] << 8);
            jpeg.prepend(bytesOf(r, 16, static_cast<std::size_t>(length)));
            if ((r[11] | (r[12] << 8)) == 0)
                break;
        }
        QImage image;
        image.loadFromData(jpeg, "JPEG");
        return image;
    }

    // The pictures sent to the screen. Each one goes out twice in a row (the module shows a
    // picture when a second transfer follows it), and counts once.
    [[nodiscard]] std::size_t frames() const
    {
        std::size_t count = 0;
        for (const auto &r : transport->writes) {
            if (r.size() > 10 && r[0] == 0x02 && r[1] == 0x0c && r[10] == 1)
                ++count;
        }
        return count / 2;
    }

    static NowPlaying track(double position = 10, double duration = 100, const QString &art = {})
    {
        NowPlaying t;
        t.title = QStringLiteral("A Title");
        t.artist = QStringLiteral("An Artist");
        t.artUrl = art;
        t.positionSeconds = position;
        t.durationSeconds = duration;
        return t;
    }
};

constexpr int FarRightX = 705;
constexpr int Row = 200;
constexpr QRgb Wallpaper = qRgb(30, 160, 60);
constexpr QRgb TrackScreen = qRgb(0x14, 0x17, 0x1c);

} // namespace

class TestScreenController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void nothingIsDrawnBeforeAWallpaperIsChosen()
    {
        Fixture f;
        f.media.set(MediaController::State::Playing, Fixture::track());
        QCOMPARE(f.frames(), std::size_t{0});
    }

    void aPictureAddedWhileItRunsCanBeShown()
    {
        Fixture f;
        QVERIFY(!f.controller->SetWallpaper("mine/new.png")); // not there yet
        QImage red(1600, 900, QImage::Format_RGB32);
        red.fill(QColor(200, 30, 30));
        QDir().mkpath(f.dir.filePath("wall/mine"));
        QVERIFY(red.save(f.dir.filePath("wall/mine/new.png"))); // the settings window adds it
        QVERIFY(f.controller->SetWallpaper("mine/new.png"));    // the catalog is read again before refusing
        QVERIFY(near(pixelOf(f.lastScreen(), FarRightX, Row), qRgb(200, 30, 30)));
        QVERIFY(f.controller->Wallpapers().contains("mine/new.png"));
        QVERIFY(!f.controller->SetWallpaper("mine/never-there.png"));
    }

    void theWallpaperShowsWhenNothingPlays()
    {
        Fixture f;
        QVERIFY(f.controller->SetWallpaper(f.wallpaper()));
        QVERIFY(near(pixelOf(f.lastScreen(), FarRightX, Row), Wallpaper));
    }

    void aPlayingTrackTakesTheScreen()
    {
        Fixture f;
        QVERIFY(f.controller->SetWallpaper(f.wallpaper()));
        f.media.set(MediaController::State::Playing, Fixture::track(25, 100));
        const QImage screen = f.lastScreen();
        QVERIFY(near(pixelOf(screen, FarRightX, Row), TrackScreen));
        QVERIFY(near(pixelOf(screen, 388 + 38, 317), qRgb(0x4c, 0xaf, 0x50))); // the bar, 25 % filled, playing
    }

    void whenTheMusicStopsTheWallpaperComesBack()
    {
        Fixture f;
        QVERIFY(f.controller->SetWallpaper(f.wallpaper()));
        f.media.set(MediaController::State::Playing, Fixture::track());
        QVERIFY(near(pixelOf(f.lastScreen(), FarRightX, Row), TrackScreen));
        f.media.set(MediaController::State::Stopped, NowPlaying{});
        QVERIFY(near(pixelOf(f.lastScreen(), FarRightX, Row), Wallpaper));
    }

    void pausedFollowsTheConfiguredMode()
    {
        Fixture f;
        QVERIFY(f.controller->SetWallpaper(f.wallpaper()));
        f.media.set(MediaController::State::Paused, Fixture::track());
        QVERIFY(near(pixelOf(f.lastScreen(), FarRightX, Row), Wallpaper)); // the default: playing only

        ScreenConfig config;
        config.nowPlaying = NowPlayingMode::PlayingOrPaused;
        f.controller->setScreenConfig(config);
        const QImage screen = f.lastScreen();
        QVERIFY(near(pixelOf(screen, FarRightX, Row), TrackScreen));
        QVERIFY(near(pixelOf(screen, 388 + 15, 317), qRgb(0x8a, 0x92, 0x9d))); // a paused bar is grey
    }

    void neverKeepsTheWallpaperWhilePlaying()
    {
        Fixture f;
        QVERIFY(f.controller->SetWallpaper(f.wallpaper()));
        ScreenConfig config;
        config.nowPlaying = NowPlayingMode::Never;
        f.controller->setScreenConfig(config);
        f.media.set(MediaController::State::Playing, Fixture::track());
        QVERIFY(near(pixelOf(f.lastScreen(), FarRightX, Row), Wallpaper));
    }

    void theTimelineRedrawsEverySecond()
    {
        Fixture f;
        QVERIFY(f.controller->SetWallpaper(f.wallpaper()));
        f.media.set(MediaController::State::Playing, Fixture::track(10, 100));
        const auto before = f.frames();
        f.media.set(MediaController::State::Playing, Fixture::track(11, 100));
        QCOMPARE(f.frames(), before + 1);
    }

    void aLocalCoverIsShownAndReplaced()
    {
        Fixture f;
        QVERIFY(f.controller->SetWallpaper(f.wallpaper()));
        QImage red(200, 200, QImage::Format_RGB32);
        red.fill(QColor(210, 30, 30));
        QImage blue(200, 200, QImage::Format_RGB32);
        blue.fill(QColor(30, 30, 210));
        QVERIFY(red.save(f.dir.filePath("red.png")));
        QVERIFY(blue.save(f.dir.filePath("blue.png")));

        f.media.set(MediaController::State::Playing, Fixture::track(1, 100, QUrl::fromLocalFile(f.dir.filePath("red.png")).toString()));
        QVERIFY(near(pixelOf(f.lastScreen(), 190, 190), qRgb(210, 30, 30)));

        f.media.set(MediaController::State::Playing, Fixture::track(1, 100, QUrl::fromLocalFile(f.dir.filePath("blue.png")).toString()));
        QVERIFY(near(pixelOf(f.lastScreen(), 190, 190), qRgb(30, 30, 210)));

        f.media.set(MediaController::State::Playing, Fixture::track(1, 100, QString())); // no cover any more
        QVERIFY(near(pixelOf(f.lastScreen(), 40, 40), qRgb(0x23, 0x28, 0x30))); // the placeholder
    }

    void aTrackWithoutACoverUsesTheImageNextToItsFile()
    {
        Fixture f;
        QVERIFY(f.controller->SetWallpaper(f.wallpaper()));
        QImage orange(200, 200, QImage::Format_RGB32);
        orange.fill(QColor(230, 130, 20));
        QVERIFY(orange.save(f.dir.filePath("cover.png")));

        NowPlaying video = Fixture::track(5, 100);
        video.url = QUrl::fromLocalFile(f.dir.filePath("movie.mp4")).toString(); // no artUrl, as VLC does
        f.media.set(MediaController::State::Playing, video);
        QVERIFY(near(pixelOf(f.lastScreen(), 190, 190), qRgb(230, 130, 20)));

        // The folder is not read again at every second of the timeline: remove the
        // image and move on by one second; the cover stays.
        QVERIFY(QFile::remove(f.dir.filePath("cover.png")));
        video.positionSeconds = 6;
        f.media.set(MediaController::State::Playing, video);
        QVERIFY(near(pixelOf(f.lastScreen(), 190, 190), qRgb(230, 130, 20)));
    }

    // A throw-away icon theme holding one icon: "galleon-test-player".
    static bool installTestTheme(const QString &root, QColor colour)
    {
        QDir().mkpath(root + "/galleontest/48x48/apps");
        QFile index(root + "/galleontest/index.theme");
        if (!index.open(QIODevice::WriteOnly))
            return false;
        index.write("[Icon Theme]\nName=galleontest\nDirectories=48x48/apps\n\n[48x48/apps]\nSize=48\nType=Fixed\n");
        index.close();
        QImage icon(48, 48, QImage::Format_ARGB32);
        icon.fill(colour);
        if (!icon.save(root + "/galleontest/48x48/apps/galleon-test-player.png"))
            return false;
        QIcon::setThemeSearchPaths({root});
        QIcon::setThemeName("galleontest");
        return true;
    }

    void aTrackWithoutACoverShowsThePlayerIcon()
    {
        Fixture f;
        QVERIFY(installTestTheme(f.dir.filePath("icons"), QColor(10, 200, 220)));
        QVERIFY(f.controller->SetWallpaper(f.wallpaper()));

        NowPlaying video = Fixture::track(5, 100);
        video.playerIcon = "galleon-test-player"; // what MPRIS' DesktopEntry gives
        f.media.set(MediaController::State::Playing, video);
        QVERIFY(near(pixelOf(f.lastScreen(), 190, 190), qRgb(10, 200, 220)));  // the icon
        QVERIFY(near(pixelOf(f.lastScreen(), 36, 36), qRgb(0x23, 0x28, 0x30))); // with room around it

        // A real cover takes its place...
        QImage red(200, 200, QImage::Format_RGB32);
        red.fill(QColor(210, 30, 30));
        QVERIFY(red.save(f.dir.filePath("red.png")));
        video.artUrl = QUrl::fromLocalFile(f.dir.filePath("red.png")).toString();
        f.media.set(MediaController::State::Playing, video);
        QVERIFY(near(pixelOf(f.lastScreen(), 36, 36), qRgb(210, 30, 30))); // a cover fills the square

        // ...and the icon comes back when the next track has none.
        video.artUrl.clear();
        f.media.set(MediaController::State::Playing, video);
        QVERIFY(near(pixelOf(f.lastScreen(), 190, 190), qRgb(10, 200, 220)));
        QIcon::setThemeName(QString());
    }

    void aVideoGetsItsThumbnailAndKeepsItInTheCache()
    {
        Fixture f;
        QVERIFY(installTestTheme(f.dir.filePath("icons"), QColor(10, 200, 220)));
        QVERIFY(f.controller->SetWallpaper(f.wallpaper()));

        // FFmpeg stands in as a script that writes a purple picture.
        QImage purple(320, 180, QImage::Format_RGB32);
        purple.fill(QColor(140, 40, 160));
        QVERIFY(purple.save(f.dir.filePath("frame.jpg"), "JPEG"));
        QFile script(f.dir.filePath("fake-ffmpeg"));
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(("#!/bin/sh\nfor a in \"$@\"; do last=\"$a\"; done\ncp '" + f.dir.filePath("frame.jpg").toUtf8()
                      + "' \"$last\"\necho run >> '" + f.dir.filePath("runs.log").toUtf8() + "'\n"));
        script.close();
        script.setPermissions(script.permissions() | QFileDevice::ExeOwner);
        VideoThumbnails thumbnails(f.dir.filePath("cache"), f.dir.filePath("fake-ffmpeg"));
        auto controller = std::make_unique<ScreenController>(f.deck.get(), f.wallpaperRenderer.get(),
                                                             WallpaperCatalog(f.dir.filePath("wall")), widgetsDir() + "/WallpaperClock.qml",
                                                             &f.media, f.nowPlayingRenderer.get(), &thumbnails);
        QVERIFY(controller->SetWallpaper(f.wallpaper()));

        QVERIFY(QFile(f.dir.filePath("movie.mp4")).open(QIODevice::WriteOnly));
        NowPlaying video = Fixture::track(5, 100);
        video.url = QUrl::fromLocalFile(f.dir.filePath("movie.mp4")).toString();
        video.playerIcon = "galleon-test-player"; // VLC's icon, until the picture is ready
        f.media.set(MediaController::State::Playing, video);
        QVERIFY(near(pixelOf(f.lastScreen(), 190, 190), qRgb(10, 200, 220)));
        QTRY_VERIFY_WITH_TIMEOUT(near(pixelOf(f.lastScreen(), 36, 36), qRgb(140, 40, 160)), 10000); // the thumbnail fills the square

        // The same video again: from the cache, FFmpeg does not run a second time.
        f.media.set(MediaController::State::Stopped, NowPlaying{});
        f.media.set(MediaController::State::Playing, video);
        QVERIFY(near(pixelOf(f.lastScreen(), 36, 36), qRgb(140, 40, 160)));
        QCOMPARE(QFile(f.dir.filePath("runs.log")).size(), qint64(4)); // "run\n", once
        QIcon::setThemeName(QString());
    }

    void thumbnailsCanBeTurnedOff()
    {
        Fixture f;
        QVERIFY(f.controller->SetWallpaper(f.wallpaper()));
        QFile script(f.dir.filePath("fake-ffmpeg"));
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(("#!/bin/sh\necho run >> '" + f.dir.filePath("runs.log").toUtf8() + "'\n"));
        script.close();
        script.setPermissions(script.permissions() | QFileDevice::ExeOwner);
        VideoThumbnails thumbnails(f.dir.filePath("cache"), f.dir.filePath("fake-ffmpeg"));
        ScreenController controller(f.deck.get(), f.wallpaperRenderer.get(), WallpaperCatalog(f.dir.filePath("wall")),
                                    widgetsDir() + "/WallpaperClock.qml", &f.media, f.nowPlayingRenderer.get(), &thumbnails);
        ScreenConfig config;
        config.videoThumbnails = false;
        controller.setScreenConfig(config);
        QVERIFY(controller.SetWallpaper(f.wallpaper()));
        QVERIFY(QFile(f.dir.filePath("movie.mp4")).open(QIODevice::WriteOnly));
        NowPlaying video = Fixture::track(5, 100);
        video.url = QUrl::fromLocalFile(f.dir.filePath("movie.mp4")).toString();
        f.media.set(MediaController::State::Playing, video);
        QTest::qWait(400);
        QVERIFY(!QFile::exists(f.dir.filePath("runs.log"))); // FFmpeg never started
    }

    void anUnknownPlayerIconLeavesThePlaceholder()
    {
        Fixture f;
        QVERIFY(f.controller->SetWallpaper(f.wallpaper()));
        NowPlaying video = Fixture::track(5, 100);
        video.playerIcon = "no-such-icon-in-any-theme";
        f.media.set(MediaController::State::Playing, video);
        QVERIFY(near(pixelOf(f.lastScreen(), 40, 40), qRgb(0x23, 0x28, 0x30)));
    }

    void aBadCoverFallsBackToThePlaceholder()
    {
        Fixture f;
        QVERIFY(f.controller->SetWallpaper(f.wallpaper()));
        f.media.set(MediaController::State::Playing, Fixture::track(1, 100, "file:///etc/passwd"));
        QVERIFY(near(pixelOf(f.lastScreen(), 40, 40), qRgb(0x23, 0x28, 0x30)));
        f.media.set(MediaController::State::Playing, Fixture::track(1, 100, "http://example.org/a.png")); // not https
        QVERIFY(near(pixelOf(f.lastScreen(), 40, 40), qRgb(0x23, 0x28, 0x30)));
    }

    void remoteCoversCanBeTurnedOff()
    {
        Fixture f;
        QVERIFY(f.controller->SetWallpaper(f.wallpaper()));
        ScreenConfig config;
        config.remoteCovers = false;
        f.controller->setScreenConfig(config);
        // Nothing is downloaded: the placeholder stays (and the test never reaches the network).
        f.media.set(MediaController::State::Playing, Fixture::track(1, 100, "https://covers.example.org/a.png"));
        QVERIFY(near(pixelOf(f.lastScreen(), 40, 40), qRgb(0x23, 0x28, 0x30)));
    }

    void aDeckThatIsNotOpenIsNotWrittenTo()
    {
        Fixture f;
        f.deck->close();
        const auto before = f.transport->writes.size();
        QVERIFY(!f.controller->SetWallpaper(f.wallpaper()));
        f.media.set(MediaController::State::Playing, Fixture::track());
        QCOMPARE(f.transport->writes.size(), before);
    }

    void steppingChoosesTheNextWallpaperAndWrapsAround()
    {
        Fixture const f;
        QImage red(1600, 900, QImage::Format_RGB32);
        red.fill(QColor(200, 30, 30));
        QVERIFY(red.save(f.dir.filePath("wall/style/red.png")));
        ScreenController controller(f.deck.get(), f.wallpaperRenderer.get(), WallpaperCatalog(f.dir.filePath("wall")),
                                    widgetsDir() + "/WallpaperClock.qml");
        QVERIFY(controller.SetWallpaper("style/green.png"));
        controller.stepWallpaper(1);
        QCOMPARE(controller.wallpaper(), QStringLiteral("style/red.png")); // the choice counts at once
        controller.stepWallpaper(1); // wraps around
        QCOMPARE(controller.wallpaper(), QStringLiteral("style/green.png"));
        controller.stepWallpaper(-1);
        QCOMPARE(controller.wallpaper(), QStringLiteral("style/red.png"));
        controller.stepWallpaper(-7); // many steps: still inside the catalog
        QVERIFY(!controller.wallpaper().isEmpty());
        controller.stepWallpaper(0);
    }

    void choosingTheWallpaperShowsItEvenWhileATrackPlays()
    {
        Fixture f;
        f.controller->SetWallpaper(f.wallpaper());
        f.media.set(MediaController::State::Playing, Fixture::track());
        QTRY_VERIFY_WITH_TIMEOUT(f.lastScreen().pixelColor(690, 200) != QColor(30, 160, 60), 3000); // the track screen
        f.controller->setBottomBar(0xff00ff, true);
        QTRY_VERIFY_WITH_TIMEOUT(near(f.lastScreen().pixel(690, 200), qRgb(30, 160, 60)), 3000); // the wallpaper
        QVERIFY(near(f.lastScreen().pixel(360, 382), qRgb(255, 0, 255), 50));                       // the magenta line
        f.controller->setWallpaperHoldMs(700);
        f.controller->setBottomBar(0xffffff, false);
        // The choice is shown first, then the track comes back.
        QTest::qWait(300);
        QVERIFY(near(f.lastScreen().pixel(690, 200), qRgb(30, 160, 60)));
        QTRY_VERIFY_WITH_TIMEOUT(f.lastScreen().pixelColor(690, 200) != QColor(30, 160, 60), 3000);
    }

    void theSavedWallpaperIsShownAtOnceThenTheTrackTakesOverIfOnePlays()
    {
        Fixture f;
        QVERIFY(f.controller->SetWallpaper(f.wallpaper())); // remembered in the settings

        // A new start: nothing drawn yet, then restore() puts the wallpaper on the screen.
        Fixture g;
        g.controller->setWallpaperHoldMs(700);
        QCOMPARE(g.frames(), size_t(0));
        g.controller->restore();
        QCOMPARE(g.controller->wallpaper(), f.wallpaper());
        QVERIFY(g.frames() >= 1);
        QVERIFY(near(g.lastScreen().pixel(690, 200), qRgb(30, 160, 60)));

        // The player answers a moment later: the wallpaper is kept for the hold time, then the track takes over.
        g.media.set(MediaController::State::Playing, Fixture::track());
        QTest::qWait(300);
        QVERIFY(near(g.lastScreen().pixel(690, 200), qRgb(30, 160, 60))); // still the wallpaper
        QTRY_VERIFY_WITH_TIMEOUT(!near(g.lastScreen().pixel(690, 200), qRgb(30, 160, 60)), 3000);
    }

    void aSavedWallpaperThatIsGoneLeavesTheScreenAlone()
    {
        {
            QSettings settings(QStringLiteral("galleoncore"), QStringLiteral("galleond"));
            settings.setValue(QStringLiteral("wallpaper"), QStringLiteral("style/gone.png"));
        }
        Fixture f;
        f.controller->restore();
        QVERIFY(f.controller->wallpaper().isEmpty());
        QCOMPARE(f.frames(), size_t(0));
    }

    void aFastTurnSendsFewPicturesAndEndsOnTheLastWallpaper()
    {
        Fixture const f;
        QImage red(1600, 900, QImage::Format_RGB32);
        red.fill(QColor(200, 30, 30));
        QVERIFY(red.save(f.dir.filePath("wall/style/red.png")));
        ScreenController controller(f.deck.get(), f.wallpaperRenderer.get(), WallpaperCatalog(f.dir.filePath("wall")),
                                    widgetsDir() + "/WallpaperClock.qml");
        QVERIFY(controller.SetWallpaper("style/green.png"));
        const std::size_t before = f.frames();
        controller.stepWallpaper(1); // the first one answers at once
        QVERIFY(f.frames() > before);
        for (int i = 1; i < 9; ++i) // nine steps in a row, as a dial turned fast gives
            controller.stepWallpaper(1);
        QCOMPARE(controller.wallpaper(), QStringLiteral("style/red.png")); // 9 steps over 2 wallpapers: odd, so the other one
        QVERIFY(f.frames() > before); // the first step is drawn at once, without waiting
        QTest::qWait(400);
        QVERIFY(f.frames() <= before + 3); // the rest of the burst is gathered
        QVERIFY(near(f.lastScreen().pixel(690, 200), qRgb(200, 30, 30))); // and it ends on the last wallpaper
        // The choice is saved when it is made, not at each step.
        controller.setBottomBar(0xff00ff, true);
        controller.setBottomBar(0xffffff, false);
        QSettings settings(QStringLiteral("galleoncore"), QStringLiteral("galleond"));
        settings.sync();
        QCOMPARE(settings.value("wallpaper").toString(), QStringLiteral("style/red.png"));
    }

    void theCountdownIsShownWhileTheChoiceIsBeingValidated()
    {
        Fixture f;
        QVERIFY(f.controller->SetWallpaper(f.wallpaper()));
        f.controller->setBottomBar(0xff00ff, true); // choosing
        const auto magenta = [&f] {
            const QImage image = f.lastScreen();
            int count = 0;
            for (int y = 264; y < 364; ++y) {
                for (int x = 600; x < 700; ++x)
                    count += near(image.pixel(x, y), qRgb(255, 0, 255), 50) ? 1 : 0;
            }
            return count;
        };
        QSignalSpy const shown(f.controller.get(), &ScreenController::choosingLabelShown);
        f.controller->setCountdown(3);
        QTRY_VERIFY_WITH_TIMEOUT(magenta() > 150, 2000);
        QVERIFY(shown.count() >= 1); // the screen with the label went to the module
        f.controller->setCountdown(2);
        QTest::qWait(200);
        QVERIFY(magenta() > 150);
        f.controller->setCountdown(0);
        QTRY_COMPARE_WITH_TIMEOUT(magenta(), 0, 2000);
    }

    void whileChoosingTheScreenShowsTheNameAndPlaceOfTheWallpaper()
    {
        Fixture f;
        QImage red(1600, 900, QImage::Format_RGB32);
        red.fill(QColor(200, 30, 30));
        QVERIFY(red.save(f.dir.filePath("wall/style/red.png")));
        ScreenController controller(f.deck.get(), f.wallpaperRenderer.get(), WallpaperCatalog(f.dir.filePath("wall")),
                                    widgetsDir() + "/WallpaperClock.qml");
        QVERIFY(controller.SetWallpaper("style/green.png"));
        const auto magentaBand = [&f] {
            const QImage image = f.lastScreen();
            int count = 0;
            for (int y = 384 - 82; y < 384 - 18; ++y) {
                for (int x = 360 - 150; x < 360 + 150; ++x)
                    count += near(image.pixel(x, y), qRgb(255, 0, 255), 50) ? 1 : 0;
            }
            return count;
        };
        QCOMPARE(magentaBand(), 0);
        controller.setBottomBar(0xff00ff, true); // choosing: the band with the name appears
        QTRY_VERIFY_WITH_TIMEOUT(magentaBand() > 150, 2000);
        controller.stepWallpaper(1);
        QTest::qWait(300);
        QVERIFY(magentaBand() > 150); // still there, for the next wallpaper
        controller.setBottomBar(0xffffff, false);
        QTRY_COMPARE_WITH_TIMEOUT(magentaBand(), 0, 2000);
    }
};

QTEST_MAIN(TestScreenController)
#include "test_screencontroller.moc"
