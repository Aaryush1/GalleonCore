// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <VideoConverter.h>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using bosun::ConversionJob;
using bosun::VideoConverter;

namespace {

const char *const EncoderLine = " V....D libx264              libx264 H.264 / AVC / MPEG-4 AVC\n";

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

// A program in place of FFmpeg. `body` runs for a conversion; the last argument is the file to write.
QString fakeFfmpeg(const QString &dir, const QString &name, const QString &encoders, const QString &body)
{
    QString script = dir + "/" + name;
    const QByteArray text = "#!/bin/sh\ncase \"$*\" in\n  *-encoders*) printf '" + encoders.toUtf8() + "'; exit 0;;\nesac\n"
                            "for last; do :; done\nfor a in \"$@\"; do echo \"$a\" >> \"$last.args\"; done\n"
                            + body.toUtf8() + "\n";
    if (!writeFile(script, text))
        return {};
    QFile::setPermissions(script, QFile::permissions(script) | QFile::ExeUser);
    return script;
}

QString goodBody()
{
    return QStringLiteral(
        "echo out_time_us=3000000; echo progress=continue; echo out_time_us=6000000; printf converted > \"$last\"; echo progress=end; exit 0");
}

ConversionJob jobIn(const QString &dir, const QString &name = QStringLiteral("Night Rain"))
{
    ConversionJob job;
    job.input = dir + "/in.mp4";
    job.source = QSize(1920, 1080);
    job.crop = QRect(651, 0, 618, 1080);
    job.canvas = QSize(720, 1258);
    job.fps = 15;
    job.seconds = 12;
    job.outputFolder = dir + "/out";
    job.name = name;
    return job;
}

QStringList files(const QString &folder)
{
    return QDir(folder).entryList(QDir::Files | QDir::Hidden, QDir::Name);
}

} // namespace

class TestVideoConverter : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aNameIsMadeSafe()
    {
        QCOMPARE(VideoConverter::safeName("Night Rain"), QStringLiteral("Night Rain"));
        QCOMPARE(VideoConverter::safeName("Réfuge néon_2-b.v"), QStringLiteral("Réfuge néon_2-b.v"));
        QCOMPARE(VideoConverter::safeName("a/b\\c:d*e?f"), QStringLiteral("a-b-c-d-e-f"));
        QCOMPARE(VideoConverter::safeName("..hidden"), QStringLiteral("hidden"));
        QVERIFY(!VideoConverter::safeName("../../etc/passwd").contains('/'));
        QVERIFY(!VideoConverter::safeName("../../etc/passwd").startsWith('.'));
        QCOMPARE(VideoConverter::safeName(""), QStringLiteral("video"));
        QCOMPARE(VideoConverter::safeName("   "), QStringLiteral("video"));
        QCOMPARE(VideoConverter::safeName("..."), QStringLiteral("video"));
        QCOMPARE(VideoConverter::safeName(QString(200, 'x')).size(), VideoConverter::MaxNameLength);
    }

    void aFreePathNeverTakesAFileThatIsThere()
    {
        const QTemporaryDir dir;
        QCOMPARE(VideoConverter::freePath(dir.path(), "a"), dir.filePath("a.mp4"));
        QVERIFY(writeFile(dir.filePath("a.mp4"), "x"));
        QCOMPARE(VideoConverter::freePath(dir.path(), "a"), dir.filePath("a-2.mp4"));
        QVERIFY(writeFile(dir.filePath("a-2.mp4"), "x"));
        QCOMPARE(VideoConverter::freePath(dir.path(), "a"), dir.filePath("a-3.mp4"));
    }

    void theEncoderIsChosenFromWhatFfmpegLists()
    {
        const QString both = " V....D libx264              libx264 H.264\n V..... mpeg4                MPEG-4 part 2\n A....D aac AAC\n";
        QCOMPARE(VideoConverter::chooseEncoder(both), QStringLiteral("libx264"));
        QCOMPARE(VideoConverter::chooseEncoder(" V..... mpeg4                MPEG-4 part 2\n"), QStringLiteral("mpeg4"));
        QVERIFY(VideoConverter::chooseEncoder(" V....D libx264rgb           libx264 RGB\n A....D aac AAC\n").isEmpty());
        QVERIFY(VideoConverter::chooseEncoder(" A....D libx264              not a video encoder\n").isEmpty());
        QVERIFY(VideoConverter::chooseEncoder("").isEmpty());
    }

    void progressIsReadFromTheProgressLines()
    {
        QCOMPARE(VideoConverter::progressSeconds("out_time_us=1500000").value_or(-1), 1.5);
        QCOMPARE(VideoConverter::progressSeconds("out_time_ms=2000000\r").value_or(-1), 2.0);
        for (const char *line : {"out_time_us=N/A", "out_time=00:00:01", "progress=end", "out_time_us=-5", "out_time_us=", "frame=3",
                                 "out_time_us=99999999999999999999"})
            QVERIFY2(!VideoConverter::progressSeconds(line).has_value(), line);
    }

    void aJobThatIsNotFitIsRefusedWithTheReason()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("in.mp4"), "video"));
        QVERIFY(VideoConverter::problemWith(jobIn(dir.path())).isEmpty());
        ConversionJob job = jobIn(dir.path());
        job.input = dir.filePath("missing.mp4");
        QVERIFY(!VideoConverter::problemWith(job).isEmpty());
        QVERIFY(writeFile(dir.filePath("notes.txt"), "text"));
        job.input = dir.filePath("notes.txt");
        QVERIFY(!VideoConverter::problemWith(job).isEmpty());
        job = jobIn(dir.path());
        job.crop = QRect(1800, 0, 618, 1080); // goes beyond the video
        QVERIFY(!VideoConverter::problemWith(job).isEmpty());
        job = jobIn(dir.path());
        job.crop = QRect(0, 0, 1, 1);
        QVERIFY(!VideoConverter::problemWith(job).isEmpty());
        for (const int fps : {0, 21, -1}) {
            job = jobIn(dir.path());
            job.fps = fps;
            QVERIFY(!VideoConverter::problemWith(job).isEmpty());
        }
        job = jobIn(dir.path());
        job.canvas = QSize();
        QVERIFY(!VideoConverter::problemWith(job).isEmpty());
        job = jobIn(dir.path());
        job.outputFolder.clear();
        QVERIFY(!VideoConverter::problemWith(job).isEmpty());
    }

    void theArgumentsAreAListWithTheSafeguards()
    {
        const QTemporaryDir dir;
        ConversionJob job = jobIn(dir.path());
        job.input = dir.filePath("-rf; touch pwned.mp4"); // a name that could read as an option or a command
        const QStringList x264 = VideoConverter::arguments(job, "libx264", "/out/a.mp4.partial");
        QVERIFY(x264.contains("-nostdin"));
        const auto whitelist = x264.indexOf("-protocol_whitelist");
        QVERIFY(whitelist >= 0);
        QCOMPARE(x264.at(whitelist + 1), QStringLiteral("file")); // no network
        QVERIFY(x264.contains("file:" + QFileInfo(job.input).absoluteFilePath())); // one argument, behind "file:"
        QCOMPARE(x264.at(x264.indexOf("-t") + 1), QStringLiteral("120"));
        QCOMPARE(x264.at(x264.indexOf("-vf") + 1),
                 QStringLiteral("crop=618:1080:651:0,scale=720:1258:flags=lanczos,fps=15,format=yuv420p"));
        for (const char *dropped : {"-an", "-sn", "-dn"})
            QVERIFY(x264.contains(dropped));
        QVERIFY(x264.contains("libx264"));
        QVERIFY(!x264.contains("mpeg4"));
        QCOMPARE(x264.mid(x264.size() - 3), (QStringList{"-f", "mp4", "/out/a.mp4.partial"}));

        const QStringList mpeg = VideoConverter::arguments(job, "mpeg4", "/out/a.mp4.partial");
        QVERIFY(mpeg.contains("mpeg4"));
        QVERIFY(!mpeg.contains("libx264"));
    }

    void aConversionWritesTheResultUnderItsNameAndSaysHowFarItIs()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("in.mp4"), "video"));
        VideoConverter converter(fakeFfmpeg(dir.path(), "ffmpeg", EncoderLine, goodBody()));
        QVERIFY(converter.isAvailable());
        QCOMPARE(converter.encoder(), QStringLiteral("libx264"));
        QSignalSpy progress(&converter, &VideoConverter::progress);
        QSignalSpy finished(&converter, &VideoConverter::finished);
        const QSignalSpy failed(&converter, &VideoConverter::failed);
        QVERIFY(converter.start(jobIn(dir.path())).isEmpty());
        QVERIFY(converter.isRunning());
        QVERIFY(finished.wait(8000));
        QVERIFY(failed.isEmpty());
        QCOMPARE(finished.at(0).at(0).toString(), dir.filePath("out/Night Rain.mp4"));
        QCOMPARE(files(dir.filePath("out")), (QStringList{"Night Rain.mp4", "Night Rain.mp4.partial.args"}));
        QFile result(dir.filePath("out/Night Rain.mp4"));
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("converted"));
        // 3 s then 6 s of a 12 s video, then the end.
        QVERIFY(progress.size() >= 3);
        QCOMPARE(progress.at(0).at(0).toDouble(), 0.25);
        QCOMPARE(progress.at(1).at(0).toDouble(), 0.5);
        QCOMPARE(progress.last().at(0).toDouble(), 1.0);
        QVERIFY(!converter.isRunning());
    }

    void aSecondResultWithTheSameNameDoesNotReplaceTheFirst()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("in.mp4"), "video"));
        VideoConverter converter(fakeFfmpeg(dir.path(), "ffmpeg", EncoderLine, goodBody()));
        QSignalSpy finished(&converter, &VideoConverter::finished);
        QVERIFY(converter.start(jobIn(dir.path())).isEmpty());
        QVERIFY(finished.wait(8000));
        QVERIFY(converter.start(jobIn(dir.path())).isEmpty());
        QVERIFY(finished.wait(8000));
        QCOMPARE(finished.at(1).at(0).toString(), dir.filePath("out/Night Rain-2.mp4"));
        QVERIFY(QFile::exists(dir.filePath("out/Night Rain.mp4")));
    }

    void aNameWithSlashesStaysInTheFolder()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("in.mp4"), "video"));
        VideoConverter converter(fakeFfmpeg(dir.path(), "ffmpeg", EncoderLine, goodBody()));
        QSignalSpy finished(&converter, &VideoConverter::finished);
        QVERIFY(converter.start(jobIn(dir.path(), "../../escape/evil")).isEmpty());
        QVERIFY(finished.wait(8000));
        QVERIFY(finished.at(0).at(0).toString().startsWith(dir.filePath("out/")));
        QVERIFY(!QFileInfo::exists(dir.filePath("escape")));
    }

    void aFailureSaysWhatFfmpegSaidAndLeavesNothingBehind()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("in.mp4"), "video"));
        VideoConverter converter(fakeFfmpeg(dir.path(), "ffmpeg", EncoderLine,
                                            "printf partial > \"$last\"; echo 'Invalid data found when processing input' >&2; exit 1"));
        QSignalSpy failed(&converter, &VideoConverter::failed);
        const QSignalSpy finished(&converter, &VideoConverter::finished);
        QVERIFY(converter.start(jobIn(dir.path())).isEmpty());
        QVERIFY(failed.wait(8000));
        QVERIFY(finished.isEmpty());
        QVERIFY(failed.at(0).at(0).toString().contains("Invalid data"));
        QCOMPARE(files(dir.filePath("out")), (QStringList{"Night Rain.mp4.partial.args"})); // no result, no partial file
        QVERIFY(!converter.isRunning());
    }

    void aProgramThatWritesNothingIsAFailureEvenIfItSaysOk()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("in.mp4"), "video"));
        VideoConverter converter(fakeFfmpeg(dir.path(), "ffmpeg", EncoderLine, "exit 0"));
        QSignalSpy failed(&converter, &VideoConverter::failed);
        QVERIFY(converter.start(jobIn(dir.path())).isEmpty());
        QVERIFY(failed.wait(8000));
    }

    void cancellingStopsFfmpegAndDeletesTheUnfinishedFile()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("in.mp4"), "video"));
        VideoConverter converter(fakeFfmpeg(dir.path(), "ffmpeg", EncoderLine, "printf partial > \"$last\"; sleep 30; exit 0"));
        QSignalSpy cancelled(&converter, &VideoConverter::cancelled);
        const QSignalSpy finished(&converter, &VideoConverter::finished);
        const QSignalSpy failed(&converter, &VideoConverter::failed);
        QVERIFY(converter.start(jobIn(dir.path())).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(dir.filePath("out/Night Rain.mp4.partial")), 5000);
        converter.cancel();
        QVERIFY(cancelled.wait(8000));
        QVERIFY(finished.isEmpty() && failed.isEmpty());
        QVERIFY(!QFile::exists(dir.filePath("out/Night Rain.mp4.partial")));
        QVERIFY(!QFile::exists(dir.filePath("out/Night Rain.mp4")));
        QVERIFY(!converter.isRunning());
    }

    void onlyOneConversionRunsAtATime()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("in.mp4"), "video"));
        VideoConverter converter(fakeFfmpeg(dir.path(), "ffmpeg", EncoderLine, "sleep 30; exit 0"));
        QSignalSpy cancelled(&converter, &VideoConverter::cancelled);
        QVERIFY(converter.start(jobIn(dir.path())).isEmpty());
        QVERIFY(!converter.start(jobIn(dir.path())).isEmpty());
        converter.cancel();
        QVERIFY(cancelled.wait(8000));
    }

    void withoutFfmpegOrAnEncoderNothingStarts()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath("in.mp4"), "video"));
        VideoConverter missing(dir.filePath("no-ffmpeg"));
        QVERIFY(!missing.isAvailable());
        QVERIFY(!missing.start(jobIn(dir.path())).isEmpty());
        VideoConverter bare(fakeFfmpeg(dir.path(), "ffmpeg-bare", " A....D aac AAC\n", goodBody()));
        QVERIFY(!bare.isAvailable());
        QVERIFY(!bare.start(jobIn(dir.path())).isEmpty());
        QVERIFY(!QFileInfo::exists(dir.filePath("out"))); // not even the folder
    }

    void aFolderIsNotTakenForTheProgram()
    {
        const QTemporaryDir dir;
        QDir().mkpath(dir.filePath("bin/ffmpeg"));
        QVERIFY(!VideoConverter(dir.filePath("bin/ffmpeg")).isAvailable());
    }
};

QTEST_GUILESS_MAIN(TestVideoConverter)
#include "test_videoconverter.moc"
