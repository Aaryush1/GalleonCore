// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Required.h"
#include <PagesConfig.h>
#include <PagesEditor.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace galleond;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(bytes);
    return true;
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

struct Environment
{
    QTemporaryDir dir;
    QByteArray oldHome = qgetenv("XDG_DATA_HOME");
    QByteArray oldDirs = qgetenv("XDG_DATA_DIRS");
    Environment()
    {
        qputenv("XDG_DATA_HOME", dir.filePath("home").toUtf8());
        qputenv("XDG_DATA_DIRS", dir.filePath("system").toUtf8());
        if (!writeFile(dir.filePath("system/applications/tool.desktop"),
                       "[Desktop Entry]\nType=Application\nName=The Tool\nExec=/usr/bin/true\nIcon=tool-icon\n"))
            qFatal("cannot write the desktop file");
        if (!writeFile(dir.filePath("system/applications/other.desktop"),
                       "[Desktop Entry]\nType=Application\nName=Other\nExec=/usr/bin/true\n"))
            qFatal("cannot write the desktop file");
    }
    ~Environment()
    {
        qputenv("XDG_DATA_HOME", oldHome);
        qputenv("XDG_DATA_DIRS", oldDirs);
    }
    [[nodiscard]] QString pages() const { return dir.filePath("config/pages.json"); }
};

PagesConfig configOf(const QString &file)
{
    const auto result = parsePagesConfig(readFile(file));
    if (!result.config)
        qFatal("the edited file is not valid: %s", qPrintable(result.errors.join(' ')));
    return *result.config;
}


// A pages file with a screensaver: geometry, two videos (one with a title) and settings.
QByteArray saverFile(const QString &video = QStringLiteral(R"([{"file": "a.mp4", "title": "First"}, "b.mp4", "c.mp4"])"))
{
    return (QStringLiteral(R"({"version": 1, "home": "main", "pages": {"main": {"keys": []}}, "screensaver": {
        "idle_minutes": 5, "fps": 15, "while_playing": false, "video": )") + video
            + QStringLiteral(R"(, "geometry": {"screen_mm": [62.6, 33.0], "key_mm": [13.7, 13.7], "gap_mm": [5.4, 5.4], "screen_to_keys_mm": 5.4}}})"))
        .toUtf8();
}


// Three themes (the first is the home page) and a sub-page that the first one leads to.
QByteArray themesFile()
{
    return {R"({"version": 1, "home": "main",
      "themes": [{"name": "Control", "page": "main", "color": "#ffd600"},
                 {"name": "Music", "page": "music", "color": "#e53935"},
                 {"name": "Games", "page": "games", "color": "#3949ab"}],
      "pages": {
        "main": {"keys": [
          {"key": 0, "label": "Shuffle", "icon": "media-playlist-shuffle", "action": {"type": "media", "command": "shuffle"}, "state_labels": ["OFF", "ON"]},
          {"key": 1, "label": "More", "action": {"type": "page", "page": "sub"}}]},
        "music": {"keys": []}, "games": {"keys": []},
        "sub": {"keys": [{"key": 0, "label": "Back", "action": {"type": "back"}}]}}})"};
}

KeySpec spec(const QString &type)
{
    KeySpec out;
    out.type = type;
    return out;
}

} // namespace

class TestPagesEditor : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aMissingFileStartsFromTheBuiltInPages()
    {
        Environment const env;
        const auto result = PagesEditor::assignApp(env.pages(), "main", 4, "tool.desktop");
        QVERIFY2(result.ok, qPrintable(result.errors.join('\n')));
        const PagesConfig config = configOf(env.pages());
        const Page main = config.pages.value("main");
        const auto &key = main.keys[4];
        QVERIFY(key.has_value());
        QCOMPARE(int(required(key).action.type), int(ActionType::App));
        QCOMPARE(required(key).action.desktop, QStringLiteral("tool.desktop"));
        QVERIFY(required(key).label.isEmpty()); // the application's own label and icon are used
        QVERIFY(config.pages.value("main").keys[0].has_value()); // the other keys are still there
        QVERIFY(config.dials[0].turn.type == ActionType::Media);
    }

    void aKeyThatWasUsedIsReplacedWithoutItsOldLabel()
    {
        Environment const env;
        QVERIFY(PagesEditor::assignApp(env.pages(), "main", 5, "tool.desktop").ok); // key 5 was "Vol +"
        const PagesConfig config = configOf(env.pages());
        const Page main = config.pages.value("main");
        const auto &key = main.keys[5];
        QCOMPARE(int(required(key).action.type), int(ActionType::App));
        QVERIFY(required(key).label.isEmpty());
        QVERIFY(required(key).icon.name.isEmpty());
        QCOMPARE(int(required(config.pages.value("main").keys[3]).action.command), int(MediaCommand::Volume)); // untouched
    }

    void everyKeyKeepsItsPlaceAndTheListStaysSorted()
    {
        Environment const env;
        QVERIFY(PagesEditor::assignApp(env.pages(), "main", 9, "tool.desktop").ok);
        QVERIFY(PagesEditor::assignApp(env.pages(), "main", 4, "other.desktop").ok);
        const QJsonObject root = QJsonDocument::fromJson(readFile(env.pages())).object();
        const QJsonArray keys = root.value("pages").toObject().value("main").toObject().value("keys").toArray();
        int previous = -1;
        for (const auto &value : keys) {
            const int number = value.toObject().value("key").toInt();
            QVERIFY(number > previous);
            previous = number;
        }
        QCOMPARE(required(configOf(env.pages()).pages.value("main").keys[9]).action.desktop, QStringLiteral("tool.desktop"));
        QCOMPARE(required(configOf(env.pages()).pages.value("main").keys[4]).action.desktop, QStringLiteral("other.desktop"));
    }

    void clearingEmptiesTheKey()
    {
        Environment const env;
        QVERIFY(PagesEditor::clearKey(env.pages(), "main", 0).ok);
        const PagesConfig config = configOf(env.pages());
        QVERIFY(!config.pages.value("main").keys[0].has_value());
        QVERIFY(config.pages.value("main").keys[1].has_value());
        QVERIFY(PagesEditor::clearKey(env.pages(), "main", 0).ok); // already empty: still fine
    }

    void theOldFileIsKeptAsABackup()
    {
        Environment const env;
        QVERIFY(PagesEditor::assignApp(env.pages(), "main", 4, "tool.desktop").ok); // creates the file
        const QByteArray first = readFile(env.pages());
        QVERIFY(!QFile::exists(env.pages() + ".bak")); // nothing to back up yet
        QVERIFY(PagesEditor::assignApp(env.pages(), "main", 7, "other.desktop").ok);
        QCOMPARE(readFile(env.pages() + ".bak"), first);
        QVERIFY(readFile(env.pages()) != first);
    }

    void thePermissionsAreKeptAndNeverWorldWritable()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), defaultPagesJson()));
        QFile file(env.pages());
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)); // 0600
        QVERIFY(PagesEditor::assignApp(env.pages(), "main", 4, "tool.desktop").ok);
        const QFile::Permissions mask = QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther;
        QCOMPARE(QFileInfo(env.pages()).permissions() & mask, QFile::ReadOwner | QFile::WriteOwner);

        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadOther | QFile::WriteOther)); // 0666
        QVERIFY(PagesEditor::assignApp(env.pages(), "main", 5, "tool.desktop").ok);
        QVERIFY(!QFileInfo(env.pages()).permissions().testFlag(QFile::WriteOther));
    }

    void aFileWithErrorsIsNeverRewritten()
    {
        Environment const env;
        const QByteArray broken = R"({ "version": 1, "home": "main", "pages": { "main": { "keys": [ { "key": 99 } ] } } })";
        QVERIFY(writeFile(env.pages(), broken));
        const auto result = PagesEditor::assignApp(env.pages(), "main", 4, "tool.desktop");
        QVERIFY(!result.ok);
        QVERIFY(result.errors.join(' ').contains("keys[0].key"));
        QCOMPARE(readFile(env.pages()), broken);
        QVERIFY(!QFile::exists(env.pages() + ".bak"));
    }

    void badRequestsChangeNothing()
    {
        Environment env;
        QVERIFY(writeFile(env.pages(), defaultPagesJson()));
        const QByteArray before = readFile(env.pages());
        const auto refused = [&](const EditResult &result) {
            QVERIFY(!result.ok);
            QVERIFY(!result.errors.isEmpty());
            QCOMPARE(readFile(env.pages()), before);
        };
        refused(PagesEditor::assignApp(env.pages(), "main", 12, "tool.desktop"));
        refused(PagesEditor::assignApp(env.pages(), "main", -1, "tool.desktop"));
        refused(PagesEditor::assignApp(env.pages(), "nowhere", 4, "tool.desktop"));
        refused(PagesEditor::assignApp(env.pages(), "main", 4, "absent.desktop")); // a typo is not saved
        refused(PagesEditor::assignApp(env.pages(), "main", 4, "../../bin/sh.desktop"));
        refused(PagesEditor::assignApp(env.pages(), "main", 4, "firefox"));
        refused(PagesEditor::assignApp(env.pages(), "main", 4, QString()));
        refused(PagesEditor::clearKey(env.pages(), "main", 12));
        refused(PagesEditor::clearKey(env.pages(), "nowhere", 0));
    }

    void anApplicationGivenByPathIsAccepted()
    {
        Environment const env;
        const QString path = env.dir.filePath("system/applications/tool.desktop");
        QVERIFY(PagesEditor::assignApp(env.pages(), "main", 4, path).ok);
        QCOMPARE(required(configOf(env.pages()).pages.value("main").keys[4]).action.desktop, path);
    }

    void aSecondPageCanBeEdited()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), "{ \"version\": 1, \"home\": \"main\", \"pages\": { \"main\": { \"keys\": [] }, \"apps\": { \"keys\": [] } } }"));
        QVERIFY(PagesEditor::assignApp(env.pages(), "apps", 0, "tool.desktop").ok);
        const PagesConfig config = configOf(env.pages());
        QVERIFY(config.pages.value("apps").keys[0].has_value());
        QVERIFY(!config.pages.value("main").keys[0].has_value());
    }

    void theScreenSectionAndDialsSurviveAnEdit()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), "{ \"version\": 1, \"home\": \"main\", \"pages\": { \"main\": { \"keys\": [] } },"
                                       " \"dials\": { \"1\": { \"press\": { \"type\": \"media\", \"command\": \"next\" } } },"
                                       " \"screen\": { \"now_playing\": \"playing-or-paused\", \"video_thumbnails\": false } }"));
        QVERIFY(PagesEditor::assignApp(env.pages(), "main", 2, "tool.desktop").ok);
        const PagesConfig config = configOf(env.pages());
        QCOMPARE(int(config.dials[1].press.command), int(MediaCommand::Next));
        QCOMPARE(int(config.screen.nowPlaying), int(NowPlayingMode::PlayingOrPaused));
        QVERIFY(!config.screen.videoThumbnails);
    }

    void theScreensaverSettingsAreChangedAndTheRestStays()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), saverFile()));
        QVERIFY(PagesEditor::setScreensaverIdleMinutes(env.pages(), 20).ok);
        QVERIFY(PagesEditor::setScreensaverFps(env.pages(), 12).ok);
        QVERIFY(PagesEditor::setScreensaverWhilePlaying(env.pages(), true).ok);
        const ScreensaverConfig saver = configOf(env.pages()).screensaver;
        QCOMPARE(saver.idleMinutes, 20);
        QCOMPARE(saver.fps, 12);
        QVERIFY(saver.whilePlaying);
        QCOMPARE(saver.videos, (QStringList{"a.mp4", "b.mp4", "c.mp4"}));
        QCOMPARE(saver.titles, (QStringList{"First", "", ""}));
        QVERIFY(saver.hasGeometry);
        QCOMPARE(saver.screenWidthMm, 62.6);
        QVERIFY(QFileInfo::exists(env.pages() + ".bak")); // the file as it was before the last edit
    }

    void aSettingOutOfRangeIsRefusedAndNothingIsWritten()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), saverFile()));
        const QByteArray before = readFile(env.pages());
        for (const int minutes : {0, -3, 721})
            QVERIFY2(!PagesEditor::setScreensaverIdleMinutes(env.pages(), minutes).ok, qPrintable(QString::number(minutes)));
        for (const int fps : {0, 21, 100})
            QVERIFY2(!PagesEditor::setScreensaverFps(env.pages(), fps).ok, qPrintable(QString::number(fps)));
        QCOMPARE(readFile(env.pages()), before);
        QVERIFY(!QFileInfo::exists(env.pages() + ".bak"));
    }

    void theBoundsOfTheSettingsAreAccepted()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), saverFile()));
        QVERIFY(PagesEditor::setScreensaverIdleMinutes(env.pages(), 1).ok);
        QVERIFY(PagesEditor::setScreensaverIdleMinutes(env.pages(), 720).ok);
        QVERIFY(PagesEditor::setScreensaverFps(env.pages(), 1).ok);
        QVERIFY(PagesEditor::setScreensaverFps(env.pages(), 20).ok);
    }

    void aTitleIsSetTrimmedAndTakenAway()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), saverFile()));
        QVERIFY(PagesEditor::setScreensaverVideoTitle(env.pages(), 1, "  Second  ").ok); // was a plain path
        QCOMPARE(configOf(env.pages()).screensaver.titles, (QStringList{"First", "Second", ""}));
        QVERIFY(PagesEditor::setScreensaverVideoTitle(env.pages(), 0, "   ").ok); // blank: no title any more
        QCOMPARE(configOf(env.pages()).screensaver.titles, (QStringList{"", "Second", ""}));
        QCOMPARE(configOf(env.pages()).screensaver.videos, (QStringList{"a.mp4", "b.mp4", "c.mp4"}));
    }

    void aTitleThatIsTooLongIsRefusedAndNothingIsWritten()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), saverFile()));
        const QByteArray before = readFile(env.pages());
        const auto result = PagesEditor::setScreensaverVideoTitle(env.pages(), 0, QString(MaxVideoTitleLength + 1, 'x'));
        QVERIFY(!result.ok);
        QVERIFY(!result.errors.isEmpty());
        QCOMPARE(readFile(env.pages()), before);
    }

    void aVideoIsRemovedAndTheOthersKeepTheirOrderAndTitles()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), saverFile()));
        QVERIFY(PagesEditor::removeScreensaverVideo(env.pages(), 1).ok);
        const ScreensaverConfig saver = configOf(env.pages()).screensaver;
        QCOMPARE(saver.videos, (QStringList{"a.mp4", "c.mp4"}));
        QCOMPARE(saver.titles, (QStringList{"First", ""}));
        QVERIFY(saver.enabled());
        QVERIFY(PagesEditor::removeScreensaverVideo(env.pages(), 0).ok);
        QCOMPARE(configOf(env.pages()).screensaver.videos, (QStringList{"c.mp4"}));
    }

    void theLastVideoGoneTheScreensaverIsOffButItsSettingsStay()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), saverFile(R"("only.mp4")")));
        QVERIFY(PagesEditor::removeScreensaverVideo(env.pages(), 0).ok);
        const QJsonObject saverObject = QJsonDocument::fromJson(readFile(env.pages())).object().value("screensaver").toObject();
        QVERIFY(!saverObject.contains("video")); // not an empty list, which the format refuses
        const ScreensaverConfig saver = configOf(env.pages()).screensaver;
        QVERIFY(saver.videos.isEmpty());
        QVERIFY(!saver.enabled());
        QVERIFY(saver.hasGeometry);
        QCOMPARE(saver.idleMinutes, 5);
    }

    void everyWayToWriteTheVideosIsUnderstood()
    {
        Environment const env;
        // One path, one object, a list: all can be edited.
        for (const QString &video : {QStringLiteral(R"("one.mp4")"), QStringLiteral(R"({"file": "one.mp4", "title": "One"})"),
                                     QStringLiteral(R"(["one.mp4"])")}) {
            QVERIFY(writeFile(env.pages(), saverFile(video)));
            QVERIFY2(PagesEditor::setScreensaverVideoTitle(env.pages(), 0, "Renamed").ok, qPrintable(video));
            QCOMPARE(configOf(env.pages()).screensaver.titles, (QStringList{"Renamed"}));
            QCOMPARE(configOf(env.pages()).screensaver.videos, (QStringList{"one.mp4"}));
        }
    }

    void aVideoIsAddedAtTheEndWithItsTitle()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), saverFile()));
        QVERIFY(PagesEditor::addScreensaverVideo(env.pages(), "/videos/d.mp4", "  Fourth ").ok);
        QVERIFY(PagesEditor::addScreensaverVideo(env.pages(), "e.mp4", "").ok); // relative, no title
        const ScreensaverConfig saver = configOf(env.pages()).screensaver;
        QCOMPARE(saver.videos, (QStringList{"a.mp4", "b.mp4", "c.mp4", "/videos/d.mp4", "e.mp4"}));
        QCOMPARE(saver.titles, (QStringList{"First", "", "", "Fourth", ""}));
        QVERIFY(saver.hasGeometry); // the rest of the section stays
    }

    void aVideoCanBeAddedWhenThereIsNoneYet()
    {
        Environment const env;
        QVERIFY(PagesEditor::addScreensaverVideo(env.pages(), "/videos/one.mp4", "One").ok); // no file at all
        const ScreensaverConfig saver = configOf(env.pages()).screensaver;
        QCOMPARE(saver.videos, (QStringList{"/videos/one.mp4"}));
        QVERIFY(!saver.hasGeometry); // so it is not on yet: the measures are the user's
    }

    void theSameFileInAnotherSpellingIsRefused()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), saverFile(R"(["a.mp4", "/videos/b.mp4", "~/c.mp4"])")));
        const QByteArray before = readFile(env.pages());
        const QString folder = QFileInfo(env.pages()).absolutePath();
        for (const QString &again : {QStringLiteral("a.mp4"), folder + "/a.mp4", folder + "/sub/../a.mp4", QStringLiteral("/videos/b.mp4"),
                                     QStringLiteral("/videos//b.mp4"), QDir::homePath() + "/c.mp4"}) {
            const auto result = PagesEditor::addScreensaverVideo(env.pages(), again, "");
            QVERIFY2(!result.ok, qPrintable(again));
            QVERIFY2(result.errors.join(' ').contains("already"), qPrintable(again));
        }
        QCOMPARE(readFile(env.pages()), before);
    }

    void aFullListRefusesAnotherVideo()
    {
        Environment const env;
        QStringList names;
        for (int i = 0; i < MaxScreensaverVideos; ++i)
            names << QStringLiteral("\"v%1.mp4\"").arg(i);
        QVERIFY(writeFile(env.pages(), saverFile("[" + names.join(',') + "]")));
        const QByteArray before = readFile(env.pages());
        const auto result = PagesEditor::addScreensaverVideo(env.pages(), "one-more.mp4", "");
        QVERIFY(!result.ok);
        QVERIFY(result.errors.join(' ').contains(QString::number(MaxScreensaverVideos)));
        QCOMPARE(readFile(env.pages()), before);
    }

    void aBadVideoPathOrTitleIsRefusedAndNothingIsWritten()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), saverFile()));
        const QByteArray before = readFile(env.pages());
        QVERIFY(!PagesEditor::addScreensaverVideo(env.pages(), "", "x").ok);
        QVERIFY(!PagesEditor::addScreensaverVideo(env.pages(), "   ", "x").ok);
        QVERIFY(!PagesEditor::addScreensaverVideo(env.pages(), "two\nlines.mp4", "").ok);
        QVERIFY(!PagesEditor::addScreensaverVideo(env.pages(), QString(MaxVideoPathLength + 1, 'p'), "").ok);
        QVERIFY(!PagesEditor::addScreensaverVideo(env.pages(), "ok.mp4", QString(MaxVideoTitleLength + 1, 't')).ok);
        QCOMPARE(readFile(env.pages()), before);
    }


    void aKeyCanDoAnyOfTheKindsOfAction()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), themesFile()));
        KeySpec app = spec("app");
        app.desktop = "tool.desktop";
        app.label = "  My tool ";
        app.icon = "tool-icon";
        QVERIFY2(PagesEditor::setKey(env.pages(), "music", 0, app).ok, "app");
        KeySpec volume = spec("media");
        volume.command = "volume";
        volume.step = -5;
        QVERIFY(PagesEditor::setKey(env.pages(), "music", 1, volume).ok);
        KeySpec seek = spec("media");
        seek.command = "seek";
        seek.seconds = 30;
        QVERIFY(PagesEditor::setKey(env.pages(), "music", 2, seek).ok);
        KeySpec next = spec("media");
        next.command = "next";
        QVERIFY(PagesEditor::setKey(env.pages(), "music", 3, next).ok);
        KeySpec link = spec("page");
        link.page = "games";
        QVERIFY(PagesEditor::setKey(env.pages(), "music", 4, link).ok);
        QVERIFY(PagesEditor::setKey(env.pages(), "music", 5, spec("back")).ok);
        KeySpec keypad = spec("key");
        keypad.keyName = "kp7";
        QVERIFY(PagesEditor::setKey(env.pages(), "music", 6, keypad).ok);

        const Page music = configOf(env.pages()).pages.value("music");
        const auto &first = music.keys[0];
        QCOMPARE(int(required(first).action.type), int(ActionType::App));
        QCOMPARE(required(first).action.desktop, QStringLiteral("tool.desktop"));
        QCOMPARE(required(first).label, QStringLiteral("My tool"));
        QCOMPARE(required(first).icon.name, QStringLiteral("tool-icon"));
        QCOMPARE(required(music.keys[1]).action.step, -5);
        QCOMPARE(required(music.keys[2]).action.seconds, 30);
        QCOMPARE(int(required(music.keys[3]).action.command), int(MediaCommand::Next));
        QCOMPARE(required(music.keys[4]).action.page, QStringLiteral("games"));
        QCOMPARE(int(required(music.keys[5]).action.type), int(ActionType::Back));
        QCOMPARE(required(music.keys[6]).action.keyName, QStringLiteral("kp7"));
    }

    void aKeyThatIsAskedForBadlyIsRefusedAndNothingIsWritten()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), themesFile()));
        const QByteArray before = readFile(env.pages());
        KeySpec bad = spec("fly");
        QVERIFY(!PagesEditor::setKey(env.pages(), "music", 0, bad).ok);
        bad = spec("app");
        bad.desktop = "not-installed.desktop";
        QVERIFY(!PagesEditor::setKey(env.pages(), "music", 0, bad).ok);
        bad.desktop = "../../x";
        QVERIFY(!PagesEditor::setKey(env.pages(), "music", 0, bad).ok);
        bad = spec("media");
        bad.command = "volume";
        bad.step = 0;
        QVERIFY(!PagesEditor::setKey(env.pages(), "music", 0, bad).ok);
        bad = spec("media");
        bad.command = "fly";
        QVERIFY(!PagesEditor::setKey(env.pages(), "music", 0, bad).ok);
        bad = spec("page");
        bad.page = "no-such-page";
        QVERIFY(!PagesEditor::setKey(env.pages(), "music", 0, bad).ok);
        bad = spec("key");
        bad.keyName = "f13";
        QVERIFY(!PagesEditor::setKey(env.pages(), "music", 0, bad).ok);
        KeySpec longLabel = spec("back");
        longLabel.label = QString(MaxLabelLength + 1, 'x');
        QVERIFY(!PagesEditor::setKey(env.pages(), "music", 0, longLabel).ok);
        QVERIFY(!PagesEditor::setKey(env.pages(), "music", 12, spec("back")).ok);
        QVERIFY(!PagesEditor::setKey(env.pages(), "no-such-page", 0, spec("back")).ok);
        QCOMPARE(readFile(env.pages()), before);
    }

    void theWordsOfAShuffleKeyStayOnlyWhileItIsStillOne()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), themesFile()));
        KeySpec shuffle = spec("media");
        shuffle.command = "shuffle";
        shuffle.label = "Random";
        QVERIFY(PagesEditor::setKey(env.pages(), "main", 0, shuffle).ok);
        QCOMPARE(required(configOf(env.pages()).pages.value("main").keys[0]).stateLabels, (QStringList{"OFF", "ON"}));
        QCOMPARE(required(configOf(env.pages()).pages.value("main").keys[0]).label, QStringLiteral("Random"));
        KeySpec stop = spec("media");
        stop.command = "stop";
        QVERIFY(PagesEditor::setKey(env.pages(), "main", 0, stop).ok);
        QVERIFY(required(configOf(env.pages()).pages.value("main").keys[0]).stateLabels.isEmpty());
        QVERIFY(required(configOf(env.pages()).pages.value("main").keys[0]).icon.name.isEmpty()); // the old icon went too
    }

    void aPlusPageIsMadeForATabWithAKeyThatLeadsToItAndOneThatGoesBack()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), themesFile()));
        QVERIFY(PagesEditor::addSubPage(env.pages(), "music", "music-plus", "More", "Back").ok);
        const PagesConfig config = configOf(env.pages());
        const Page music = config.pages.value("music");
        const Page plus = config.pages.value("music-plus");
        const auto &more = music.keys[11];
        QCOMPARE(int(required(more).action.type), int(ActionType::Page));
        QCOMPARE(required(more).action.page, QStringLiteral("music-plus"));
        QCOMPARE(required(more).label, QStringLiteral("More"));
        const auto &back = plus.keys[11];
        QCOMPARE(int(required(back).action.type), int(ActionType::Back));
        QCOMPARE(required(back).label, QStringLiteral("Back"));
        QVERIFY(!plus.keys[0].has_value()); // the rest is empty
        QVERIFY(config.themes.size() == 3);                              // not a tab of its own
    }

    void theKeyThatLeadsToAPlusPageTakesTheLastFreeOne()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), themesFile()));
        KeySpec taken = spec("back");
        QVERIFY(PagesEditor::setKey(env.pages(), "music", 11, taken).ok); // the 12th is used
        QVERIFY(PagesEditor::setKey(env.pages(), "music", 10, taken).ok);
        QVERIFY(PagesEditor::addSubPage(env.pages(), "music", "music-plus", "More", "Back").ok);
        QCOMPARE(required(configOf(env.pages()).pages.value("music").keys[9]).action.page, QStringLiteral("music-plus"));
        QCOMPARE(int(required(configOf(env.pages()).pages.value("music").keys[11]).action.type), int(ActionType::Back)); // untouched
    }

    void aPlusPageCannotBeMadeWithoutRoomOrOnAPageThatIsThere()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), themesFile()));
        const QByteArray before = readFile(env.pages());
        QVERIFY(!PagesEditor::addSubPage(env.pages(), "music", "games", "More", "Back").ok);      // the name is taken
        QVERIFY(!PagesEditor::addSubPage(env.pages(), "no-such-page", "x", "More", "Back").ok);
        QVERIFY(!PagesEditor::addSubPage(env.pages(), "music", "Not Valid!", "More", "Back").ok);   // not a page name
        QVERIFY(!PagesEditor::addSubPage(env.pages(), "music", "x", QString(MaxLabelLength + 1, 'x'), "Back").ok);
        QCOMPARE(readFile(env.pages()), before);
        for (int key = 0; key < 12; ++key)
            QVERIFY(PagesEditor::setKey(env.pages(), "games", key, spec("back")).ok);
        const QByteArray full = readFile(env.pages());
        const auto result = PagesEditor::addSubPage(env.pages(), "games", "games-plus", "More", "Back");
        QVERIFY(!result.ok);
        QVERIFY(result.errors.join(' ').contains("used"));
        QCOMPARE(readFile(env.pages()), full);
    }

    void aThemeIsAddedWithItsOwnEmptyPage()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), themesFile()));
        QVERIFY(PagesEditor::addTheme(env.pages(), "  Work ", "#00BCD4", "work").ok);
        const PagesConfig config = configOf(env.pages());
        QCOMPARE(config.themes.size(), size_t(4));
        QCOMPARE(config.themes.back().name, QStringLiteral("Work"));
        QCOMPARE(config.themes.back().page, QStringLiteral("work"));
        QCOMPARE(config.themes.back().rgb, 0x00bcd4U);
        QVERIFY(config.pages.contains("work"));
        QVERIFY(!config.pages.value("work").keys[0].has_value());
    }

    void aThemeCannotBeAddedOnAPageThatIsThereOrWithBadWords()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), themesFile()));
        const QByteArray before = readFile(env.pages());
        QVERIFY(!PagesEditor::addTheme(env.pages(), "Work", "#00bcd4", "music").ok);  // the page exists
        QVERIFY(!PagesEditor::addTheme(env.pages(), "Work", "red", "work").ok);       // not a #rrggbb colour
        QVERIFY(!PagesEditor::addTheme(env.pages(), "", "#00bcd4", "work").ok);       // no name
        QVERIFY(!PagesEditor::addTheme(env.pages(), QString(MaxLabelLength + 1, 'x'), "#00bcd4", "work").ok);
        QVERIFY(!PagesEditor::addTheme(env.pages(), "Work", "#00bcd4", "Not Valid!").ok); // not a page name
        QCOMPARE(readFile(env.pages()), before);
    }

    void aThemeCanBeRenamedAndRecoloured()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), themesFile()));
        QVERIFY(PagesEditor::setTheme(env.pages(), 1, " Songs ", "#AABBCC").ok);
        const PagesConfig config = configOf(env.pages());
        QCOMPARE(config.themes[1].name, QStringLiteral("Songs"));
        QCOMPARE(config.themes[1].rgb, 0xaabbccU);
        QCOMPARE(config.themes[1].page, QStringLiteral("music")); // the page does not change
        const QByteArray before = readFile(env.pages());
        QVERIFY(!PagesEditor::setTheme(env.pages(), 1, "Songs", "nope").ok);
        QVERIFY(!PagesEditor::setTheme(env.pages(), 1, "", "#aabbcc").ok);
        QVERIFY(!PagesEditor::setTheme(env.pages(), 9, "Songs", "#aabbcc").ok);
        QCOMPARE(readFile(env.pages()), before);
    }

    void aThemeGoneTakesItsPageWithItUnlessSomethingLeadsThere()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), themesFile()));
        QVERIFY(PagesEditor::removeTheme(env.pages(), 2).ok); // Games: nothing leads to it
        PagesConfig config = configOf(env.pages());
        QCOMPARE(config.themes.size(), size_t(2));
        QVERIFY(!config.pages.contains("games"));

        // Make a page that a key of Music leads to a theme too: removing the theme keeps the page.
        QVERIFY(PagesEditor::addTheme(env.pages(), "Extra", "#123456", "extra").ok);
        KeySpec link = spec("page");
        link.page = "extra";
        QVERIFY(PagesEditor::setKey(env.pages(), "music", 0, link).ok);
        QVERIFY(PagesEditor::removeTheme(env.pages(), 2).ok);
        config = configOf(env.pages());
        QCOMPARE(config.themes.size(), size_t(2));
        QVERIFY(config.pages.contains("extra")); // still reachable from a key
    }

    void aThemeGoneTakesItsPlusPageToo()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), themesFile()));
        QVERIFY(PagesEditor::addSubPage(env.pages(), "games", "games-plus", "More", "Back").ok);
        QVERIFY(configOf(env.pages()).pages.contains("games-plus"));
        QVERIFY(PagesEditor::removeTheme(env.pages(), 2).ok); // Games
        const PagesConfig config = configOf(env.pages());
        QVERIFY(!config.pages.contains("games"));
        QVERIFY(!config.pages.contains("games-plus")); // only it led there
        QVERIFY(config.pages.contains("sub"));         // another tab's sub-page is not touched
    }

    void aPlusPageThatAnotherPageStillLeadsToStays()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), themesFile()));
        QVERIFY(PagesEditor::addSubPage(env.pages(), "games", "games-plus", "More", "Back").ok);
        KeySpec link = spec("page");
        link.page = "games-plus";
        QVERIFY(PagesEditor::setKey(env.pages(), "music", 0, link).ok); // Music leads there too
        QVERIFY(PagesEditor::removeTheme(env.pages(), 2).ok);
        QVERIFY(configOf(env.pages()).pages.contains("games-plus"));
    }

    void theHomeThemeAndTheLastThemeCannotBeRemoved()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), themesFile()));
        const QByteArray before = readFile(env.pages());
        QVERIFY(!PagesEditor::removeTheme(env.pages(), 0).ok); // opens the home page
        QVERIFY(!PagesEditor::removeTheme(env.pages(), 7).ok);
        QVERIFY(!PagesEditor::removeTheme(env.pages(), -1).ok);
        QCOMPARE(readFile(env.pages()), before);
        QVERIFY(PagesEditor::removeTheme(env.pages(), 2).ok);
        QVERIFY(PagesEditor::removeTheme(env.pages(), 1).ok);
        QVERIFY(!PagesEditor::removeTheme(env.pages(), 0).ok); // the last one, and home
        QCOMPARE(configOf(env.pages()).themes.size(), size_t(1));
    }

    void themesCanBeMovedWithinTheList()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), themesFile()));
        QVERIFY(PagesEditor::moveTheme(env.pages(), 2, -1).ok);
        PagesConfig config = configOf(env.pages());
        QCOMPARE(config.themes[1].name, QStringLiteral("Games"));
        QCOMPARE(config.themes[2].name, QStringLiteral("Music"));
        QVERIFY(PagesEditor::moveTheme(env.pages(), 0, 2).ok);
        config = configOf(env.pages());
        QCOMPARE(config.themes[2].name, QStringLiteral("Control"));
        const QByteArray before = readFile(env.pages());
        QVERIFY(!PagesEditor::moveTheme(env.pages(), 0, -1).ok);
        QVERIFY(!PagesEditor::moveTheme(env.pages(), 2, 1).ok);
        QVERIFY(!PagesEditor::moveTheme(env.pages(), 5, 1).ok);
        QCOMPARE(readFile(env.pages()), before);
    }

    void aVideoThatIsNotThereIsRefusedAndNothingIsWritten()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), saverFile()));
        const QByteArray before = readFile(env.pages());
        for (const int index : {-1, 3, 99}) {
            QVERIFY2(!PagesEditor::removeScreensaverVideo(env.pages(), index).ok, qPrintable(QString::number(index)));
            QVERIFY2(!PagesEditor::setScreensaverVideoTitle(env.pages(), index, "x").ok, qPrintable(QString::number(index)));
        }
        QCOMPARE(readFile(env.pages()), before);
    }

    void aFileWithoutScreensaverGetsOneForASetting()
    {
        Environment const env;
        QVERIFY(PagesEditor::setScreensaverIdleMinutes(env.pages(), 8).ok); // no file at all: the built-in pages
        const ScreensaverConfig saver = configOf(env.pages()).screensaver;
        QCOMPARE(saver.idleMinutes, 8);
        QVERIFY(saver.videos.isEmpty());
        QVERIFY(!PagesEditor::removeScreensaverVideo(env.pages(), 0).ok); // nothing to remove
    }

    void aFileThatIsAlreadyInvalidIsLeftAlone()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), R"({"version": 1, "pages": {"main": {"keys": [{"key": 99}]}}})"));
        const QByteArray before = readFile(env.pages());
        const auto result = PagesEditor::setScreensaverFps(env.pages(), 10);
        QVERIFY(!result.ok);
        QCOMPARE(readFile(env.pages()), before);
    }
};

QTEST_GUILESS_MAIN(TestPagesEditor)
#include "test_pageseditor.moc"
