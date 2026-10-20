// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BosunBackend.h"
#include "DaemonStatus.h"
#include "Required.h"

#include <AppsBackend.h>

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

// A program in place of FFmpeg: lists an H.264 encoder, and for a conversion runs `body`
// (the last argument is the file to write; its arguments are kept in `<file>.args`).
QString fakeFfmpeg(const QString &dir, const QString &body)
{
    QString script = dir + QStringLiteral("/fake-ffmpeg");
    QFile file(script);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(("#!/bin/sh\ncase \"$*\" in\n  *-encoders*) printf ' V....D libx264 libx264 H.264\\n'; exit 0;;\nesac\n"
                "for last; do :; done\nfor a in \"$@\"; do echo \"$a\" >> \"$last.args\"; done\n" + body + "\n").toUtf8());
    file.close();
    QFile::setPermissions(script, QFile::permissions(script) | QFile::ExeUser);
    return script;
}

QByteArray wideProbe()
{
    return R"({"streams": [{"codec_name": "h264", "width": 1920, "height": 1080, "avg_frame_rate": "24/1"}], "format": {"duration": "12"}})";
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

    void aChosenFileThatIsNotAVideoIsSaidAndCannotBeAdded()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("notes.txt"), "text"));
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, saverPages(R"("a.mp4")")));
        bosun::BosunBackend backend(file, {.ffprobe = "/nonexistent/none", .ffmpeg = "/nonexistent/none", .thumbnailFolder = dir.filePath("thumbs")});
        QVERIFY(backend.candidate().isEmpty());
        for (const QString &chosen : {dir.filePath("notes.txt"), dir.filePath("missing.mp4"), QString("file://") + dir.filePath("notes.txt")}) {
            backend.inspect(chosen);
            const QVariantMap candidate = backend.candidate();
            QCOMPARE(candidate.value("state").toString(), QStringLiteral("unusable"));
            QVERIFY(!candidate.value("canAdd").toBool());
            QVERIFY(!candidate.value("notes").toStringList().isEmpty());
            QVERIFY(!backend.addCandidate("x").isEmpty());
        }
        QCOMPARE(backend.screensaver().value("videos").toList().size(), 1); // nothing was added
    }

    void aChosenVideoIsLookedAtThenAddedAsItIs()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("Night Rain.mp4"), "x"));
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, saverPages(R"("a.mp4")")));
        // Wide (1920 x 1080), 24 fps: the sides will be cut.
        const QString probe = fakeProbe(dir.path(),
                                        R"({"streams": [{"codec_name": "h264", "width": 1920, "height": 1080, "avg_frame_rate": "24/1"}], "format": {"duration": "12"}})");
        bosun::BosunBackend backend(file, {.ffprobe = probe, .ffmpeg = "/nonexistent/none", .thumbnailFolder = dir.filePath("thumbs")});
        backend.inspect(QUrl::fromLocalFile(dir.filePath("Night Rain.mp4")).toString()); // as a file dialog gives it
        QCOMPARE(backend.candidate().value("state").toString(), QStringLiteral("checking"));
        QVERIFY(!backend.candidate().value("canAdd").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(backend.candidate().value("state").toString(), QStringLiteral("ready"), 8000);
        const QVariantMap candidate = backend.candidate();
        QCOMPARE(candidate.value("details").toString(), QStringLiteral("1920 × 1080 · 24.0 fps · 12 s · h264"));
        QVERIFY(candidate.value("notes").toStringList().join('\n').contains("at the sides"));
        QVERIFY(candidate.value("canAdd").toBool());
        QCOMPARE(candidate.value("suggestedTitle").toString(), QStringLiteral("Night Rain"));

        QVERIFY(backend.addCandidate("").isEmpty()); // no title given: the name of the file
        QVERIFY(backend.candidate().isEmpty());
        const QVariantList videos = backend.screensaver().value("videos").toList();
        QCOMPARE(videos.size(), 2);
        QCOMPARE(videos.at(1).toMap().value("title").toString(), QStringLiteral("Night Rain"));
        QCOMPARE(videos.at(1).toMap().value("file").toString(), dir.filePath("Night Rain.mp4")); // where it is, not a copy
    }

    void aVideoAlreadyInTheListOrAFullListCannotBeAdded()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("a.mp4"), "x"));
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, saverPages(R"("a.mp4")")));
        bosun::BosunBackend backend(file, {.ffprobe = "/nonexistent/none", .ffmpeg = "/nonexistent/none", .thumbnailFolder = dir.filePath("thumbs")});
        backend.inspect(dir.filePath("a.mp4"));
        QVERIFY(!backend.candidate().value("canAdd").toBool());
        QVERIFY(backend.candidate().value("why").toString().contains("already"));
        QVERIFY(!backend.addCandidate("again").isEmpty());

        QStringList names;
        for (int i = 0; i < galleond::MaxScreensaverVideos; ++i)
            names << QStringLiteral(R"("v%1.mp4")").arg(i);
        QVERIFY(writeFile(file, saverPages(names.join(','))));
        backend.reload();
        QVERIFY(writeFile(dir.filePath("new.mp4"), "x"));
        backend.inspect(dir.filePath("new.mp4"));
        QVERIFY(!backend.candidate().value("canAdd").toBool());
        QVERIFY(backend.candidate().value("why").toString().contains("full"));
    }

    void aVideoThatFfprobeCannotReadCannotBeAdded()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("bad.mp4"), "x"));
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, saverPages(R"("a.mp4")")));
        const QString probe = fakeProbe(dir.path(), "not what ffprobe prints");
        bosun::BosunBackend backend(file, {.ffprobe = probe, .ffmpeg = "/nonexistent/none", .thumbnailFolder = dir.filePath("thumbs")});
        backend.inspect(dir.filePath("bad.mp4"));
        QTRY_COMPARE_WITH_TIMEOUT(backend.candidate().value("state").toString(), QStringLiteral("unusable"), 8000);
        QVERIFY(!backend.candidate().value("canAdd").toBool());
        QVERIFY(!backend.addCandidate("x").isEmpty());
    }

    void theChoiceCanBeDropped()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("a.mp4"), "x"));
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, saverPages(R"("b.mp4")")));
        bosun::BosunBackend backend(file, {.ffprobe = "/nonexistent/none", .ffmpeg = "/nonexistent/none", .thumbnailFolder = dir.filePath("thumbs")});
        backend.inspect(dir.filePath("a.mp4"));
        QVERIFY(!backend.candidate().isEmpty());
        backend.clearCandidate();
        QVERIFY(backend.candidate().isEmpty());
        QVERIFY(!backend.addCandidate("x").isEmpty()); // nothing chosen
    }

    void aConversionCutsTheVideoAddsTheResultAndKeepsTheOriginal()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("Night Rain.mp4"), "original"));
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, saverPages(R"("a.mp4")")));
        bosun::BosunBackend backend(file, {.ffprobe = fakeProbe(dir.path(), wideProbe()),
                                           .ffmpeg = fakeFfmpeg(dir.path(), "echo out_time_us=6000000; printf converted > \"$last\"; exit 0"),
                                           .thumbnailFolder = dir.filePath("thumbs"),
                                           .convertedFolder = dir.filePath("converted")});
        backend.inspect(dir.filePath("Night Rain.mp4"));
        QTRY_COMPARE_WITH_TIMEOUT(backend.candidate().value("state").toString(), QStringLiteral("ready"), 8000);
        // A rectangle of other proportions: it is made valid before FFmpeg sees it (500 wide, 874 high).
        QVERIFY(backend.startConversion(100, 0, 500, 900, "").isEmpty());
        QCOMPARE(backend.conversion().value("state").toString(), QStringLiteral("running"));
        QTRY_COMPARE_WITH_TIMEOUT(backend.conversion().value("state").toString(), QStringLiteral("done"), 8000);

        const QString result = dir.filePath("converted/Night Rain.mp4");
        QCOMPARE(backend.conversion().value("message").toString(), result);
        const QVariantList videos = backend.screensaver().value("videos").toList();
        QCOMPARE(videos.size(), 2);
        QCOMPARE(videos.at(1).toMap().value("file").toString(), result);
        QCOMPARE(videos.at(1).toMap().value("title").toString(), QStringLiteral("Night Rain"));
        QVERIFY(backend.candidate().isEmpty());
        QFile original(dir.filePath("Night Rain.mp4"));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("original")); // the source is never touched
        QFile arguments(result + ".partial.args");
        QVERIFY(arguments.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(arguments.readAll());
        QVERIFY2(text.contains("crop=500:874:100:0,scale=720:1258:flags=lanczos,fps=12"), qPrintable(text));
    }

    void aConversionThatFailsSaysWhyAndAddsNothing()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("v.mp4"), "x"));
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, saverPages(R"("a.mp4")")));
        bosun::BosunBackend backend(file, {.ffprobe = fakeProbe(dir.path(), wideProbe()),
                                           .ffmpeg = fakeFfmpeg(dir.path(), "echo 'Invalid data' >&2; exit 1"),
                                           .thumbnailFolder = dir.filePath("thumbs"), .convertedFolder = dir.filePath("converted")});
        backend.inspect(dir.filePath("v.mp4"));
        QTRY_COMPARE_WITH_TIMEOUT(backend.candidate().value("state").toString(), QStringLiteral("ready"), 8000);
        QVERIFY(backend.startConversion(651, 0, 618, 1080, "T").isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(backend.conversion().value("state").toString(), QStringLiteral("failed"), 8000);
        QVERIFY(backend.conversion().value("message").toString().contains("Invalid data"));
        QCOMPARE(backend.screensaver().value("videos").toList().size(), 1);
        QVERIFY(!backend.candidate().isEmpty()); // still there: the user can try again
    }

    void aConversionCanBeCancelled()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("v.mp4"), "x"));
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, saverPages(R"("a.mp4")")));
        bosun::BosunBackend backend(file, {.ffprobe = fakeProbe(dir.path(), wideProbe()), .ffmpeg = fakeFfmpeg(dir.path(), "sleep 30"),
                                           .thumbnailFolder = dir.filePath("thumbs"), .convertedFolder = dir.filePath("converted")});
        backend.inspect(dir.filePath("v.mp4"));
        QTRY_COMPARE_WITH_TIMEOUT(backend.candidate().value("state").toString(), QStringLiteral("ready"), 8000);
        QVERIFY(backend.startConversion(651, 0, 618, 1080, "T").isEmpty());
        QVERIFY(!backend.startConversion(651, 0, 618, 1080, "T").isEmpty()); // one at a time
        backend.cancelConversion();
        QTRY_COMPARE_WITH_TIMEOUT(backend.conversion().value("state").toString(), QStringLiteral("cancelled"), 8000);
        QCOMPARE(backend.screensaver().value("videos").toList().size(), 1);
    }

    void nothingIsConvertedWhenThereIsNothingToConvertOrNoRoom()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("v.mp4"), "x"));
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, saverPages(R"("a.mp4")")));
        bosun::BosunBackend backend(file, {.ffprobe = fakeProbe(dir.path(), wideProbe()), .ffmpeg = fakeFfmpeg(dir.path(), "exit 0"),
                                           .thumbnailFolder = dir.filePath("thumbs"), .convertedFolder = dir.filePath("converted")});
        QVERIFY(!backend.startConversion(0, 0, 100, 100, "x").isEmpty()); // nothing chosen
        QStringList names;
        for (int i = 0; i < galleond::MaxScreensaverVideos; ++i)
            names << QStringLiteral(R"("v%1.mp4")").arg(i);
        QVERIFY(writeFile(file, saverPages(names.join(','))));
        backend.reload();
        backend.inspect(dir.filePath("v.mp4"));
        QTRY_COMPARE_WITH_TIMEOUT(backend.candidate().value("state").toString(), QStringLiteral("ready"), 8000);
        QVERIFY(!backend.startConversion(651, 0, 618, 1080, "x").isEmpty()); // the list is full: no use converting
        QVERIFY(!QFileInfo::exists(dir.filePath("converted")));
    }

    void theKeysCarryWhatAnEditorNeeds()
    {
        const QTemporaryDir dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, twoThemes()));
        const bosun::BosunBackend backend(file);
        const QVariantMap play = backend.keys("main").at(0).toMap();
        QCOMPARE(play.value("type").toString(), QStringLiteral("media"));
        QCOMPARE(play.value("command").toString(), QStringLiteral("play-pause"));
        QCOMPARE(play.value("rawLabel").toString(), QStringLiteral("Play"));
        QCOMPARE(backend.keys("main").at(2).toMap().value("pageTarget").toString(), QStringLiteral("music"));
        QVERIFY(backend.keys("main").at(5).toMap().value("type").isNull()); // an empty key has no action to describe
        QVERIFY(backend.pageNames().contains("music"));
        QCOMPARE(backend.mediaCommands().size(), 12);
        QVERIFY(bosun::BosunBackend::keypadKeys().contains("kp0"));
    }

    void aKeyIsEditedThroughTheBackend()
    {
        const QTemporaryDir dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, twoThemes()));
        bosun::BosunBackend backend(file);
        const int before = backend.revision();
        QVERIFY(backend.setKey("music", 3, {{"type", "media"}, {"command", "volume"}, {"step", -10}, {"label", "Quieter"}}).isEmpty());
        QVERIFY(backend.revision() > before); // read again at once
        const QVariantMap key = backend.keys("music").at(3).toMap();
        QCOMPARE(key.value("command").toString(), QStringLiteral("volume"));
        QCOMPARE(key.value("step").toInt(), -10);
        QCOMPARE(key.value("label").toString(), QStringLiteral("Quieter"));
        QVERIFY(backend.clearKey("music", 3).isEmpty());
        QVERIFY(backend.keys("music").at(3).toMap().value("empty").toBool());
        QVERIFY(!backend.setKey("music", 3, {{"type", "media"}, {"command", "nonsense"}}).isEmpty());
        QVERIFY(!backend.setKey("music", 3, {{"type", "page"}, {"page", "no-such-page"}}).isEmpty());
        QVERIFY(backend.keys("music").at(3).toMap().value("empty").toBool()); // refused: nothing written
    }

    void aTabIsAddedRenamedMovedAndRemovedThroughTheBackend()
    {
        const QTemporaryDir dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, twoThemes()));
        bosun::BosunBackend backend(file);
        QVERIFY(backend.addTheme("Création Musicale", "#ab47bc").isEmpty());
        QCOMPARE(backend.themes().size(), 3);
        QCOMPARE(backend.themes().at(2).toMap().value("page").toString(), QStringLiteral("creation-musicale")); // the page name is made from the name
        QCOMPARE(backend.themes().at(2).toMap().value("color").toString(), QStringLiteral("#ab47bc"));
        QVERIFY(backend.addTheme("Création Musicale", "#ab47bc").isEmpty()); // same name again: its own page
        QCOMPARE(backend.themes().at(3).toMap().value("page").toString(), QStringLiteral("creation-musicale-2"));
        QCOMPARE(backend.keys("creation-musicale").size(), 12); // the page is there, empty

        QVERIFY(backend.setTheme(2, "Studio", "#00acc1").isEmpty());
        QCOMPARE(backend.themes().at(2).toMap().value("name").toString(), QStringLiteral("Studio"));
        QVERIFY(backend.moveTheme(2, -1).isEmpty());
        QCOMPARE(backend.themes().at(1).toMap().value("name").toString(), QStringLiteral("Studio"));
        QVERIFY(backend.removeTheme(1).isEmpty());
        QCOMPARE(backend.themes().size(), 3);
        QVERIFY(!backend.pageNames().contains("creation-musicale"));
        QVERIFY(!backend.removeTheme(0).isEmpty()); // the home page's tab stays
        QVERIFY(!backend.addTheme("", "#ab47bc").isEmpty());
    }

    void aSubPageTakesTheColourOfTheTabItIsShownIn()
    {
        const QTemporaryDir dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, twoThemes()));
        const bosun::BosunBackend backend(file);
        // "music" is the page of a theme with its own colour; shown in the tab of "main" it takes main's.
        QVERIFY(backend.tile("music", 0, "main").toImage() != backend.tile("music", 0, "music").toImage());
        QVERIFY(backend.tile("music", 0).toImage() == backend.tile("music", 0, "music").toImage());
        bosun::TileProvider provider(&backend);
        QSize size;
        QVERIFY(provider.requestPixmap("music/0?r=7&theme=main", &size, {}).toImage() == backend.tile("music", 0, "main").toImage());
    }

    void aVideoThatCanBeCutShowsWhereTheModuleFallsAndASensibleCrop()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("v.mp4"), "x"));
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, saverPages(R"("v.mp4")"))); // the video is in the list already: it can still be cut
        bosun::BosunBackend backend(file, {.ffprobe = fakeProbe(dir.path(), wideProbe()), .ffmpeg = fakeFfmpeg(dir.path(), "exit 0"),
                                           .thumbnailFolder = dir.filePath("thumbs"), .convertedFolder = dir.filePath("converted")});
        backend.inspect(dir.filePath("v.mp4"));
        QTRY_COMPARE_WITH_TIMEOUT(backend.candidate().value("state").toString(), QStringLiteral("ready"), 8000);
        const QVariantMap candidate = backend.candidate();
        QVERIFY(!candidate.value("canAdd").toBool());  // already there
        QVERIFY(candidate.value("canCut").toBool());   // but a cut is a new file
        QCOMPARE(candidate.value("width").toInt(), 1920);
        QCOMPARE(candidate.value("height").toInt(), 1080);
        QCOMPARE(candidate.value("canvasWidth").toInt(), 720);
        QCOMPARE(candidate.value("canvasHeight").toInt(), 1258);
        QCOMPARE(candidate.value("keyRegions").toList().size(), 12);
        QCOMPARE(candidate.value("screen").toMap().value("width").toDouble(), 720.0);
        const QVariantMap crop = candidate.value("crop").toMap();
        QCOMPARE(crop.value("x").toInt(), 651);
        QCOMPARE(crop.value("width").toInt(), 618);
        QCOMPARE(crop.value("height").toInt(), 1080);
    }

    void theCropIsMovedAndZoomedWithinTheVideo()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("v.mp4"), "x"));
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, saverPages(R"("a.mp4")")));
        bosun::BosunBackend backend(file, {.ffprobe = fakeProbe(dir.path(), wideProbe()), .ffmpeg = fakeFfmpeg(dir.path(), "exit 0"),
                                           .thumbnailFolder = dir.filePath("thumbs"), .convertedFolder = dir.filePath("converted")});
        // Nothing chosen: what is given comes back.
        const QVariantMap given{{"x", 5}, {"y", 6}, {"width", 7}, {"height", 8}};
        QCOMPARE(backend.cropMoved(given, 10, 10), given);
        QCOMPARE(backend.cropZoomed(given, 2, 0, 0), given);
        QVERIFY(backend.cropCentered().value("width").toInt() == 0);

        backend.inspect(dir.filePath("v.mp4"));
        QTRY_COMPARE_WITH_TIMEOUT(backend.candidate().value("state").toString(), QStringLiteral("ready"), 8000);
        const QVariantMap crop = backend.candidate().value("crop").toMap();
        QCOMPARE(backend.cropMoved(crop, 100, 0).value("x").toInt(), 751);
        QCOMPARE(backend.cropMoved(crop, 99999, 0).value("x").toInt(), 1302); // kept inside the video
        QCOMPARE(backend.cropMoved(crop, -99999, 0).value("x").toInt(), 0);
        const QVariantMap closer = backend.cropZoomed(crop, 2, 960, 540); // around the middle of the video
        QCOMPARE(closer.value("width").toInt(), 309);
        QCOMPARE(closer.value("height").toInt(), 540);
        QCOMPARE(backend.cropZoomed(crop, 0.1, 960, 540).value("width").toInt(), 618); // cannot go wider than the video allows
    }

    void aFullListOrNoFfmpegMeansNoCut()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("v.mp4"), "x"));
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, saverPages(R"("a.mp4")")));
        bosun::BosunBackend noFfmpeg(file, {.ffprobe = fakeProbe(dir.path(), wideProbe()), .ffmpeg = "/nonexistent/none",
                                            .thumbnailFolder = dir.filePath("thumbs"), .convertedFolder = dir.filePath("converted")});
        noFfmpeg.inspect(dir.filePath("v.mp4"));
        QTRY_COMPARE_WITH_TIMEOUT(noFfmpeg.candidate().value("state").toString(), QStringLiteral("ready"), 8000);
        QVERIFY(!noFfmpeg.candidate().value("canCut").toBool());
        QVERIFY(!noFfmpeg.candidate().value("cutWhy").toString().isEmpty());
        QVERIFY(noFfmpeg.candidate().value("canAdd").toBool()); // as it is still works
        QVERIFY(!noFfmpeg.startConversion(651, 0, 618, 1080, "x").isEmpty());

        QStringList names;
        for (int i = 0; i < galleond::MaxScreensaverVideos; ++i)
            names << QStringLiteral(R"("v%1.mp4")").arg(i);
        QVERIFY(writeFile(file, saverPages(names.join(','))));
        bosun::BosunBackend full(file, {.ffprobe = fakeProbe(dir.path(), wideProbe()), .ffmpeg = fakeFfmpeg(dir.path(), "exit 0"),
                                        .thumbnailFolder = dir.filePath("thumbs"), .convertedFolder = dir.filePath("converted")});
        full.inspect(dir.filePath("v.mp4"));
        QTRY_COMPARE_WITH_TIMEOUT(full.candidate().value("state").toString(), QStringLiteral("ready"), 8000);
        QVERIFY(!full.candidate().value("canCut").toBool());
        QVERIFY(full.candidate().value("cutWhy").toString().contains("full"));
    }

    void theWindowHasATabPerThemeAndTheFixedOnes()
    {
        const QTemporaryDir dir;
        const QString file = dir.filePath("pages.json");
        QVERIFY(writeFile(file, twoThemes()));
        const bosun::BosunBackend backend(file);
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("tiles"), new bosun::TileProvider(&backend));
        const bosun::DaemonStatus daemon("/nonexistent/systemctl");
        engine.addImageProvider(QStringLiteral("themeicon"), new galleond::ThemeIconProvider);
        engine.setInitialProperties({{QStringLiteral("backend"), QVariant::fromValue(&backend)}, {QStringLiteral("daemon"), QVariant::fromValue(&daemon)}});
        engine.load(QUrl(QStringLiteral("qrc:/bosun/qml/Bosun.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        QObject *window = engine.rootObjects().constFirst();
        // The window must be able to call what it is given (the daemon's start, the backend's edits).
        QVERIFY(qvariant_cast<QObject *>(window->property("daemon")) != nullptr);
        QVERIFY(qvariant_cast<QObject *>(window->property("backend")) != nullptr);
        QCOMPARE(window->property("tabs").toList().size(), 2 + 4); // themes, the new tab, keyboard, wallpapers, screensavers
        QCOMPARE(window->property("keys").toList().size(), 12);
        QVERIFY(window->property("selected").toMap().isEmpty()); // nothing chosen yet
        window->setProperty("selectedKey", 0);
        QCOMPARE(window->property("selected").toMap().value("label").toString(), QStringLiteral("Play"));
        // Another tab forgets the selection; a fixed tab has no keys.
        window->setProperty("currentTab", 1);
        QCOMPARE(window->property("selectedKey").toInt(), -1);
        QCOMPARE(window->property("keys").toList().size(), 12);
        window->setProperty("currentTab", 5);
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
