// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <VideoProbe.h>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace galleond;

namespace {

QByteArray good()
{
    return R"({"programs": [], "streams": [{"codec_name": "h264", "width": 704, "height": 1280,
    "r_frame_rate": "24/1", "avg_frame_rate": "24/1", "duration": "9.000000"}], "format": {"duration": "9.041000"}})";
}

// A program that prints `output` and ends with `code`, in place of ffprobe; it also
// writes its arguments to `log`.
QString fakeProbe(const QString &dir, const QByteArray &output, int code, const QString &log = {})
{
    QString script = dir + QStringLiteral("/fake-ffprobe-%1").arg(QString::number(qHash(output) ^ static_cast<uint>(code), 16));
    QFile file(script);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    QByteArray text = "#!/bin/sh\n";
    if (!log.isEmpty())
        text += QByteArray(R"(for a in "$@"; do echo "$a" >> ')") + log.toUtf8() + "'; done\n";
    text += "cat <<'EOF'\n" + output + "\nEOF\nexit " + QByteArray::number(code) + "\n";
    file.write(text);
    file.close();
    QFile::setPermissions(script, QFile::permissions(script) | QFile::ExeUser);
    return script;
}

} // namespace

class TestVideoProbe : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aPlainAnswerIsRead()
    {
        const VideoInfo info = VideoProbe::parse(good());
        QVERIFY(info.valid);
        QCOMPARE(info.width, 704);
        QCOMPARE(info.height, 1280);
        QCOMPARE(info.fps, 24.0);
        QCOMPARE(info.seconds, 9.041);
        QCOMPARE(info.codec, QStringLiteral("h264"));
    }

    void aFractionalRateIsDivided()
    {
        const VideoInfo info = VideoProbe::parse(
            R"({"streams": [{"codec_name": "vp9", "width": 640, "height": 360, "avg_frame_rate": "30000/1001"}]})");
        QVERIFY(info.valid);
        QVERIFY(qAbs(info.fps - 29.97) < 0.01);
        QCOMPARE(info.seconds, 0.0); // the file does not say
    }

    void theOtherRateIsUsedWhenTheAverageIsZero()
    {
        const VideoInfo info = VideoProbe::parse(
            R"({"streams": [{"width": 640, "height": 360, "avg_frame_rate": "0/0", "r_frame_rate": "25/1"}]})");
        QVERIFY(info.valid);
        QCOMPARE(info.fps, 25.0);
    }

    void nonsenseIsRefused_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("not json") << QByteArray("this is not json");
        QTest::newRow("an array") << QByteArray("[1, 2]");
        QTest::newRow("no streams") << QByteArray(R"({"streams": []})");
        QTest::newRow("stream is not an object") << QByteArray(R"({"streams": [3]})");
        QTest::newRow("no size") << QByteArray(R"({"streams": [{"codec_name": "h264"}]})");
        QTest::newRow("zero width") << QByteArray(R"({"streams": [{"width": 0, "height": 10}]})");
        QTest::newRow("negative height") << QByteArray(R"({"streams": [{"width": 10, "height": -4}]})");
        QTest::newRow("huge width") << QByteArray(R"({"streams": [{"width": 99999999, "height": 10}]})");
        QTest::newRow("size as text") << QByteArray(R"({"streams": [{"width": "640", "height": "360"}]})");
        QByteArray tooLong(R"({"streams": [{"width": 10, "height": 10, "codec_name": ")");
        tooLong += QByteArray(VideoProbe::MaxOutputBytes, 'a') + "\"}]}";
        QTest::newRow("too long") << tooLong;
    }
    void nonsenseIsRefused()
    {
        QFETCH(const QByteArray, json);
        QVERIFY(!VideoProbe::parse(json).valid);
    }

    void implausibleNumbersAreDropped()
    {
        // The size is fine, the rest is not: a video without a rate and without a length.
        const VideoInfo info = VideoProbe::parse(R"({"streams": [{"codec_name": "../../etc", "width": 640, "height": 360,
            "avg_frame_rate": "99999/1", "r_frame_rate": "1/0", "duration": "1e30"}], "format": {"duration": "-5"}})");
        QVERIFY(info.valid);
        QCOMPARE(info.fps, 0.0);
        QCOMPARE(info.seconds, 0.0);
        QVERIFY(info.codec.isEmpty()); // a name with a slash is not a codec name
    }

    void aProgramThatAnswersIsAsked()
    {
        const QTemporaryDir dir;
        const QString log = dir.filePath("arguments.log");
        const VideoProbe probe(fakeProbe(dir.path(), good(), 0, log));
        QVERIFY(probe.isAvailable());
        VideoProbe asker(fakeProbe(dir.path(), good(), 0, log));
        QSignalSpy spy(&asker, &VideoProbe::probed);
        asker.request("/tmp/some video.mp4");
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("/tmp/some video.mp4"));
        const auto info = qvariant_cast<VideoInfo>(spy.at(0).at(1));
        QVERIFY(info.valid);
        QCOMPARE(info.width, 704);
        // Only the file protocol, and the name cannot read as an option.
        QFile arguments(log);
        QVERIFY(arguments.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(arguments.readAll());
        QVERIFY(text.contains("-protocol_whitelist\nfile\n"));
        QVERIFY(text.contains("file:/tmp/some video.mp4"));
    }

    void aFailureGivesNoAnswer()
    {
        const QTemporaryDir dir;
        VideoProbe probe(fakeProbe(dir.path(), "boom", 1));
        QSignalSpy spy(&probe, &VideoProbe::probed);
        probe.request("/tmp/a.mp4");
        QVERIFY(spy.wait(5000));
        QVERIFY(!qvariant_cast<VideoInfo>(spy.at(0).at(1)).valid);
    }

    void aMissingProgramGivesNoAnswerAtOnce()
    {
        VideoProbe probe(QStringLiteral("/nonexistent/ffprobe"));
        QVERIFY(!probe.isAvailable());
        const QSignalSpy spy(&probe, &VideoProbe::probed);
        probe.request("/tmp/a.mp4");
        QCOMPARE(spy.size(), 1);
        QVERIFY(!qvariant_cast<VideoInfo>(spy.at(0).at(1)).valid);
    }

    void filesAreAskedOneAtATimeInOrderAndNotTwice()
    {
        const QTemporaryDir dir;
        VideoProbe probe(fakeProbe(dir.path(), good(), 0));
        const QSignalSpy spy(&probe, &VideoProbe::probed);
        probe.request("/tmp/one.mp4");
        probe.request("/tmp/two.mp4");
        probe.request("/tmp/two.mp4"); // already waiting
        probe.request("/tmp/one.mp4"); // already running
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 2, 8000);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("/tmp/one.mp4"));
        QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("/tmp/two.mp4"));
    }
};

QTEST_MAIN(TestVideoProbe)
#include "test_videoprobe.moc"
