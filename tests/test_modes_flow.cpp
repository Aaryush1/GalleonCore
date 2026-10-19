// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// The dials, the themes and the wallpaper choice through the same code as
// galleond (connectPagesToScreen), with the real screen widgets and a fake module.

#include "FakeTransport.h"

#include <PagesController.h>
#include <ScreenController.h>
#include <ScreenRenderer.h>
#include <Wiring.h>
#include <galleon/StreamDeck.h>

#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace galleond;
using galleon::StreamDeck;

namespace {

const bool IsolatedSettings = [] {
    qputenv("XDG_CONFIG_HOME", (QDir::tempPath() + QStringLiteral("/galleoncore-test-config")).toUtf8());
    return true;
}();

const QString WidgetsDir = QStringLiteral(GALLEON_SOURCE_WIDGETS);

class FakeMedia : public MediaController
{
public:
    [[nodiscard]] State state() const override { return m_state; }
    [[nodiscard]] NowPlaying nowPlaying() const override { return {}; }
    void playPause() override {}
    void next() override {}
    void previous() override {}
    void stop() override {}
    void changeVolume(int percent) override { volume += percent; }
    void seek(int) override {}
    void setState(State state)
    {
        m_state = state;
        Q_EMIT stateChanged();
    }
    int volume = 0;

private:
    State m_state = State::Stopped;
};

bool near(QRgb a, QRgb b, int tolerance = 30)
{
    return qAbs(qRed(a) - qRed(b)) <= tolerance && qAbs(qGreen(a) - qGreen(b)) <= tolerance
           && qAbs(qBlue(a) - qBlue(b)) <= tolerance;
}

QJsonObject page()
{
    return {{"keys", QJsonArray{}}};
}

PagesConfig userLikeConfig()
{
    // The same shape as the user's file: Control tied to a playing media, the keypad to none.
    const QJsonObject root{
        {"version", 1},
        {"home", "main"},
        {"pages", QJsonObject{{"main", page()}, {"son", page()}, {"numpad", page()}}},
        {"themes", QJsonArray{QJsonObject{{"name", "Control"}, {"page", "main"}, {"color", "#ffd600"}, {"when", "media"}},
                              QJsonObject{{"name", "Sound"}, {"page", "son"}, {"color", "#e53935"}},
                              QJsonObject{{"name", "Keypad"}, {"page", "numpad"}, {"color", "#ffffff"}, {"when", "no-media"}}}},
        {"dials", QJsonObject{{"0", QJsonObject{{"turn", QJsonObject{{"type", "media"}, {"command", "volume"}, {"step", 2}}},
                                                {"press", QJsonObject{{"type", "theme"}}}}},
                              {"1", QJsonObject{{"press", QJsonObject{{"type", "wallpaper-mode"}}}}}}}};
    const auto parsed = parsePagesConfig(QJsonDocument(root).toJson());
    if (!parsed.config)
        qFatal("test config: %s", qPrintable(parsed.errors.join(' ')));
    return *parsed.config;
}

struct Fixture
{
    QTemporaryDir dir;
    FakeTransport *transport = nullptr;
    std::unique_ptr<StreamDeck> deck;
    FakeMedia media;
    std::unique_ptr<ScreenRenderer> wallpaperRenderer;
    std::unique_ptr<ScreenRenderer> nowPlayingRenderer;
    std::unique_ptr<ScreenController> screen;
    std::unique_ptr<PagesController> pages;

    Fixture()
    {
        QDir().mkpath(dir.filePath("wall/style"));
        QImage green(1600, 900, QImage::Format_RGB32);
        green.fill(QColor(30, 160, 60));
        QVERIFY(green.save(dir.filePath("wall/style/a-green.png")));
        QImage blue(1600, 900, QImage::Format_RGB32);
        blue.fill(QColor(40, 60, 200));
        QVERIFY(blue.save(dir.filePath("wall/style/b-blue.png")));

        auto fake = std::make_unique<FakeTransport>();
        transport = fake.get();
        deck = std::make_unique<StreamDeck>(std::move(fake));
        QVERIFY(deck->open());
        wallpaperRenderer = std::make_unique<ScreenRenderer>(WidgetsDir + "/WallpaperClock.qml");
        nowPlayingRenderer = std::make_unique<ScreenRenderer>(WidgetsDir + "/NowPlaying.qml");
        screen = std::make_unique<ScreenController>(deck.get(), wallpaperRenderer.get(), WallpaperCatalog(dir.filePath("wall")),
                                                    WidgetsDir + "/WallpaperClock.qml", &media, nowPlayingRenderer.get());
        pages = std::make_unique<PagesController>(deck.get(), &media, TileRenderer());
        connectPagesToScreen(*pages, *screen);
        pages->setConfig(userLikeConfig());
        QVERIFY(screen->SetWallpaper("style/a-green.png"));
    }

    void dial(int index, bool pressed)
    {
        std::vector<std::uint8_t> report = {0x01, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00};
        report[5 + static_cast<std::size_t>(index)] = pressed ? 1 : 0;
        transport->pendingReads.push_back(report);
        QTRY_VERIFY_WITH_TIMEOUT(transport->pendingReads.empty(), 2000);
        QTest::qWait(30);
    }
    void click(int index)
    {
        dial(index, true);
        dial(index, false);
    }
    void turn(int index, int steps)
    {
        std::vector<std::uint8_t> report = {0x01, 0x03, 0x03, 0x00, 0x01, 0x00, 0x00};
        report[5 + static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(steps);
        transport->pendingReads.push_back(report);
        QTRY_VERIFY_WITH_TIMEOUT(transport->pendingReads.empty(), 2000);
        QTest::qWait(30);
    }

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
            jpeg.prepend(reinterpret_cast<const char *>(r.data()) + 16, r[13] | (r[14] << 8));
            if ((r[11] | (r[12] << 8)) == 0)
                break;
        }
        QImage image;
        image.loadFromData(jpeg, "JPEG");
        return image;
    }
    [[nodiscard]] QString theme() const { return pages->currentTheme() ? pages->currentTheme()->name : QString(); }
};

} // namespace

class TestModesFlow : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void withoutMediaTheKeypadIsShownAndOneClickShowsTheBandAndTheDialChangesTheTheme()
    {
        Fixture f; // no media
        QCOMPARE(f.theme(), QStringLiteral("Keypad"));
        const QRgb top = f.lastScreen().pixel(10, 2);
        QVERIFY(near(top, qRgb(255, 255, 255))); // the white theme line

        f.click(0); // ONE click: the theme choice, with its band
        QVERIFY(f.pages->themeMode());
        const QImage withBand = f.lastScreen();
        // The bottom line, in the theme's colour (white), is drawn at once: the visible sign of the mode.
        QVERIFY(near(withBand.pixel(360, 384 - 2), qRgb(255, 255, 255)));

        f.turn(0, 1); // one step: the next theme (wraps to the first)
        QCOMPARE(f.theme(), QStringLiteral("Control"));
        QTRY_VERIFY_WITH_TIMEOUT(near(f.lastScreen().pixel(10, 2), qRgb(0xff, 0xd6, 0x00)), 3000); // yellow line
        f.turn(0, 1);
        QCOMPARE(f.theme(), QStringLiteral("Sound"));
        QTRY_VERIFY_WITH_TIMEOUT(near(f.lastScreen().pixel(10, 2), qRgb(0xe5, 0x39, 0x35)), 3000);
        QCOMPARE(f.media.volume, 0); // the volume was left alone

        f.click(0); // leaving: the dial is the volume again
        QVERIFY(!f.pages->themeMode());
        f.turn(0, 1);
        QCOMPARE(f.theme(), QStringLiteral("Sound"));
        QCOMPARE(f.media.volume, 2);
    }

    void aPlayingMediaGivesTheControlTheme()
    {
        Fixture f;
        f.media.setState(MediaController::State::Playing);
        QCOMPARE(f.theme(), QStringLiteral("Control"));
        f.click(0);
        f.turn(0, 1);
        QCOMPARE(f.theme(), QStringLiteral("Sound"));
        f.media.setState(MediaController::State::Stopped); // the user is choosing: no interference
        QCOMPARE(f.theme(), QStringLiteral("Sound"));
    }

    void theChosenWallpaperIsShownWhenTheModeIsLeft()
    {
        Fixture f;
        QVERIFY(near(f.lastScreen().pixel(700, 200), qRgb(30, 160, 60)));
        f.click(1); // choose the wallpaper
        QVERIFY(f.pages->wallpaperMode());
        f.turn(1, 1); // the next one: the blue
        QTRY_VERIFY_WITH_TIMEOUT(near(f.lastScreen().pixel(700, 200), qRgb(40, 60, 200)), 3000);
        f.click(1); // validate
        QVERIFY(!f.pages->wallpaperMode());
        QTRY_VERIFY_WITH_TIMEOUT(near(f.lastScreen().pixel(700, 200), qRgb(40, 60, 200)), 3000);
        QCOMPARE(f.screen->wallpaper(), QStringLiteral("style/b-blue.png"));
        QTest::qWait(400);
        QVERIFY(near(f.lastScreen().pixel(700, 200), qRgb(40, 60, 200))); // and it stays
    }

    void turningSeveralStepsThenLeavingQuicklyKeepsTheLastWallpaper()
    {
        Fixture f;
        f.click(1);
        f.turn(1, 1); // blue
        f.click(1);   // leave at once, before the picture of the step is made
        QTRY_VERIFY_WITH_TIMEOUT(near(f.lastScreen().pixel(700, 200), qRgb(40, 60, 200)), 3000);
        QCOMPARE(f.screen->wallpaper(), QStringLiteral("style/b-blue.png"));
    }
};

QTEST_MAIN(TestModesFlow)
#include "test_modes_flow.moc"
