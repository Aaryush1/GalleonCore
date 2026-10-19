// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

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

} // namespace

class TestPagesEditor : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aMissingFileStartsFromTheBuiltInPages()
    {
        Environment env;
        const auto result = PagesEditor::assignApp(env.pages(), "main", 4, "tool.desktop");
        QVERIFY2(result.ok, qPrintable(result.errors.join('\n')));
        const PagesConfig config = configOf(env.pages());
        const Page main = config.pages.value("main");
        const auto &key = main.keys[4];
        QVERIFY(key.has_value());
        QCOMPARE(int(key->action.type), int(ActionType::App));
        QCOMPARE(key->action.desktop, QStringLiteral("tool.desktop"));
        QVERIFY(key->label.isEmpty()); // the application's own label and icon are used
        QVERIFY(config.pages.value("main").keys[0].has_value()); // the other keys are still there
        QVERIFY(config.dials[0].turn.type == ActionType::Media);
    }

    void aKeyThatWasUsedIsReplacedWithoutItsOldLabel()
    {
        Environment env;
        QVERIFY(PagesEditor::assignApp(env.pages(), "main", 5, "tool.desktop").ok); // key 5 was "Vol +"
        const PagesConfig config = configOf(env.pages());
        const Page main = config.pages.value("main");
        const auto &key = main.keys[5];
        QCOMPARE(int(key->action.type), int(ActionType::App));
        QVERIFY(key->label.isEmpty());
        QVERIFY(key->icon.name.isEmpty());
        QCOMPARE(int(config.pages.value("main").keys[3]->action.command), int(MediaCommand::Volume)); // untouched
    }

    void everyKeyKeepsItsPlaceAndTheListStaysSorted()
    {
        Environment env;
        QVERIFY(PagesEditor::assignApp(env.pages(), "main", 9, "tool.desktop").ok);
        QVERIFY(PagesEditor::assignApp(env.pages(), "main", 4, "other.desktop").ok);
        const QJsonObject root = QJsonDocument::fromJson(readFile(env.pages())).object();
        const QJsonArray keys = root.value("pages").toObject().value("main").toObject().value("keys").toArray();
        int previous = -1;
        for (const QJsonValue &value : keys) {
            const int number = value.toObject().value("key").toInt();
            QVERIFY(number > previous);
            previous = number;
        }
        QCOMPARE(configOf(env.pages()).pages.value("main").keys[9]->action.desktop, QStringLiteral("tool.desktop"));
        QCOMPARE(configOf(env.pages()).pages.value("main").keys[4]->action.desktop, QStringLiteral("other.desktop"));
    }

    void clearingEmptiesTheKey()
    {
        Environment env;
        QVERIFY(PagesEditor::clearKey(env.pages(), "main", 0).ok);
        const PagesConfig config = configOf(env.pages());
        QVERIFY(!config.pages.value("main").keys[0].has_value());
        QVERIFY(config.pages.value("main").keys[1].has_value());
        QVERIFY(PagesEditor::clearKey(env.pages(), "main", 0).ok); // already empty: still fine
    }

    void theOldFileIsKeptAsABackup()
    {
        Environment env;
        QVERIFY(PagesEditor::assignApp(env.pages(), "main", 4, "tool.desktop").ok); // creates the file
        const QByteArray first = readFile(env.pages());
        QVERIFY(!QFile::exists(env.pages() + ".bak")); // nothing to back up yet
        QVERIFY(PagesEditor::assignApp(env.pages(), "main", 7, "other.desktop").ok);
        QCOMPARE(readFile(env.pages() + ".bak"), first);
        QVERIFY(readFile(env.pages()) != first);
    }

    void thePermissionsAreKeptAndNeverWorldWritable()
    {
        Environment env;
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
        Environment env;
        const QByteArray broken = "{ \"version\": 1, \"home\": \"main\", \"pages\": { \"main\": { \"keys\": [ { \"key\": 99 } ] } } }";
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
        Environment env;
        const QString path = env.dir.filePath("system/applications/tool.desktop");
        QVERIFY(PagesEditor::assignApp(env.pages(), "main", 4, path).ok);
        QCOMPARE(configOf(env.pages()).pages.value("main").keys[4]->action.desktop, path);
    }

    void aSecondPageCanBeEdited()
    {
        Environment env;
        QVERIFY(writeFile(env.pages(), "{ \"version\": 1, \"home\": \"main\", \"pages\": { \"main\": { \"keys\": [] }, \"apps\": { \"keys\": [] } } }"));
        QVERIFY(PagesEditor::assignApp(env.pages(), "apps", 0, "tool.desktop").ok);
        const PagesConfig config = configOf(env.pages());
        QVERIFY(config.pages.value("apps").keys[0].has_value());
        QVERIFY(!config.pages.value("main").keys[0].has_value());
    }

    void theScreenSectionAndDialsSurviveAnEdit()
    {
        Environment env;
        QVERIFY(writeFile(env.pages(), "{ \"version\": 1, \"home\": \"main\", \"pages\": { \"main\": { \"keys\": [] } },"
                                       " \"dials\": { \"1\": { \"press\": { \"type\": \"media\", \"command\": \"next\" } } },"
                                       " \"screen\": { \"now_playing\": \"playing-or-paused\", \"video_thumbnails\": false } }"));
        QVERIFY(PagesEditor::assignApp(env.pages(), "main", 2, "tool.desktop").ok);
        const PagesConfig config = configOf(env.pages());
        QCOMPARE(int(config.dials[1].press.command), int(MediaCommand::Next));
        QCOMPARE(int(config.screen.nowPlaying), int(NowPlayingMode::PlayingOrPaused));
        QVERIFY(!config.screen.videoThumbnails);
    }
};

QTEST_GUILESS_MAIN(TestPagesEditor)
#include "test_pageseditor.moc"
