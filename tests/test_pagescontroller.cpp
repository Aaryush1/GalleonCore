// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FakeTransport.h"

#include <PagesController.h>
#include <galleon/StreamDeck.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace galleond;
using galleon::StreamDeck;

namespace {

class FakeMedia : public MediaController
{
public:
    [[nodiscard]] State state() const override { return m_state; }
    void playPause() override { ++playPauseCount; }
    void next() override { ++nextCount; }
    void previous() override { ++previousCount; }
    void stop() override { ++stopCount; }
    void changeVolume(int percent) override { volumeTotal += percent; }
    void seek(int seconds) override { seekTotal += seconds; }

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

private:
    State m_state = State::Stopped;
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
        std::vector<std::uint8_t> report = {0x01, 0x03, 0x03, 0x00, 0x01, 0x00, 0x00};
        report[5 + static_cast<std::size_t>(dial)] = static_cast<std::uint8_t>(signedSteps);
        transport->pendingReads.push_back(report);
        QTRY_VERIFY_WITH_TIMEOUT(transport->pendingReads.empty(), 2000);
        QTest::qWait(30);
    }

    void pressDial(int dial, bool pressed)
    {
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
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(bytes);
    return true;
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
        const auto before = f.transport->writes.size();
        f.pressKey(4, true);
        f.pressKey(4, false);
        QCOMPARE(f.transport->writes.size(), before);
        QCOMPARE(f.media.playPauseCount + f.media.nextCount + f.media.stopCount, 0);
    }

    void dialsTurnAndPress()
    {
        Fixture f;
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
        QTemporaryDir dir;
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
        QTemporaryDir dir;
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
        QTemporaryDir dir;
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
        QTemporaryDir dir;
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
        QTemporaryDir dir;
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
        QTemporaryDir dir;
        Fixture f;
        f.controller->loadFile(dir.filePath("absent.json"));
        QCOMPARE(f.controller->currentPage(), QStringLiteral("main"));
        f.pressKey(2, true);
        f.pressKey(2, false);
        QCOMPARE(f.media.nextCount, 1);
    }

    void aChangeOnDiskIsReloaded()
    {
        QTemporaryDir dir;
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
};

QTEST_MAIN(TestPagesController)
#include "test_pagescontroller.moc"
