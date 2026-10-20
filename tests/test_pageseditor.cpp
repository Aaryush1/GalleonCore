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
