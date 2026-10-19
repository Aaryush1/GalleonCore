// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <VideoThumbnails.h>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using galleond::VideoThumbnails;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes, bool executable = false)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(bytes);
    file.close();
    if (executable)
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    return true;
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QByteArray jpegBytes(int width, int height, QColor colour)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(colour);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "JPEG"))
        qFatal("cannot encode the test image");
    return bytes;
}

// A program that stands in for ffmpeg: it logs its arguments, and writes the
// prepared picture to its LAST argument (the output file).
QString fakeFfmpeg(const QString &dir, const QString &picture, const QString &log, bool fail = false, bool garbage = false)
{
    const QString script = dir + "/fake-ffmpeg-" + (fail ? "fail" : garbage ? "garbage" : "ok");
    QByteArray text = "#!/bin/sh\nfor a in \"$@\"; do last=\"$a\"; echo \"$a\" >> '" + log.toUtf8() + "'; done\necho '--' >> '" + log.toUtf8() + "'\n";
    if (fail)
        text += "exit 1\n";
    else if (garbage)
        text += "echo not an image > \"$last\"\nexit 0\n";
    else
        text += "cp '" + picture.toUtf8() + "' \"$last\"\nexit 0\n";
    writeFile(script, text, true);
    return script;
}

} // namespace

class TestVideoThumbnails : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void onlyKnownVideoExtensionsCount()
    {
        for (const char *name : {"a.mp4", "A.MKV", "b.webm", "c.avi", "d.mov", "e.m4v", "f.mpg", "g.mpeg", "h.wmv", "i.flv", "j.ogv"})
            QVERIFY2(VideoThumbnails::isVideoFile(QString::fromLatin1(name)), name);
        for (const char *name : {"a.mp3", "b.flac", "c.jpg", "d.txt", "e", "f.m3u8", "g.sh", "mp4", ".mp4x"})
            QVERIFY2(!VideoThumbnails::isVideoFile(QString::fromLatin1(name)), name);
    }

    void withoutFfmpegNothingHappens()
    {
        QTemporaryDir dir;
        VideoThumbnails thumbnails(dir.filePath("cache"), "/nonexistent/ffmpeg");
        QVERIFY(!thumbnails.isAvailable());
        QSignalSpy ready(&thumbnails, &VideoThumbnails::thumbnailReady);
        writeFile(dir.filePath("v.mp4"), "x");
        thumbnails.request(dir.filePath("v.mp4"), 100);
        QVERIFY(thumbnails.cached(dir.filePath("v.mp4")).isEmpty());
        QTest::qWait(100);
        QCOMPARE(ready.count(), 0);
    }

    void aPictureIsMadeCachedAndNotMadeAgain()
    {
        QTemporaryDir dir;
        const QString picture = dir.filePath("frame.jpg");
        writeFile(picture, jpegBytes(320, 180, Qt::darkMagenta));
        const QString log = dir.filePath("args.log");
        VideoThumbnails thumbnails(dir.filePath("cache"), fakeFfmpeg(dir.path(), picture, log));
        QVERIFY(thumbnails.isAvailable());

        const QString video = dir.filePath("My Film.mp4");
        writeFile(video, "not really a video");
        QVERIFY(thumbnails.cached(video).isEmpty());

        QSignalSpy ready(&thumbnails, &VideoThumbnails::thumbnailReady);
        thumbnails.request(video, 600); // a tenth of 600 s = 60 s
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        QCOMPARE(ready.at(0).at(0).toString(), video);
        const QString file = ready.at(0).at(1).toString();
        QVERIFY(QFile::exists(file));
        QCOMPARE(thumbnails.cached(video), file);
        QVERIFY(file.startsWith(dir.filePath("cache")));
        QImage image(file);
        QVERIFY(!image.isNull());

        // The second request is answered from the cache: FFmpeg is not started again.
        const QByteArray before = readAll(log);
        thumbnails.request(video, 600);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 2000);
        const QByteArray after = readAll(log);
        QCOMPARE(after, before);
    }

    void ffmpegIsStartedWithTheProtections()
    {
        QTemporaryDir dir;
        const QString picture = dir.filePath("frame.jpg");
        writeFile(picture, jpegBytes(64, 64, Qt::red));
        const QString log = dir.filePath("args.log");
        VideoThumbnails thumbnails(dir.filePath("cache"), fakeFfmpeg(dir.path(), picture, log));
        // A name that looks like an option, and a shell trick, are just a file name.
        const QString video = dir.filePath("-i evil; touch pwned $(id).mp4");
        writeFile(video, "x");
        QSignalSpy ready(&thumbnails, &VideoThumbnails::thumbnailReady);
        thumbnails.request(video, 0);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);

        QFile f(log);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QStringList args = QString::fromUtf8(f.readAll()).split('\n');
        QVERIFY(args.contains("-protocol_whitelist"));
        QCOMPARE(args.at(args.indexOf("-protocol_whitelist") + 1), QStringLiteral("file"));
        QVERIFY(args.contains("-nostdin"));
        QVERIFY(args.contains("file:" + video)); // the "file:" prefix, the name untouched
        QVERIFY(!QFile::exists(dir.filePath("pwned")));
        QVERIFY(args.contains("-frames:v"));
    }

    void theMomentFollowsTheDuration()
    {
        QTemporaryDir dir;
        const QString picture = dir.filePath("frame.jpg");
        writeFile(picture, jpegBytes(64, 64, Qt::red));
        const auto momentFor = [&](double duration) {
            const QString log = dir.filePath(QString("log%1").arg(duration));
            VideoThumbnails thumbnails(dir.filePath(QString("cache%1").arg(duration)), fakeFfmpeg(dir.path(), picture, log));
            const QString video = dir.filePath(QString("v%1.mp4").arg(duration));
            writeFile(video, "x");
            QSignalSpy ready(&thumbnails, &VideoThumbnails::thumbnailReady);
            thumbnails.request(video, duration);
            ready.wait(10000);
            const QStringList args = QString::fromUtf8(readAll(log)).split('\n');
            return args.at(args.indexOf("-ss") + 1);
        };
        QCOMPARE(momentFor(0), QStringLiteral("1.0"));       // unknown
        QCOMPARE(momentFor(10), QStringLiteral("1.0"));      // short: the first second
        QCOMPARE(momentFor(600), QStringLiteral("60.0"));    // a tenth, 60 s at most
        QCOMPARE(momentFor(7200), QStringLiteral("60.0"));
        QCOMPARE(momentFor(100), QStringLiteral("10.0"));
    }

    void aFailureOrGarbageGivesNoPicture()
    {
        QTemporaryDir dir;
        const QString picture = dir.filePath("frame.jpg");
        writeFile(picture, jpegBytes(64, 64, Qt::red));
        for (const bool garbage : {false, true}) {
            VideoThumbnails thumbnails(dir.filePath(garbage ? "cacheG" : "cacheF"),
                                       fakeFfmpeg(dir.path(), picture, dir.filePath("l.log"), !garbage, garbage));
            const QString video = dir.filePath(garbage ? "g.mp4" : "f.mp4");
            writeFile(video, "x");
            QSignalSpy ready(&thumbnails, &VideoThumbnails::thumbnailReady);
            thumbnails.request(video, 600);
            QTest::qWait(1500); // first try, then the first-frame retry
            QCOMPARE(ready.count(), 0);
            QVERIFY(thumbnails.cached(video).isEmpty());
            QVERIFY(QDir(dir.filePath(garbage ? "cacheG" : "cacheF")).entryList({"*.jpg"}, QDir::Files).isEmpty()); // no leftover
        }
    }

    void aReplacedFileGetsANewPicture()
    {
        QTemporaryDir dir;
        const QString picture = dir.filePath("frame.jpg");
        writeFile(picture, jpegBytes(64, 64, Qt::red));
        VideoThumbnails thumbnails(dir.filePath("cache"), fakeFfmpeg(dir.path(), picture, dir.filePath("l.log")));
        const QString video = dir.filePath("v.mp4");
        writeFile(video, "first version");
        QSignalSpy ready(&thumbnails, &VideoThumbnails::thumbnailReady);
        thumbnails.request(video, 0);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        writeFile(video, "a second, longer version of the file");
        QVERIFY(thumbnails.cached(video).isEmpty()); // size changed: another key
    }

    void thePictureOfAMissingFileIsNeverRequested()
    {
        QTemporaryDir dir;
        const QString picture = dir.filePath("frame.jpg");
        writeFile(picture, jpegBytes(64, 64, Qt::red));
        const QString log = dir.filePath("args.log");
        VideoThumbnails thumbnails(dir.filePath("cache"), fakeFfmpeg(dir.path(), picture, log));
        thumbnails.request(dir.filePath("absent.mp4"), 10);
        thumbnails.request(dir.filePath("notvideo.mp3"), 10);
        QTest::qWait(300);
        QVERIFY(!QFile::exists(log)); // ffmpeg never ran
    }

    void theCacheIsBounded()
    {
        QTemporaryDir dir;
        const QString cache = dir.filePath("cache");
        QDir().mkpath(cache);
        for (int i = 0; i < VideoThumbnails::MaxEntries + 20; ++i)
            writeFile(cache + QString("/old%1.jpg").arg(i), "x");
        VideoThumbnails thumbnails(cache, "/bin/true"); // pruning happens when it starts
        QVERIFY(QDir(cache).entryList({"*.jpg"}, QDir::Files).size() <= VideoThumbnails::MaxEntries);
    }

    void aRealVideoWorksWithTheRealFfmpeg()
    {
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        const QString video = dir.filePath("test clip.mp4");
        QProcess make;
        make.start(ffmpeg, {"-nostdin", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                            "color=c=0xcc5a14:s=640x360:d=3", "-pix_fmt", "yuv420p", "-y", video});
        QVERIFY(make.waitForFinished(30000) && make.exitCode() == 0);

        VideoThumbnails thumbnails(dir.filePath("cache"));
        QSignalSpy ready(&thumbnails, &VideoThumbnails::thumbnailReady);
        thumbnails.request(video, 3); // short: the first second
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 30000);
        const QImage image(ready.at(0).at(1).toString());
        QVERIFY(!image.isNull());
        QVERIFY(image.width() <= VideoThumbnails::Side && image.height() <= VideoThumbnails::Side);
        const QRgb pixel = image.pixel(image.width() / 2, image.height() / 2);
        QVERIFY(qAbs(qRed(pixel) - 0xcc) < 30 && qAbs(qGreen(pixel) - 0x5a) < 30 && qAbs(qBlue(pixel) - 0x14) < 40);
    }
};

QTEST_MAIN(TestVideoThumbnails)
#include "test_videothumbnails.moc"
