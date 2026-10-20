// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BosunBackend.h"
#include "Required.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

// Two themes and two pages: the first has an application, a media key and a page key.
QByteArray twoThemes()
{
    return R"({
  "version": 1,
  "home": "main",
  "themes": [
    {"name": "Control", "page": "main", "color": "#ffd600"},
    {"name": "Music", "page": "music", "color": "#e53935"}
  ],
  "pages": {
    "main": {"keys": [
      {"key": 0, "label": "Play", "action": {"type": "media", "command": "play-pause"}},
      {"key": 1, "action": {"type": "app", "desktop": "nothing-like-this.desktop"}},
      {"key": 2, "label": "More", "action": {"type": "page", "page": "music"}}
    ]},
    "music": {"keys": [
      {"key": 11, "label": "Back", "action": {"type": "back"}}
    ], "steam": {"max": 4}}
  }
})";
}

// A program that prints what ffprobe would, in place of ffprobe.
QString fakeProbe(const QString &dir, const QByteArray &json)
{
    QString script = dir + QStringLiteral("/fake-ffprobe");
    QFile file(script);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write("#!/bin/sh\ncat <<'EOF'\n" + json + "\nEOF\n");
    file.close();
    QFile::setPermissions(script, QFile::permissions(script) | QFile::ExeUser);
    return script;
}

QByteArray saverPages(const QString &videos, const QString &geometry = QStringLiteral(
                                                 R"("geometry": {"screen_mm": [62.6, 33.0], "key_mm": [13.7, 13.7], "gap_mm": [5.4, 5.4], "screen_to_keys_mm": 5.4},)"))
{
    return (QStringLiteral(R"({"version": 1, "home": "main", "pages": {"main": {"keys": []}}, "screensaver": {)") + geometry
            + QStringLiteral(R"("idle_minutes": 3, "fps": 12, "while_playing": true, "video": [)") + videos + QStringLiteral("]}}"))
        .toUtf8();
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size();
}

class TestBosun : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void themesAreTheTabs()
    {
        const QTemporaryDir dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, twoThemes()));
        const bosun::BosunBackend backend(file);
        QVERIFY2(backend.problem().isEmpty(), qPrintable(backend.problem()));
        const QVariantList themes = backend.themes();
        QCOMPARE(themes.size(), 2);
        QCOMPARE(themes.at(0).toMap().value("name").toString(), QStringLiteral("Control"));
        QCOMPARE(themes.at(1).toMap().value("page").toString(), QStringLiteral("music"));
        QCOMPARE(themes.at(1).toMap().value("color").toString(), QStringLiteral("#e53935"));
    }

    void aPageHasTwelveKeysAndSaysWhatTheyDo()
    {
        const QTemporaryDir dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, twoThemes()));
        const bosun::BosunBackend backend(file);
        const QVariantList keys = backend.keys("main");
        QCOMPARE(keys.size(), 12);
        const QVariantMap play = keys.at(0).toMap();
        QCOMPARE(play.value("kind").toString(), QStringLiteral("media"));
        QCOMPARE(play.value("label").toString(), QStringLiteral("Play"));
        QCOMPARE(play.value("detail").toString(), QStringLiteral("play / pause"));
        QVERIFY(!play.value("empty").toBool());
        // An application that is not installed is shown by the name of its file.
        const QVariantMap app = keys.at(1).toMap();
        QCOMPARE(app.value("kind").toString(), QStringLiteral("app"));
        QCOMPARE(app.value("label").toString(), QStringLiteral("nothing-like-this.desktop"));
        QCOMPARE(keys.at(2).toMap().value("detail").toString(), QStringLiteral("music"));
        QVERIFY(keys.at(3).toMap().value("empty").toBool());
        QCOMPARE(keys.at(3).toMap().value("kind").toString(), QStringLiteral("empty"));
        QVERIFY(backend.keys("no-such-page").isEmpty());
    }

    void whatTheModuleAddsIsSaid()
    {
        const QTemporaryDir dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, twoThemes()));
        const bosun::BosunBackend backend(file);
        QVERIFY(backend.autoFill("main").isEmpty());
        QVERIFY(backend.autoFill("music").contains("Steam"));
        QVERIFY(backend.autoFill("no-such-page").isEmpty());
    }

    void everyKeyHasItsPicture()
    {
        const QTemporaryDir dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, twoThemes()));
        const bosun::BosunBackend backend(file);
        for (const int key : {0, 1, 3, 11}) {
            const QPixmap tile = backend.tile("main", key);
            QCOMPARE(tile.size(), QSize(160, 160));
        }
        // A key with a label is not drawn like an empty one.
        QVERIFY(backend.tile("main", 0).toImage() != backend.tile("main", 3).toImage());
        // Nothing to show: an empty key, never a crash or a null picture.
        QCOMPARE(backend.tile("no-such-page", 0).size(), QSize(160, 160));
        QCOMPARE(backend.tile("main", 12).size(), QSize(160, 160));
        QCOMPARE(backend.tile("main", -1).size(), QSize(160, 160));
    }

    void theProviderReadsPageAndKeyFromTheUrl()
    {
        const QTemporaryDir dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, twoThemes()));
        const bosun::BosunBackend backend(file);
        bosun::TileProvider provider(&backend);
        QSize size;
        const QPixmap labelled = provider.requestPixmap("main/0?r=3", &size, {});
        QCOMPARE(size, QSize(160, 160));
        QVERIFY(labelled.toImage() == backend.tile("main", 0).toImage());
        // Bad identifiers give an empty key.
        for (const QString &id : {QString(), QStringLiteral("main"), QStringLiteral("main/x"), QStringLiteral("/3"), QStringLiteral("main/99")}) {
            const QPixmap blank = provider.requestPixmap(id, &size, {});
            QCOMPARE(size, QSize(160, 160));
            QVERIFY(blank.toImage() == backend.tile("main", 3).toImage() || blank.toImage() == backend.tile("no-such-page", 0).toImage());
        }
    }

    void aBrokenFileIsReportedAndShowsNothing()
    {
        const QTemporaryDir dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, R"({"version": 1, "home": "main", "pages": {"main": {"keys": [{"key": 99}]}}})"));
        const bosun::BosunBackend backend(file);
        QVERIFY(!backend.problem().isEmpty());
        QVERIFY(backend.themes().isEmpty());
        QVERIFY(backend.keys("main").isEmpty());
        QCOMPARE(backend.tile("main", 0).size(), QSize(160, 160));
    }

    void aMissingFileShowsTheBuiltInPages()
    {
        const QTemporaryDir dir;
        const bosun::BosunBackend backend(dir.filePath("none.json"));
        QVERIFY2(backend.problem().isEmpty(), qPrintable(backend.problem()));
        QCOMPARE(backend.keys("main").size(), 12);
    }

    void aChangeOnDiskIsSeen()
    {
        const QTemporaryDir dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, twoThemes()));
        const bosun::BosunBackend backend(file);
        const int before = backend.revision();
        QSignalSpy changed(&backend, &bosun::BosunBackend::changed);
        QVERIFY(writeFile(file, R"({"version": 1, "home": "main", "pages": {"main": {"keys": []}}})"));
        QVERIFY(changed.wait(3000));
        QVERIFY(backend.revision() > before);
        QVERIFY(backend.themes().isEmpty());
        // The file replaced as an editor does (new file, then renamed over): still watched.
        QVERIFY(writeFile(file + ".new", twoThemes()));
        QVERIFY(QFile::remove(file));
        QVERIFY(QFile::rename(file + ".new", file));
        changed.clear();
        QTRY_VERIFY_WITH_TIMEOUT(backend.themes().size() == 2, 3000);
    }

    void theScreensaverIsOffWithoutGeometryOrVideo()
    {
        const QTemporaryDir dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, twoThemes()));
        const bosun::BosunBackend nothing(file, {.ffprobe = "/nonexistent", .ffmpeg = "/nonexistent", .thumbnailFolder = dir.filePath("thumbs")});
        QVERIFY(!nothing.screensaver().value("enabled").toBool());
        QVERIFY(!nothing.screensaver().value("hasGeometry").toBool());
        QVERIFY(nothing.screensaver().value("videos").toList().isEmpty());
        QVERIFY(nothing.screensaver().value("canvas").toString().isEmpty());

        // Geometry but no video: still off, and the size of the picture is known.
        QVERIFY(writeFile(file, R"({"version": 1, "home": "main", "pages": {"main": {"keys": []}}, "screensaver": {
            "geometry": {"screen_mm": [62.6, 33.0], "key_mm": [13.7, 13.7], "gap_mm": [5.4, 5.4], "screen_to_keys_mm": 5.4}}})"));
        const bosun::BosunBackend noVideo(file, {.ffprobe = "/nonexistent", .ffmpeg = "/nonexistent", .thumbnailFolder = dir.filePath("thumbs")});
        QVERIFY(noVideo.screensaver().value("hasGeometry").toBool());
        QVERIFY(!noVideo.screensaver().value("enabled").toBool());
        QCOMPARE(noVideo.screensaver().value("canvas").toString(), QStringLiteral("720 × 1258"));
    }

    void theSettingsAndTheVideosAreListed()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("night.mp4"), "not really a video"));
        QVERIFY(writeFile(dir.filePath("notes.txt"), "not a video at all"));
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, saverPages(R"({"file": "night.mp4", "title": "Night"}, {"file": "gone.mp4"}, {"file": "notes.txt"})")));
        const bosun::BosunBackend backend(file, {.ffprobe = "/nonexistent", .ffmpeg = "/nonexistent", .thumbnailFolder = dir.filePath("thumbs")});
        const QVariantMap saver = backend.screensaver();
        QVERIFY(saver.value("enabled").toBool());
        QCOMPARE(saver.value("idleMinutes").toInt(), 3);
        QCOMPARE(saver.value("fps").toInt(), 12);
        QVERIFY(saver.value("whilePlaying").toBool());
        const QVariantList videos = saver.value("videos").toList();
        QCOMPARE(videos.size(), 3);
        const QVariantMap first = videos.at(0).toMap();
        QCOMPARE(first.value("title").toString(), QStringLiteral("Night"));
        QCOMPARE(first.value("name").toString(), QStringLiteral("night.mp4"));
        QVERIFY(first.value("found").toBool());
        QVERIFY(first.value("notes").toStringList().isEmpty()); // without ffprobe nothing is known, so nothing to say
        QVERIFY(saver.value("probeMissing").toBool());
        // A missing file and a file that is not a video are said, not hidden.
        for (const int index : {1, 2}) {
            const QVariantMap missing = videos.at(index).toMap();
            QVERIFY(!missing.value("found").toBool());
            QCOMPARE(missing.value("notes").toStringList().size(), 1);
        }
    }

    void whatFfprobeSaysIsShownWithItsWarnings()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("tall.mp4"), "x"));
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, saverPages(R"({"file": "tall.mp4"})")));
        // 640 x 640 and 10 fps: square for a tall picture, below the 720 px of the screen, slower than 12.
        const QString probe = fakeProbe(dir.path(),
                                        R"({"streams": [{"codec_name": "h264", "width": 640, "height": 640, "avg_frame_rate": "10/1"}], "format": {"duration": "75.5"}})");
        const bosun::BosunBackend backend(file, {.ffprobe = probe, .ffmpeg = "/nonexistent", .thumbnailFolder = dir.filePath("thumbs")});
        QTRY_VERIFY_WITH_TIMEOUT(!backend.screensaver().value("videos").toList().at(0).toMap().value("details").toString().isEmpty(), 8000);
        const QVariantMap video = backend.screensaver().value("videos").toList().at(0).toMap();
        QCOMPARE(video.value("details").toString(), QStringLiteral("640 × 640 · 10.0 fps · 1:16 · h264"));
        const QString notes = video.value("notes").toStringList().join('\n');
        QVERIFY2(notes.contains("cut off at the sides"), qPrintable(notes));
        QVERIFY2(notes.contains("Smaller than the screen"), qPrintable(notes));
        QVERIFY2(notes.contains("fewer images per second"), qPrintable(notes));
    }

    void aVideoThatFfprobeCannotReadIsSaid()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("bad.mp4"), "x"));
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, saverPages(R"({"file": "bad.mp4"})")));
        const QString probe = fakeProbe(dir.path(), "this is not what ffprobe prints");
        const bosun::BosunBackend backend(file, {.ffprobe = probe, .ffmpeg = "/nonexistent", .thumbnailFolder = dir.filePath("thumbs")});
        QTRY_VERIFY_WITH_TIMEOUT(!backend.screensaver().value("videos").toList().at(0).toMap().value("notes").toStringList().isEmpty(), 8000);
        QVERIFY(backend.screensaver().value("videos").toList().at(0).toMap().value("notes").toStringList().at(0).contains("ffprobe"));
    }

    void theScreensaverTabEditsTheFileAndShowsTheResult()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("a.mp4"), "x"));
        QVERIFY(writeFile(dir.filePath("b.mp4"), "x"));
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, saverPages(R"("a.mp4", {"file": "b.mp4", "title": "Second"})")));
        bosun::BosunBackend backend(file, {.ffprobe = "/nonexistent/none", .ffmpeg = "/nonexistent/none", .thumbnailFolder = dir.filePath("thumbs")});

        QVERIFY(backend.setIdleMinutes(30).isEmpty());
        QVERIFY(backend.setFps(10).isEmpty());
        QVERIFY(backend.setWhilePlaying(false).isEmpty());
        QCOMPARE(backend.screensaver().value("idleMinutes").toInt(), 30); // read again at once, not after the file watch
        QCOMPARE(backend.screensaver().value("fps").toInt(), 10);
        QVERIFY(!backend.screensaver().value("whilePlaying").toBool());

        QVERIFY(backend.setVideoTitle(0, "First").isEmpty());
        QCOMPARE(backend.screensaver().value("videos").toList().at(0).toMap().value("title").toString(), QStringLiteral("First"));

        QVERIFY(backend.removeVideo(0).isEmpty());
        const QVariantList left = backend.screensaver().value("videos").toList();
        QCOMPARE(left.size(), 1);
        QCOMPARE(left.at(0).toMap().value("title").toString(), QStringLiteral("Second"));
        QVERIFY(QFile::exists(dir.filePath("a.mp4"))); // the video file itself is never touched
        QVERIFY(backend.removeVideo(0).isEmpty());
        QVERIFY(!backend.screensaver().value("enabled").toBool());
    }

    void aRefusedEditSaysWhyAndChangesNothing()
    {
        const QTemporaryDir dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, saverPages(R"("a.mp4")")));
        bosun::BosunBackend backend(file, {.ffprobe = "/nonexistent/none", .ffmpeg = "/nonexistent/none", .thumbnailFolder = dir.filePath("thumbs")});
        const int before = backend.revision();
        QVERIFY(!backend.setFps(99).isEmpty());
        QVERIFY(!backend.setIdleMinutes(0).isEmpty());
        QVERIFY(!backend.removeVideo(7).isEmpty());
        QCOMPARE(backend.screensaver().value("fps").toInt(), 12);
        QCOMPARE(backend.revision(), before); // not even read again
    }

    void theWindowHasATabPerThemeAndTheFixedOnes()
    {
        const QTemporaryDir dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, twoThemes()));
        const bosun::BosunBackend backend(file);
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("tiles"), new bosun::TileProvider(&backend));
        engine.setInitialProperties({{QStringLiteral("backend"), QVariant::fromValue(&backend)}});
        engine.load(QUrl(QStringLiteral("qrc:/bosun/qml/Bosun.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        QObject *window = engine.rootObjects().constFirst();
        QCOMPARE(window->property("tabs").toList().size(), 2 + 3); // themes, keyboard, wallpapers, screensavers
        QCOMPARE(window->property("keys").toList().size(), 12);
        QVERIFY(window->property("selected").toMap().isEmpty()); // nothing chosen yet
        window->setProperty("selectedKey", 0);
        QCOMPARE(window->property("selected").toMap().value("label").toString(), QStringLiteral("Play"));
        // Another tab forgets the selection; a fixed tab has no keys.
        window->setProperty("currentTab", 1);
        QCOMPARE(window->property("selectedKey").toInt(), -1);
        QCOMPARE(window->property("keys").toList().size(), 12);
        window->setProperty("currentTab", 4);
        QVERIFY(window->property("keys").toList().isEmpty());
        // A window that is not shown has no picture, but a shown one grabs: the pictures load.
        window->setProperty("currentTab", 0);
        auto *quick = qobject_cast<QQuickWindow *>(window);
        QVERIFY(quick);
        QVERIFY(!quick->grabWindow().isNull());
    }
};

} // namespace

int main(int argc, char *argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    const QGuiApplication app(argc, argv);
    TestBosun test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_bosun.moc"
