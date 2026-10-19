// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FakeTransport.h"

#include <ScreenController.h>
#include <ScreenRenderer.h>
#include <galleon/StreamDeck.h>

#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace galleond;
using galleon::StreamDeck;

namespace {

const QString WidgetsDir = QStringLiteral(GALLEON_SOURCE_WIDGETS);

class FakeMedia : public MediaController
{
public:
    [[nodiscard]] State state() const override { return m_state; }
    [[nodiscard]] NowPlaying nowPlaying() const override { return m_track; }
    void playPause() override {}
    void next() override {}
    void previous() override {}
    void stop() override {}
    void changeVolume(int) override {}
    void seek(int) override {}

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
        wallpaperRenderer = std::make_unique<ScreenRenderer>(WidgetsDir + "/WallpaperClock.qml");
        nowPlayingRenderer = std::make_unique<ScreenRenderer>(WidgetsDir + "/NowPlaying.qml");
        controller = std::make_unique<ScreenController>(deck.get(), wallpaperRenderer.get(),
                                                        WallpaperCatalog(dir.filePath("wall")), WidgetsDir + "/WallpaperClock.qml",
                                                        &media, nowPlayingRenderer.get());
    }

    QString wallpaper() const { return QStringLiteral("style/green.png"); }

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
            jpeg.prepend(reinterpret_cast<const char *>(r.data()) + 16, length);
            if ((r[11] | (r[12] << 8)) == 0)
                break;
        }
        QImage image;
        image.loadFromData(jpeg, "JPEG");
        return image;
    }

    [[nodiscard]] std::size_t frames() const
    {
        std::size_t count = 0;
        for (const auto &r : transport->writes) {
            if (r.size() > 10 && r[0] == 0x02 && r[1] == 0x0c && r[10] == 1)
                ++count;
        }
        return count;
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
const QRgb Wallpaper = qRgb(30, 160, 60);
const QRgb TrackScreen = qRgb(0x14, 0x17, 0x1c);

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
};

QTEST_MAIN(TestScreenController)
#include "test_screencontroller.moc"
