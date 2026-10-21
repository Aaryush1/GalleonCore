// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Bytes.h"
#include "Required.h"
#include "FakeTransport.h"

#include <PagesController.h>
#include <SteamLibrary.h>
#include <galleon/StreamDeck.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <ranges>
#include <utility>

using namespace galleond;
using galleon::StreamDeck;

namespace {

class FakeMedia : public MediaController
{
public:
    [[nodiscard]] State state() const override { return m_state; }
    [[nodiscard]] NowPlaying nowPlaying() const override { return {}; }
    void playPause() override { ++playPauseCount; }
    void next() override { ++nextCount; }
    void previous() override { ++previousCount; }
    void stop() override { ++stopCount; }
    void changeVolume(int percent) override { volumeTotal += percent; }
    void seek(int seconds) override { seekTotal += seconds; }
    void toggleShuffle() override { ++shuffleCount; }
    void cycleRepeat() override { ++repeatCount; }
    void toggleMute() override { ++muteCount; }
    void raisePlayer() override { ++raiseCount; }
    void toggleFullscreen() override { ++fullscreenCount; }
    void restartTrack() override { ++restartCount; }
    [[nodiscard]] bool shuffleOn() const override { return shuffle; }
    [[nodiscard]] Loop loopMode() const override { return loop; }
    void setExtras(bool shuffleState, Loop loopState)
    {
        shuffle = shuffleState;
        loop = loopState;
        Q_EMIT stateChanged();
    }
    bool shuffle = false;
    Loop loop = Loop::None;

    void setState(State state)
    {
        m_state = state;
        Q_EMIT stateChanged();
    }

    int playPauseCount = 0;
    int nextCount = 0;
    int previousCount = 0;
    int stopCount = 0;
    int volumeTotal = 0;
    int seekTotal = 0;
    int shuffleCount = 0;
    int repeatCount = 0;
    int muteCount = 0;
    int raiseCount = 0;
    int fullscreenCount = 0;
    int restartCount = 0;

private:
    State m_state = State::Stopped;
};

class FakeInjector : public KeyInjector
{
public:
    void prepare() override { ++prepared; }
    bool key(int code, bool down) override
    {
        events.emplace_back(code, down);
        return true;
    }
    int prepared = 0;
    std::vector<std::pair<int, bool>> events;
};

struct Fixture
{
    FakeTransport *transport;
    std::unique_ptr<StreamDeck> deck;
    FakeMedia media;
    std::unique_ptr<PagesController> controller;

    explicit Fixture(bool openDeck = true, const QString &iconFolder = {})
    {
        auto fake = std::make_unique<FakeTransport>();
        transport = fake.get();
        deck = std::make_unique<StreamDeck>(std::move(fake));
        if (openDeck)
            QVERIFY(deck->open());
        controller = std::make_unique<PagesController>(deck.get(), &media, TileRenderer(iconFolder));
    }

    // The keys that received a picture since `from` (an index into the writes).
    [[nodiscard]] QSet<int> keysDrawnSince(std::size_t from) const
    {
        QSet<int> keys;
        for (std::size_t i = from; i < transport->writes.size(); ++i) {
            const auto &report = transport->writes[i];
            if (report.size() > 3 && report[0] == 0x02 && report[1] == 0x07)
                keys.insert(report[2]);
        }
        return keys;
    }

    void pressKey(int key, bool pressed)
    {
        std::vector<std::uint8_t> report = {0x01, 0x00, 0x0c, 0x00};
        report.resize(4 + galleon::protocol::KeyCount, 0);
        report[4 + static_cast<std::size_t>(key)] = pressed ? 1 : 0;
        transport->pendingReads.push_back(report);
        QTRY_VERIFY_WITH_TIMEOUT(transport->pendingReads.empty(), 2000);
        QTest::qWait(30); // let the signal reach the controller
    }

    void turnDial(int dial, int signedSteps)
    {
        QTest::qWait(350); // a step right after a press is the click's release: let it pass
        std::vector<std::uint8_t> report = {0x01, 0x03, 0x03, 0x00, 0x01, 0x00, 0x00};
        report[5 + static_cast<std::size_t>(dial)] = static_cast<std::uint8_t>(signedSteps);
        transport->pendingReads.push_back(report);
        QTRY_VERIFY_WITH_TIMEOUT(transport->pendingReads.empty(), 2000);
        QTest::qWait(30);
    }

    void pressDial(int dial, bool pressed)
    {
        if (pressed)
            QTest::qWait(450); // a step just before a press is the click's own wobble: let it settle
        std::vector<std::uint8_t> report = {0x01, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00};
        report[5 + static_cast<std::size_t>(dial)] = pressed ? 1 : 0;
        transport->pendingReads.push_back(report);
        QTRY_VERIFY_WITH_TIMEOUT(transport->pendingReads.empty(), 2000);
        QTest::qWait(30);
    }
};

QJsonObject keyJson(int key, const QString &label, const QJsonObject &action, const QJsonValue &icon = QJsonValue())
{
    QJsonObject object{{"key", key}, {"label", label}, {"action", action}};
    if (!icon.isUndefined() && !icon.isNull())
        object.insert("icon", icon);
    return object;
}

QJsonObject mediaAction(const QString &command)
{
    return {{"type", "media"}, {"command", command}};
}

QByteArray pagesFile(const QJsonObject &pages, const QString &home = QStringLiteral("main"))
{
    return QJsonDocument(QJsonObject{{"version", 1}, {"home", home}, {"pages", pages}}).toJson();
}

QJsonObject page(const QJsonArray &keys)
{
    return {{"keys", keys}};
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(bytes);
    return true;
}

// The XDG data folders point at a throw-away folder for one test.
struct DataFolders
{
    QTemporaryDir dir;
    QByteArray oldHome = qgetenv("XDG_DATA_HOME");
    QByteArray oldDirs = qgetenv("XDG_DATA_DIRS");
    DataFolders()
    {
        qputenv("XDG_DATA_HOME", dir.filePath("home").toUtf8());
        qputenv("XDG_DATA_DIRS", dir.filePath("system").toUtf8());
    }
    ~DataFolders()
    {
        qputenv("XDG_DATA_HOME", oldHome);
        qputenv("XDG_DATA_DIRS", oldDirs);
    }
    QString addApp(const QString &file, const QString &name, const QString &icon = {})
    {
        const QString path = dir.filePath("system/applications/" + file);
        const QByteArray text = "[Desktop Entry]\nType=Application\nName=" + name.toUtf8() + "\nExec=/usr/bin/true\n"
                                + (icon.isEmpty() ? QByteArray() : "Icon=" + icon.toUtf8() + "\n");
        if (!writeFile(path, text))
            qFatal("cannot write the desktop file");
        return path;
    }
};

// The picture last sent to a key, as the bytes of its JPEG.
QByteArray lastKeyJpeg(const FakeTransport &transport, int key)
{
    const auto &writes = transport.writes;
    qsizetype last = -1;
    for (qsizetype i = static_cast<qsizetype>(writes.size()) - 1; i >= 0; --i) {
        const auto &r = writes[static_cast<std::size_t>(i)];
        if (r.size() > 3 && r[0] == 0x02 && r[1] == 0x07 && std::cmp_equal(r[2], key) && r[3] == 1) {
            last = i;
            break;
        }
    }
    QByteArray jpeg;
    for (qsizetype i = last; i >= 0; --i) {
        const auto &r = writes[static_cast<std::size_t>(i)];
        if (r.size() < 8 || r[1] != 0x07 || std::cmp_not_equal(r[2], key))
            break;
        jpeg.prepend(bytesOf(r, 8, static_cast<std::size_t>(r[4] | (r[5] << 8))));
        if ((r[6] | (r[7] << 8)) == 0)
            break;
    }
    return jpeg;
}

bool saveColour(const QString &path, QColor colour)
{
    QImage image(64, 64, QImage::Format_RGB32);
    image.fill(colour);
    return image.save(path);
}

} // namespace

class TestPagesController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void allTwelveKeysAreDrawnAtStart()
    {
        Fixture f;
        const QSet<int> drawn = f.keysDrawnSince(0);
        QCOMPARE(drawn.size(), 12);
        QCOMPARE(f.controller->currentPage(), QStringLiteral("main"));
    }

    void nothingChangedSendsNothing()
    {
        Fixture f;
        const auto before = f.transport->writes.size();
        f.controller->redraw();
        f.controller->redraw();
        QCOMPARE(f.transport->writes.size(), before);
        f.controller->redraw(true); // forced: everything again
        QCOMPARE(f.keysDrawnSince(before).size(), 12);
    }

    void aDeckPluggedLaterGetsItsKeys()
    {
        Fixture f(false);
        QVERIFY(f.transport->writes.empty()); // nothing can be sent to a closed deck
        QVERIFY(f.deck->open());
        f.controller->redraw(true);
        QCOMPARE(f.keysDrawnSince(0).size(), 12);
    }

    void aKeyPressRunsTheActionAndLightsTheKey()
    {
        Fixture f;
        const auto before = f.transport->writes.size();
        f.pressKey(1, true); // the default pages: key 1 = play/pause
        QCOMPARE(f.media.playPauseCount, 1);
        QCOMPARE(f.keysDrawnSince(before), QSet<int>{1}); // only that key is redrawn
        const auto afterPress = f.transport->writes.size();

        f.pressKey(1, false);
        QCOMPARE(f.keysDrawnSince(afterPress), QSet<int>{1}); // back to its normal picture
        QCOMPARE(f.media.playPauseCount, 1);                  // the action runs on press only
    }

    void everyDefaultKeyDoesItsJob()
    {
        Fixture f;
        f.pressKey(0, true);
        f.pressKey(0, false);
        f.pressKey(2, true);
        f.pressKey(2, false);
        f.pressKey(11, true);
        f.pressKey(11, false);
        f.pressKey(3, true);
        f.pressKey(3, false);
        f.pressKey(5, true);
        f.pressKey(5, false);
        f.pressKey(6, true);
        f.pressKey(6, false);
        f.pressKey(8, true);
        f.pressKey(8, false);
        QCOMPARE(f.media.previousCount, 1);
        QCOMPARE(f.media.nextCount, 1);
        QCOMPARE(f.media.stopCount, 1);
        QCOMPARE(f.media.volumeTotal, -5 + 5);
        QCOMPARE(f.media.seekTotal, -10 + 10);
    }

    void anEmptyKeyDoesNothing()
    {
        Fixture f;
        const auto parsed = parsePagesConfig(defaultPagesJson());
        QVERIFY(parsed.config.has_value());
        f.controller->setConfig(required(parsed.config)); // The minimal portable fixture has an empty key 4.
        const auto before = f.transport->writes.size();
        f.pressKey(4, true);
        f.pressKey(4, false);
        QCOMPARE(f.transport->writes.size(), before);
        QCOMPARE(f.media.playPauseCount + f.media.nextCount + f.media.stopCount, 0);
    }

    void dialsTurnAndPress()
    {
        Fixture f;
        const auto parsed = parsePagesConfig(defaultPagesJson());
        QVERIFY(parsed.config.has_value());
        f.controller->setConfig(required(parsed.config)); // Check media dial actions independently of first-run themes.
        f.turnDial(0, 3);  // volume, +2 per click
        QCOMPARE(f.media.volumeTotal, 6);
        f.turnDial(0, -2);
        QCOMPARE(f.media.volumeTotal, 2);
        f.turnDial(1, 2);  // seek, +5 s per click
        QCOMPARE(f.media.seekTotal, 10);
        f.turnDial(1, -1);
        QCOMPARE(f.media.seekTotal, 5);

        f.pressDial(0, true);
        QCOMPARE(f.media.playPauseCount, 1);
        f.pressDial(0, false);
        f.pressDial(1, true);
        QCOMPARE(f.media.nextCount, 1);
        f.pressDial(1, false);
        QCOMPARE(f.media.playPauseCount, 1); // a release does nothing
    }

    void aStateChangeRedrawsOnlyTheKeysThatDependOnIt()
    {
        QTemporaryDir const dir;
        QVERIFY(saveColour(dir.filePath("play.png"), Qt::green));
        QVERIFY(saveColour(dir.filePath("pause.png"), Qt::blue));
        const QJsonObject states{{"playing", "pause.png"}, {"paused", "play.png"}, {"stopped", "play.png"}};
        const QString path = dir.filePath("pages.json");
        QVERIFY(writeFile(path, pagesFile({{"main", page({keyJson(1, "", mediaAction("play-pause"), states),
                                                          keyJson(2, "Next", mediaAction("next"), "next.png")})}})));

        Fixture f(true, dir.path());
        f.controller->loadFile(path);
        const auto before = f.transport->writes.size();

        f.media.setState(MediaController::State::Playing);
        QCOMPARE(f.keysDrawnSince(before), QSet<int>{1}); // key 2 does not depend on the state

        const auto again = f.transport->writes.size();
        f.media.setState(MediaController::State::Playing); // same state: same picture
        QCOMPARE(f.transport->writes.size(), again);
    }

    void pagesOpenAndCloseWithBack()
    {
        QTemporaryDir const dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, pagesFile({{"main", page({keyJson(0, "More", {{"type", "page"}, {"page", "sub"}})})},
                                           {"sub", page({keyJson(0, "Back", {{"type", "back"}}), keyJson(1, "Stop", mediaAction("stop"))})}})));
        Fixture f;
        f.controller->loadFile(file);
        QCOMPARE(f.controller->pageStack(), QStringList{"main"});

        auto before = f.transport->writes.size();
        f.pressKey(0, true);
        f.pressKey(0, false);
        QCOMPARE(f.controller->pageStack(), (QStringList{"main", "sub"}));
        QVERIFY(f.keysDrawnSince(before).contains(1)); // the sub-page's key 1 appeared

        f.pressKey(1, true);
        f.pressKey(1, false);
        QCOMPARE(f.media.stopCount, 1); // key 1 now belongs to the sub-page

        f.pressKey(0, true); // Back
        f.pressKey(0, false);
        QCOMPARE(f.controller->pageStack(), QStringList{"main"});
        f.pressKey(1, true); // main has no key 1 here: nothing happens
        f.pressKey(1, false);
        QCOMPARE(f.media.stopCount, 1);
    }

    void backOnTheHomePageStays()
    {
        QTemporaryDir const dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, pagesFile({{"main", page({keyJson(0, "Back", {{"type", "back"}})})}})));
        Fixture f;
        f.controller->loadFile(file);
        f.pressKey(0, true);
        f.pressKey(0, false);
        QCOMPARE(f.controller->pageStack(), QStringList{"main"});
    }

    void pageDepthIsBounded()
    {
        QTemporaryDir const dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, pagesFile({{"main", page({keyJson(0, "Loop", {{"type", "page"}, {"page", "main"}})})}})));
        Fixture f;
        f.controller->loadFile(file);
        for (int i = 0; i < MaxPageDepth + 5; ++i) {
            f.pressKey(0, true);
            f.pressKey(0, false);
        }
        QCOMPARE(f.controller->pageStack().size(), MaxPageDepth);
    }

    void aFileWithErrorsKeepsTheCurrentPages()
    {
        QTemporaryDir const dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, pagesFile({{"main", page({keyJson(0, "Good", mediaAction("next"))})}})));
        Fixture f;
        f.controller->loadFile(file);
        f.pressKey(0, true);
        f.pressKey(0, false);
        QCOMPARE(f.media.nextCount, 1);

        QVERIFY(writeFile(file, pagesFile({{"main", page({keyJson(0, "Bad", mediaAction("explode"))})}})));
        f.controller->loadFile(file); // refused as a whole
        f.pressKey(0, true);
        f.pressKey(0, false);
        QCOMPARE(f.media.nextCount, 2); // still the previous configuration
        QCOMPARE(f.media.stopCount, 0);
    }

    void aMissingFileUsesTheBuiltInPages()
    {
        QTemporaryDir const dir;
        Fixture f;
        f.controller->loadFile(dir.filePath("absent.json"));
        QCOMPARE(f.controller->currentPage(), QStringLiteral("main"));
        f.pressKey(2, true);
        f.pressKey(2, false);
        QCOMPARE(f.media.nextCount, 1);
    }

    void anApplicationKeyStartsItsApplication()
    {
        DataFolders data;
        const QString desktop = data.addApp("tool.desktop", "The Tool", "tool-icon");
        const QString file = data.dir.filePath("pages.json");
        QVERIFY(writeFile(file, pagesFile({{"main", page({QJsonObject{{"key", 4}, {"action", QJsonObject{{"type", "app"}, {"desktop", "tool.desktop"}}}}})}})));
        Fixture f;
        QStringList launched;
        f.controller->setAppLauncher([&launched](const DesktopApp &app) {
            launched << app.file;
            return true;
        });
        f.controller->loadFile(file);

        f.pressKey(4, true);
        QCOMPARE(launched, QStringList{desktop});
        f.pressKey(4, false);
        QCOMPARE(launched.size(), 1); // a release starts nothing
    }

    void anUnknownApplicationStartsNothing()
    {
        DataFolders const data;
        const QString file = data.dir.filePath("pages.json");
        QVERIFY(writeFile(file, pagesFile({{"main", page({QJsonObject{{"key", 4}, {"label", "Ghost"}, {"action", QJsonObject{{"type", "app"}, {"desktop", "absent.desktop"}}}}})}})));
        Fixture f;
        int launched = 0;
        f.controller->setAppLauncher([&launched](const DesktopApp &) {
            ++launched;
            return true;
        });
        f.controller->loadFile(file);
        f.pressKey(4, true);
        f.pressKey(4, false);
        QCOMPARE(launched, 0);
    }

    void anApplicationKeyTakesTheLabelAndIconOfTheDesktopFile()
    {
        DataFolders data;
        data.addApp("tool.desktop", "The Tool", "tool-icon");
        const QString applicationFile = data.dir.filePath("app.json");
        const QString explicitFile = data.dir.filePath("explicit.json");
        QVERIFY(writeFile(applicationFile, pagesFile({{"main", page({QJsonObject{{"key", 4}, {"action", QJsonObject{{"type", "app"}, {"desktop", "tool.desktop"}}}}})}})));
        // The same key written out by hand: the application's name and icon.
        QVERIFY(writeFile(explicitFile, pagesFile({{"main", page({QJsonObject{{"key", 4}, {"label", "The Tool"}, {"icon", "tool-icon"},
                                                                          {"action", QJsonObject{{"type", "app"}, {"desktop", "tool.desktop"}}}}})}})));
        Fixture f;
        f.controller->loadFile(applicationFile);
        const QByteArray byDefault = lastKeyJpeg(*f.transport, 4);
        QVERIFY(!byDefault.isEmpty());
        f.controller->loadFile(explicitFile);
        QCOMPARE(lastKeyJpeg(*f.transport, 4), byDefault);

        // And a label given by the user wins over the application's name.
        QVERIFY(writeFile(explicitFile, pagesFile({{"main", page({QJsonObject{{"key", 4}, {"label", "Mine"}, {"icon", "tool-icon"},
                                                                          {"action", QJsonObject{{"type", "app"}, {"desktop", "tool.desktop"}}}}})}})));
        f.controller->loadFile(explicitFile);
        QVERIFY(lastKeyJpeg(*f.transport, 4) != byDefault);
    }

    void aDoublePressStartsTheApplicationOnce()
    {
        DataFolders data;
        data.addApp("tool.desktop", "The Tool");
        const QString file = data.dir.filePath("pages.json");
        QVERIFY(writeFile(file, pagesFile({{"main", page({QJsonObject{{"key", 4}, {"action", QJsonObject{{"type", "app"}, {"desktop", "tool.desktop"}}}}})}})));
        Fixture f;
        int launched = 0;
        f.controller->setAppLauncher([&launched](const DesktopApp &) {
            ++launched;
            return true;
        });
        f.controller->loadFile(file);
        f.pressKey(4, true);
        f.pressKey(4, false);
        f.pressKey(4, true); // straight away: ignored
        f.pressKey(4, false);
        QCOMPARE(launched, 1);
        QTest::qWait(800);
        f.pressKey(4, true);
        f.pressKey(4, false);
        QCOMPARE(launched, 2);
    }

    void aPagesFileOthersCanWriteDisablesApplications()
    {
        DataFolders data;
        data.addApp("tool.desktop", "The Tool");
        const QString file = data.dir.filePath("pages.json");
        QVERIFY(writeFile(file, pagesFile({{"main", page({QJsonObject{{"key", 4}, {"action", QJsonObject{{"type", "app"}, {"desktop", "tool.desktop"}}}}})}})));
        QFile pages(file);
        QVERIFY(pages.setPermissions(pages.permissions() | QFileDevice::WriteOther));

        Fixture f;
        int launched = 0;
        f.controller->setAppLauncher([&launched](const DesktopApp &) {
            ++launched;
            return true;
        });
        f.controller->loadFile(file);
        f.pressKey(4, true);
        f.pressKey(4, false);
        QCOMPARE(launched, 0); // writable by others: not trusted to start programs

        QVERIFY(pages.setPermissions(pages.permissions() & ~QFileDevice::WriteOther));
        f.controller->loadFile(file);
        QTest::qWait(800);
        f.pressKey(4, true);
        f.pressKey(4, false);
        QCOMPARE(launched, 1); // trusted again
    }

    void aChangeOnDiskIsReloaded()
    {
        QTemporaryDir const dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, pagesFile({{"main", page({keyJson(0, "A", mediaAction("next"))})}})));
        Fixture f;
        f.controller->loadFile(file);

        QVERIFY(writeFile(file, pagesFile({{"main", page({keyJson(0, "B", mediaAction("stop"))})}})));
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                f.pressKey(0, true);
                f.pressKey(0, false);
                return f.media.stopCount > 0;
            }(),
            5000);

        // Replaced the way editors do: a new file renamed over the old one.
        const QString temporary = dir.filePath("pages.json.new");
        QVERIFY(writeFile(temporary, pagesFile({{"main", page({keyJson(0, "C", mediaAction("previous"))})}})));
        QVERIFY(QFile::remove(file));
        QVERIFY(QFile::rename(temporary, file));
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                f.pressKey(0, true);
                f.pressKey(0, false);
                return f.media.previousCount > 0;
            }(),
            5000);
    }

    void aDialPressStepsThroughTheThemes()
    {
        QJsonObject const config{
            {"version", 1},
            {"home", "main"},
            {"pages", QJsonObject{{"main", page({keyJson(0, "A", mediaAction("previous"))})},
                                  {"sound", page({keyJson(0, "B", mediaAction("next"))})}}},
            {"themes", QJsonArray{QJsonObject{{"name", "Control"}, {"page", "main"}, {"color", "#ffffff"}},
                                  QJsonObject{{"name", "Sound"}, {"page", "sound"}, {"color", "#e53935"}}}},
            {"dials", QJsonObject{{"0", QJsonObject{{"turn", QJsonObject{{"type", "media"}, {"command", "volume"}, {"step", 2}}},
                                                    {"press", QJsonObject{{"type", "theme"}}}}}}}};
        const auto parsed = parsePagesConfig(QJsonDocument(config).toJson());
        QVERIFY2(parsed.config.has_value(), qPrintable(parsed.errors.join('\n')));

        Fixture f;
        QSignalSpy const changed(f.controller.get(), &PagesController::themeChanged);
        f.controller->setConfig(required(parsed.config));
        QVERIFY(f.controller->currentTheme());
        QCOMPARE(f.controller->currentTheme()->name, QStringLiteral("Control"));

        // The left ring (LEDs 4..7) takes the theme's colour, the right one
        // (LEDs 0..3) is white.
        const auto ringReports = [&](bool left) {
            std::vector<std::vector<std::uint8_t>> reports;
            for (const auto &r : f.transport->featureReports) {
                if (r.size() > 5 && r[1] == 0x24 && (r[2] >= 4) == left)
                    reports.push_back(r);
            }
            return reports;
        };
        auto rings = ringReports(true);
        QVERIFY(rings.size() >= 4);
        for (const auto &r : ringReports(false))
            QVERIFY(r[3] == 255 && r[4] == 255 && r[5] == 255);
        QCOMPARE(int(rings.back()[3]), 255);

        const auto before = f.transport->writes.size();
        // A press enters theme mode: turning the dial then steps the themes.
        f.pressDial(0, true);
        f.pressDial(0, false);
        QVERIFY(f.controller->themeMode());
        QCOMPARE(f.controller->currentTheme()->name, QStringLiteral("Control")); // a press alone changes nothing
        f.turnDial(0, 1);
        QCOMPARE(f.media.volumeTotal, 0); // the volume is left alone
        QCOMPARE(f.controller->currentTheme()->name, QStringLiteral("Sound"));
        QCOMPARE(f.controller->currentPage(), QStringLiteral("sound"));
        QTRY_VERIFY_WITH_TIMEOUT(f.keysDrawnSince(before).contains(0), 2000); // drawn a moment later
        QTRY_VERIFY_WITH_TIMEOUT(int(ringReports(true).back()[3]) == 0xe5, 2000);
        rings = ringReports(true);
        QCOMPARE(int(rings.back()[3]), 0xe5);
        QCOMPARE(int(rings.back()[4]), 0x39);
        QCOMPARE(int(rings.back()[5]), 0x35);

        f.turnDial(0, 1); // wraps around to the first
        QCOMPARE(f.controller->currentTheme()->name, QStringLiteral("Control"));
        QCOMPARE(f.controller->currentPage(), QStringLiteral("main"));
        f.turnDial(0, -1); // and backwards
        QCOMPARE(f.controller->currentTheme()->name, QStringLiteral("Sound"));
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 3, 2000);

        // A second press goes back to the player control: turning is the volume again.
        f.pressDial(0, true);
        f.pressDial(0, false);
        QVERIFY(!f.controller->themeMode());
        f.turnDial(0, 1);
        QCOMPARE(f.controller->currentTheme()->name, QStringLiteral("Sound"));
    }

    void withoutThemesTheRingsStayDarkAndThePressDoesNothing()
    {
        const auto parsed = parsePagesConfig(
            QJsonDocument(QJsonObject{{"version", 1},
                                      {"home", "main"},
                                      {"pages", QJsonObject{{"main", page({})}}},
                                      {"dials", QJsonObject{{"0", QJsonObject{{"press", QJsonObject{{"type", "theme"}}}}}}}})
                .toJson());
        QVERIFY(parsed.config.has_value());
        Fixture f;
        f.controller->setConfig(required(parsed.config));
        QVERIFY(!f.controller->currentTheme());
        f.pressDial(0, true);
        f.pressDial(0, false);
        QVERIFY(!f.controller->currentTheme());
        QCOMPARE(f.controller->currentPage(), QStringLiteral("main"));
    }

    void theRightDialPressSwitchesToChoosingTheWallpaper()
    {
        const auto parsed = parsePagesConfig(QJsonDocument(QJsonObject{
            {"version", 1},
            {"home", "main"},
            {"screen", QJsonObject{{"wallpaper_validate_seconds", 60}, {"wallpaper_step_delay_ms", 0}}},
            {"pages", QJsonObject{{"main", page({})}}},
            {"dials", QJsonObject{{"1", QJsonObject{{"turn", mediaAction("volume").value("type").toString() == "media"
                                                              ? QJsonObject{{"type", "media"}, {"command", "seek"}, {"seconds", 5}}
                                                              : QJsonObject()},
                                                    {"press", QJsonObject{{"type", "wallpaper-mode"}}}}}}}}).toJson());
        QVERIFY2(parsed.config.has_value(), qPrintable(parsed.errors.join('\n')));
        Fixture f;
        f.controller->setConfig(required(parsed.config));
        QSignalSpy steps(f.controller.get(), &PagesController::wallpaperStep);
        QSignalSpy const modes(f.controller.get(), &PagesController::wallpaperModeChanged);

        const auto rightRing = [&] {
            for (auto & featureReport : std::ranges::reverse_view(f.transport->featureReports)) {
                if (featureReport.size() > 5 && featureReport[1] == 0x24 && featureReport[2] < 4)
                    return std::array<int, 3>{featureReport[3], featureReport[4], featureReport[5]};
            }
            return std::array<int, 3>{-1, -1, -1};
        };
        QCOMPARE(rightRing(), (std::array<int, 3>{255, 255, 255})); // the player control: white

        // Not in the mode: a turn is the configured one (seek), no wallpaper step.
        f.turnDial(1, 2);
        QCOMPARE(steps.count(), 0);
        QCOMPARE(f.media.seekTotal, 10);

        f.pressDial(1, true);
        f.pressDial(1, false);
        QVERIFY(f.controller->wallpaperMode());
        f.controller->wallpaperLabelShown(); // the screen shows the name: turns count now
        QCOMPARE(rightRing(), (std::array<int, 3>{255, 0, 255})); // magenta
        f.turnDial(1, 3);
        QCOMPARE(steps.count(), 1);
        QCOMPARE(steps.first().first().toInt(), 3); // as many places as the dial counts
        f.turnDial(1, -4);
        QCOMPARE(steps.last().first().toInt(), -4);
        QCOMPARE(f.media.seekTotal, 10); // the player is left alone

        f.pressDial(1, true); // a second press leaves the mode
        f.pressDial(1, false);
        QVERIFY(!f.controller->wallpaperMode());
        QCOMPARE(rightRing(), (std::array<int, 3>{255, 255, 255}));
        QVERIFY(modes.count() >= 2);
    }

    void whileSuspendedKeysAndDialsAreIgnoredAndTheRingsAreDark()
    {
        Fixture f;
        f.controller->setSuspended(true);
        const auto writes = f.transport->writes.size();
        f.media.setState(MediaController::State::Playing); // would redraw a key
        QCOMPARE(f.transport->writes.size(), writes);

        f.pressKey(1, true);
        f.pressKey(1, false);
        f.pressDial(0, true);
        f.pressDial(0, false);
        QCOMPARE(f.media.playPauseCount, 0);

        // Leaving it draws all the keys again.
        const auto before = f.transport->writes.size();
        f.controller->setSuspended(false);
        QCOMPARE(f.keysDrawnSince(before).size(), 12);
        f.pressKey(1, true);
        f.pressKey(1, false);
        QVERIFY(f.media.playPauseCount > 0);
    }

    void aKeyActionHoldsTheKeyAsLongAsTheKeyIsHeld()
    {
        const auto parsed = parsePagesConfig(pagesFile({{"main", page({keyJson(0, "7", QJsonObject{{"type", "key"}, {"key", "kp7"}}),
                                                                       keyJson(1, "P", QJsonObject{{"type", "page"}, {"page", "other"}})})},
                                                        {"other", page({})}}));
        QVERIFY2(parsed.config.has_value(), qPrintable(parsed.errors.join('\n')));
        Fixture f;
        FakeInjector injector;
        f.controller->setKeyInjector(&injector);
        f.controller->setConfig(required(parsed.config));
        QCOMPARE(injector.prepared, 1); // the device is made when the file is applied

        f.pressKey(0, true);
        QCOMPARE(injector.events.size(), size_t(1));
        QCOMPARE(injector.events[0].first, static_cast<int>(KeyCode::Kp7));
        QVERIFY(injector.events[0].second);
        f.pressKey(0, false);
        QCOMPARE(injector.events.size(), size_t(2));
        QVERIFY(!injector.events[1].second);

        // A key still held when the page changes is released: never stuck down.
        f.pressKey(0, true);
        f.controller->setSuspended(true);
        QVERIFY(!injector.events.back().second);
        QCOMPARE(injector.events.size(), size_t(4));
    }

    void withoutAnInjectorKeyActionsDoNothing()
    {
        const auto parsed = parsePagesConfig(pagesFile({{"main", page({keyJson(0, "7", QJsonObject{{"type", "key"}, {"key", "kp7"}})})}}));
        QVERIFY(parsed.config.has_value());
        Fixture f;
        f.controller->setConfig(required(parsed.config));
        f.pressKey(0, true);
        f.pressKey(0, false); // no crash, nothing to check but that
    }

    void themesTiedToTheMediaStateAreChosenByThemselves()
    {
        QJsonObject const config{
            {"version", 1},
            {"home", "main"},
            {"pages", QJsonObject{{"main", page({})}, {"numpad", page({})}, {"other", page({})}}},
            {"themes", QJsonArray{QJsonObject{{"name", "Control"}, {"page", "main"}, {"color", "#ffffff"}, {"when", "media"}},
                                  QJsonObject{{"name", "Other"}, {"page", "other"}, {"color", "#00bcd4"}},
                                  QJsonObject{{"name", "Keypad"}, {"page", "numpad"}, {"color", "#ff9800"}, {"when", "no-media"}}}}};
        const auto parsed = parsePagesConfig(QJsonDocument(config).toJson());
        QVERIFY2(parsed.config.has_value(), qPrintable(parsed.errors.join('\n')));
        Fixture f; // no player: stopped
        f.controller->setConfig(required(parsed.config));
        QCOMPARE(f.controller->currentTheme()->name, QStringLiteral("Keypad")); // by default without media
        QCOMPARE(f.controller->currentPage(), QStringLiteral("numpad"));

        f.media.setState(MediaController::State::Playing);
        QCOMPARE(f.controller->currentTheme()->name, QStringLiteral("Control"));
        f.media.setState(MediaController::State::Paused); // still a media: no change
        QCOMPARE(f.controller->currentTheme()->name, QStringLiteral("Control"));
        f.media.setState(MediaController::State::Stopped);
        QCOMPARE(f.controller->currentTheme()->name, QStringLiteral("Keypad"));
    }

    void aThemeChosenByHandStaysUntilTheNextChangeOfMedia()
    {
        QJsonObject const config{
            {"version", 1},
            {"home", "main"},
            {"pages", QJsonObject{{"main", page({})}, {"numpad", page({})}, {"other", page({})}}},
            {"themes", QJsonArray{QJsonObject{{"name", "Control"}, {"page", "main"}, {"color", "#ffffff"}, {"when", "media"}},
                                  QJsonObject{{"name", "Other"}, {"page", "other"}, {"color", "#00bcd4"}},
                                  QJsonObject{{"name", "Keypad"}, {"page", "numpad"}, {"color", "#ff9800"}, {"when", "no-media"}}}},
            {"dials", QJsonObject{{"0", QJsonObject{{"press", QJsonObject{{"type", "theme"}}}}}}}};
        const auto parsed = parsePagesConfig(QJsonDocument(config).toJson());
        QVERIFY(parsed.config.has_value());
        Fixture f;
        f.controller->setConfig(required(parsed.config));
        f.media.setState(MediaController::State::Playing);
        f.pressDial(0, true); // theme mode
        f.pressDial(0, false);
        f.turnDial(0, 1);
        QCOMPARE(f.controller->currentTheme()->name, QStringLiteral("Other"));
        // The state notifies again without changing between present and absent: nothing moves.
        f.media.setState(MediaController::State::Paused);
        QCOMPARE(f.controller->currentTheme()->name, QStringLiteral("Other"));
        // While the theme is being chosen, a media change does not interfere.
        f.media.setState(MediaController::State::Stopped);
        QCOMPARE(f.controller->currentTheme()->name, QStringLiteral("Other"));
    }

    void enteringOneChoiceModeLeavesTheOther()
    {
        const auto parsed = parsePagesConfig(QJsonDocument(QJsonObject{
            {"version", 1},
            {"home", "main"},
            {"screen", QJsonObject{{"wallpaper_validate_seconds", 60}, {"wallpaper_step_delay_ms", 0}}},
            {"pages", QJsonObject{{"main", page({})}}},
            {"themes", QJsonArray{QJsonObject{{"name", "Control"}, {"page", "main"}, {"color", "#ffd600"}}}},
            {"dials", QJsonObject{{"0", QJsonObject{{"press", QJsonObject{{"type", "theme"}}}}},
                                  {"1", QJsonObject{{"press", QJsonObject{{"type", "wallpaper-mode"}}}}}}}}).toJson());
        QVERIFY2(parsed.config.has_value(), qPrintable(parsed.errors.join('\n')));
        Fixture f;
        f.controller->setConfig(required(parsed.config));

        f.pressDial(1, true);
        f.pressDial(1, false);
        QVERIFY(f.controller->wallpaperMode() && !f.controller->themeMode());
        f.pressDial(0, true); // the theme mode takes over
        f.pressDial(0, false);
        QVERIFY(f.controller->themeMode() && !f.controller->wallpaperMode());
        f.pressDial(1, true); // and back
        f.pressDial(1, false);
        QVERIFY(f.controller->wallpaperMode() && !f.controller->themeMode());
    }

    void withoutTouchingTheDialTheWallpaperChoiceIsValidatedByItself()
    {
        const auto parsed = parsePagesConfig(QJsonDocument(QJsonObject{
            {"version", 1},
            {"home", "main"},
            {"screen", QJsonObject{{"wallpaper_validate_seconds", 2}, {"wallpaper_step_delay_ms", 0}}},
            {"pages", QJsonObject{{"main", page({})}}},
            {"dials", QJsonObject{{"1", QJsonObject{{"press", QJsonObject{{"type", "wallpaper-mode"}}}}}}}}).toJson());
        QVERIFY2(parsed.config.has_value(), qPrintable(parsed.errors.join('\n')));
        Fixture f;
        f.controller->setConfig(required(parsed.config));
        QSignalSpy const steps(f.controller.get(), &PagesController::wallpaperStep);

        QSignalSpy countdown(f.controller.get(), &PagesController::wallpaperCountdown);
        f.pressDial(1, true);
        f.pressDial(1, false);
        QVERIFY(f.controller->wallpaperMode());
        f.controller->wallpaperLabelShown();
        QCOMPARE(countdown.last().first().toInt(), 2); // the countdown starts at the validation time
        // Turning keeps the mode: the time starts again at each step.
        for (int i = 0; i < 3; ++i) {
            QTest::qWait(500);
            f.turnDial(1, 1);
            QVERIFY(f.controller->wallpaperMode());
        }
        QCOMPARE(steps.count(), 3);
        // Left alone: the countdown runs with the dial still in the choice, then it is validated.
        countdown.clear();
        QTRY_VERIFY_WITH_TIMEOUT(countdown.count() >= 1 && countdown.last().first().toInt() == 1, 3000);
        QVERIFY(f.controller->wallpaperMode()); // still choosing while it counts
        QTRY_VERIFY_WITH_TIMEOUT(!f.controller->wallpaperMode(), 5000); // over: validated, the ring goes white
        QCOMPARE(countdown.last().first().toInt(), 0);
    }

    void theNextTurnIsListenedToOnlyOnceTheStepIsShown()
    {
        const auto parsed = parsePagesConfig(QJsonDocument(QJsonObject{
            {"version", 1},
            {"home", "main"},
            {"screen", QJsonObject{{"wallpaper_validate_seconds", 60}, {"wallpaper_step_delay_ms", 300}}},
            {"pages", QJsonObject{{"main", page({})}}},
            {"dials", QJsonObject{{"1", QJsonObject{{"press", QJsonObject{{"type", "wallpaper-mode"}}}}}}}}).toJson());
        QVERIFY2(parsed.config.has_value(), qPrintable(parsed.errors.join('\n')));
        Fixture f;
        f.controller->setConfig(required(parsed.config));
        QSignalSpy steps(f.controller.get(), &PagesController::wallpaperStep);
        f.pressDial(1, true);
        f.pressDial(1, false);
        f.controller->wallpaperLabelShown(); // the name is on the screen
        QTest::qWait(400);

        f.turnDial(1, 3);  // taken, and as one place
        QCOMPARE(steps.count(), 1);
        QCOMPARE(steps.last().first().toInt(), 1);
        f.turnDial(1, 1);  // its picture is not shown yet: ignored, not queued
        f.turnDial(1, 1);
        QCOMPARE(steps.count(), 1);

        f.controller->wallpaperLabelShown(); // the screen shows it now
        f.turnDial(1, 1);  // 350 ms later: the rest (300 ms) is over, so this one counts
        QCOMPARE(steps.count(), 2);
        f.controller->wallpaperLabelShown();
        QTest::qWait(100); // inside the rest after the picture: ignored
        {
            std::vector<std::uint8_t> const turn = {0x01, 0x03, 0x03, 0x00, 0x01, 0x00, 0x01};
            f.transport->pendingReads.push_back(turn);
            QTRY_VERIFY_WITH_TIMEOUT(f.transport->pendingReads.empty(), 2000);
            QTest::qWait(30);
        }
        QCOMPARE(steps.count(), 2);
        QTest::qWait(1700); // nothing was queued
        QCOMPARE(steps.count(), 2);
    }

    void aScreenThatNeverAnswersDoesNotBlockTheDialForGood()
    {
        const auto parsed = parsePagesConfig(QJsonDocument(QJsonObject{
            {"version", 1},
            {"home", "main"},
            {"screen", QJsonObject{{"wallpaper_validate_seconds", 60}, {"wallpaper_step_delay_ms", 100}}},
            {"pages", QJsonObject{{"main", page({})}}},
            {"dials", QJsonObject{{"1", QJsonObject{{"press", QJsonObject{{"type", "wallpaper-mode"}}}}}}}}).toJson());
        QVERIFY(parsed.config.has_value());
        Fixture f;
        f.controller->setConfig(required(parsed.config));
        QSignalSpy const steps(f.controller.get(), &PagesController::wallpaperStep);
        f.pressDial(1, true);
        f.pressDial(1, false);
        f.controller->wallpaperLabelShown();
        f.turnDial(1, 1);
        QCOMPARE(steps.count(), 1);
        f.turnDial(1, 1); // no answer from the screen: ignored for now...
        QCOMPARE(steps.count(), 1);
        QTest::qWait(1700); // ...but not for ever
        f.turnDial(1, 1);
        QCOMPARE(steps.count(), 2);
    }

    void turnsAreIgnoredUntilTheWallpaperNameIsShown()
    {
        const auto parsed = parsePagesConfig(QJsonDocument(QJsonObject{
            {"version", 1},
            {"home", "main"},
            {"screen", QJsonObject{{"wallpaper_validate_seconds", 60}, {"wallpaper_step_delay_ms", 0}}},
            {"pages", QJsonObject{{"main", page({})}}},
            {"dials", QJsonObject{{"1", QJsonObject{{"press", QJsonObject{{"type", "wallpaper-mode"}}}}}}}}).toJson());
        QVERIFY(parsed.config.has_value());
        Fixture f;
        f.controller->setConfig(required(parsed.config));
        QSignalSpy const steps(f.controller.get(), &PagesController::wallpaperStep);
        f.pressDial(1, true);
        f.pressDial(1, false);
        QVERIFY(f.controller->wallpaperMode());
        f.turnDial(1, 1); // the name is not on the screen yet: nothing happens
        QCOMPARE(steps.count(), 0);
        f.controller->wallpaperLabelShown();
        f.turnDial(1, 1);
        QCOMPARE(steps.count(), 1);
    }

    void withoutTheScreenSayingSoTurnsCountAfterAMoment()
    {
        const auto parsed = parsePagesConfig(QJsonDocument(QJsonObject{
            {"version", 1},
            {"home", "main"},
            {"screen", QJsonObject{{"wallpaper_validate_seconds", 60}, {"wallpaper_step_delay_ms", 0}}},
            {"pages", QJsonObject{{"main", page({})}}},
            {"dials", QJsonObject{{"1", QJsonObject{{"press", QJsonObject{{"type", "wallpaper-mode"}}}}}}}}).toJson());
        QVERIFY(parsed.config.has_value());
        Fixture f;
        f.controller->setConfig(required(parsed.config));
        QSignalSpy const steps(f.controller.get(), &PagesController::wallpaperStep);
        f.pressDial(1, true);
        f.pressDial(1, false);
        QTest::qWait(1700); // the safety delay: a screen that never answers cannot block the dial for good
        f.turnDial(1, 1);
        QCOMPARE(steps.count(), 1);
    }

    void aPageCanPutItsApplicationsInOrderOfUse()
    {
        DataFolders data;
        data.addApp("a.desktop", "App A");
        data.addApp("b.desktop", "App B");
        data.addApp("c.desktop", "App C");
        const auto appKey = [](int key, const QString &desktop) {
            return QJsonObject{{"key", key}, {"action", QJsonObject{{"type", "app"}, {"desktop", desktop}}}};
        };
        QJsonObject pageObject{{"keys", QJsonArray{appKey(0, "a.desktop"), appKey(1, "b.desktop"), appKey(2, "c.desktop")}},
                               {"sort", "usage"}};
        const QString file = data.dir.filePath("pages.json");
        QVERIFY(writeFile(file, pagesFile({{"main", pageObject}})));
        Fixture f;
        QStringList launched;
        f.controller->setAppLauncher([&launched](const DesktopApp &app) {
            launched << app.id;
            return true;
        });
        f.controller->loadFile(file);

        f.pressKey(2, true); // C: its page order is the file's at first
        f.pressKey(2, false);
        QCOMPARE(launched, QStringList{"c.desktop"});
        QTest::qWait(800);

        // C was used: it is now the first key, and the others follow in the file's order.
        f.pressKey(0, true);
        f.pressKey(0, false);
        QCOMPARE(launched, (QStringList{"c.desktop", "c.desktop"}));
        QTest::qWait(800);
        f.pressKey(1, true); // A is now second
        f.pressKey(1, false);
        QCOMPARE(launched.last(), QStringLiteral("a.desktop"));
        QTest::qWait(800);

        // A was used once: it goes in front of B, behind C (used twice).
        f.pressKey(1, true);
        f.pressKey(1, false);
        QCOMPARE(launched.last(), QStringLiteral("a.desktop"));
        QTest::qWait(800);
        f.pressKey(2, true);
        f.pressKey(2, false);
        QCOMPARE(launched.last(), QStringLiteral("b.desktop"));

        // The most used one carries a gold star in its corner; the others do not.
        const auto goldInCorner = [&f](int key) {
            QImage image;
            image.loadFromData(lastKeyJpeg(*f.transport, key), "JPEG");
            const QRgb p = image.pixel(133, 26);
            return qRed(p) > 200 && qGreen(p) > 170 && qBlue(p) < 140;
        };
        QVERIFY(goldInCorner(0));
        QVERIFY(!goldInCorner(1));
    }

    void theUsageIsKeptBetweenRuns()
    {
        DataFolders data;
        data.addApp("a.desktop", "App A");
        data.addApp("b.desktop", "App B");
        const auto appKey = [](int key, const QString &desktop) {
            return QJsonObject{{"key", key}, {"action", QJsonObject{{"type", "app"}, {"desktop", desktop}}}};
        };
        const QString file = data.dir.filePath("pages.json");
        QVERIFY(writeFile(file, pagesFile({{"main", QJsonObject{{"keys", QJsonArray{appKey(0, "a.desktop"), appKey(1, "b.desktop")}},
                                                                {"sort", "usage"}}}})));
        const QString usage = data.dir.filePath("share/usage.json");
        {
            Fixture f;
            f.controller->setAppLauncher([](const DesktopApp &) { return true; });
            f.controller->setUsageFile(usage);
            f.controller->loadFile(file);
            f.pressKey(1, true); // B
            f.pressKey(1, false);
        }
        QVERIFY(QFile::exists(usage));
        Fixture again;
        QStringList launched;
        again.controller->setAppLauncher([&launched](const DesktopApp &app) {
            launched << app.id;
            return true;
        });
        again.controller->setUsageFile(usage);
        again.controller->loadFile(file);
        again.pressKey(0, true); // B is first at once, from the file
        again.pressKey(0, false);
        QCOMPARE(launched, QStringList{"b.desktop"});
    }

    void steamGamesFillTheFreeKeysMostPlayedFirstAndStartThroughSteam()
    {
        const auto now = QDateTime::currentSecsSinceEpoch();
        const auto game = [now](int appid, const QString &name, qint64 minutes, qint64 agoDays) {
            SteamGame g;
            g.appid = appid;
            g.name = name;
            g.playtimeMinutes = minutes;
            g.lastPlayed = now - (agoDays * 86400);
            return g;
        };
        const auto parsed = parsePagesConfig(QJsonDocument(QJsonObject{
            {"version", 1}, {"home", "games"},
            {"pages", QJsonObject{{"games", QJsonObject{{"keys", QJsonArray{keyJson(5, "Stop", mediaAction("stop"))}},
                                                        {"steam", QJsonObject{{"max", 3}}}, {"sort", "usage"}}}}}}).toJson());
        QVERIFY2(parsed.config.has_value(), qPrintable(parsed.errors.join('\n')));

        Fixture f;
        QList<int> started;
        f.controller->setSteamLauncher([&started](int appid) {
            started << appid;
            return true;
        });
        f.controller->setSources([&](const QString &) {
            ArrangeSources sources;
            sources.installedSteam = QList<SteamGame>{game(11, "Old Favourite", 6000, 700),  // 100 h, but two years ago
                                                      game(22, "Played Yesterday", 300, 1),    // 5 h, recently
                                                      game(33, "Never Touched", 0, 0),
                                                      game(44, "Fourth", 60, 5)};
            for (const SteamGame &g : std::as_const(sources.installedSteam))
                sources.playedSteam.insert(g.appid, g);
            return sources;
        });
        f.controller->setConfig(required(parsed.config));
        // Three free keys (0..2 and 3, with 5 taken): the three most played in recent time come first.
        f.pressKey(0, true);
        f.pressKey(0, false);
        QCOMPARE(started, QList<int>{22}); // 5 h yesterday beats 100 h two years ago
        QTest::qWait(800);
        // It was started from the module: its use counts too, and it stays first.
        f.pressKey(0, true);
        f.pressKey(0, false);
        QCOMPARE(started, (QList<int>{22, 22}));
        QTest::qWait(800);
        f.pressKey(1, true); // the second: "Fourth" (1 h, 5 days) beats "Old Favourite" (faded to nothing)
        f.pressKey(1, false);
        QCOMPARE(started.last(), 44);
    }

    void theExtraMediaCommandsReachTheController()
    {
        const auto key = [](int n, const QString &command) { return keyJson(n, command, mediaAction(command)); };
        Fixture f;
        const auto parsed = parsePagesConfig(pagesFile({{"main", page({key(0, "shuffle"), key(1, "repeat"), key(2, "mute"), key(3, "raise"),
                                                                       key(4, "fullscreen"), key(5, "restart")})}}));
        QVERIFY2(parsed.config.has_value(), qPrintable(parsed.errors.join('\n')));
        f.controller->setConfig(required(parsed.config));
        for (int k = 0; k < 6; ++k) {
            f.pressKey(k, true);
            f.pressKey(k, false);
        }
        QCOMPARE(f.media.shuffleCount, 1);
        QCOMPARE(f.media.repeatCount, 1);
        QCOMPARE(f.media.muteCount, 1);
        QCOMPARE(f.media.raiseCount, 1);
        QCOMPARE(f.media.fullscreenCount, 1);
        QCOMPARE(f.media.restartCount, 1);
        QCOMPARE(f.media.playPauseCount, 0); // nothing else was pressed
    }

    void shuffleAndRepeatKeysShowTheirStateWithTheUsersWords()
    {
        const auto shuffle = keyJson(0, "Mix", mediaAction("shuffle"));
        QJsonObject withWords = keyJson(1, "Rep", mediaAction("repeat"));
        withWords.insert("state_labels", QJsonArray{"NON", "LISTE", "PISTE"});
        QJsonObject shuffleWords = shuffle;
        shuffleWords.insert("state_labels", QJsonArray{"NON", "OUI"});
        const auto parsed = parsePagesConfig(pagesFile({{"main", page({shuffleWords, withWords})}}));
        QVERIFY2(parsed.config.has_value(), qPrintable(parsed.errors.join('\n')));
        QCOMPARE(required(required(parsed.config).pages.value("main").keys[0]).stateLabels, (QStringList{"NON", "OUI"}));

        Fixture f;
        f.controller->setConfig(required(parsed.config));
        const auto background = [&f](int key) {
            QImage image;
            image.loadFromData(lastKeyJpeg(*f.transport, key), "JPEG");
            return image.pixel(20, 20);
        };
        const QRgb offShuffle = background(0);
        const QRgb offRepeat = background(1);
        QVERIFY(qGray(offShuffle) == qGray(offRepeat) || qAbs(qGray(offShuffle) - qGray(offRepeat)) < 6);

        f.media.setExtras(true, MediaController::Loop::Track); // both are on: the keys are lighter
        QTRY_VERIFY_WITH_TIMEOUT(qGray(background(0)) > qGray(offShuffle) + 15, 2000);
        QTRY_VERIFY_WITH_TIMEOUT(qGray(background(1)) > qGray(offRepeat) + 15, 2000);
        f.media.setExtras(false, MediaController::Loop::None); // and back
        QTRY_VERIFY_WITH_TIMEOUT(qGray(background(0)) <= qGray(offShuffle) + 6, 2000);
        QTRY_VERIFY_WITH_TIMEOUT(qGray(background(1)) <= qGray(offRepeat) + 6, 2000);
    }
};

QTEST_MAIN(TestPagesController)
#include "test_pagescontroller.moc"
