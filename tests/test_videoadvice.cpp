// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <VideoAdvice.h>

#include <QTest>

using bosun::adviseOn;
using galleond::VideoInfo;

namespace {

VideoInfo video(int width, int height, double fps = 15, double seconds = 9, const QString &codec = QStringLiteral("h264"))
{
    return {.valid = true, .width = width, .height = height, .fps = fps, .seconds = seconds, .codec = codec};
}

const QSize Canvas(720, 1258); // the screen and the 12 keys of the module, as one picture

class TestVideoAdvice : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aVideoThatCannotBeReadIsSaidAndHasNoDetails()
    {
        const auto advice = adviseOn(VideoInfo{}, Canvas, 15);
        QVERIFY(advice.details.isEmpty());
        QCOMPARE(advice.notes.size(), 1);
        QVERIFY(advice.notes.at(0).contains("ffprobe"));
    }

    void theDetailsGiveSizeRateLengthAndCodec()
    {
        QCOMPARE(adviseOn(video(704, 1280, 24, 9.04), Canvas, 15).details, QStringLiteral("704 × 1280 · 24.0 fps · 9 s · h264"));
        QCOMPARE(adviseOn(video(704, 1280, 0, 0, ""), Canvas, 15).details, QStringLiteral("704 × 1280"));
    }

    void aVideoOfTheRightProportionsHasNothingToSay()
    {
        QVERIFY(adviseOn(video(720, 1258), Canvas, 15).notes.isEmpty());
        // A few percent off is not worth a warning: 704 x 1280 loses about 4%.
        QVERIFY(adviseOn(video(704, 1280), Canvas, 15).notes.isEmpty());
    }

    void aWideVideoLosesItsSides()
    {
        const auto notes = adviseOn(video(1920, 1080), Canvas, 15).notes;
        QCOMPARE(notes.size(), 1);
        QVERIFY2(notes.at(0).contains("68%") && notes.at(0).contains("at the sides"), qPrintable(notes.at(0)));
    }

    void aNarrowVideoLosesItsTopAndBottom()
    {
        const auto notes = adviseOn(video(1000, 4000), Canvas, 15).notes; // 0.25: much narrower than 0.57
        QVERIFY2(notes.at(0).contains("top and bottom"), qPrintable(notes.at(0)));
    }

    void theCutIsSaidFromAboutFivePercent_data()
    {
        QTest::addColumn<int>("width");
        QTest::addColumn<bool>("said");
        // The canvas of this test is square: 104 x 100 loses 3.8%, 106 x 100 loses 5.7%.
        QTest::newRow("under") << 104 << false;
        QTest::newRow("over") << 106 << true;
    }
    void theCutIsSaidFromAboutFivePercent()
    {
        QFETCH(const int, width);
        QFETCH(const bool, said);
        const auto notes = adviseOn(video(width * 10, 1000), QSize(1000, 1000), 15).notes;
        QCOMPARE(!notes.isEmpty(), said);
    }

    void aSmallVideoWillBeEnlarged_data()
    {
        QTest::addColumn<int>("width");
        QTest::addColumn<bool>("said");
        QTest::newRow("a little under the screen") << 700 << false; // 97%: not worth saying
        QTest::newRow("just over 90%") << 648 << false;
        QTest::newRow("just under 90%") << 647 << true;
        QTest::newRow("tiny") << 320 << true;
    }
    void aSmallVideoWillBeEnlarged()
    {
        QFETCH(const int, width);
        QFETCH(const bool, said);
        const auto info = video(width, static_cast<int>(width * 1258.0 / 720)); // the right proportions
        const auto notes = adviseOn(info, Canvas, 15).notes;
        QCOMPARE(notes.join('\n').contains("enlarged"), said);
    }

    void aSlowerVideoIsNotMadeFaster_data()
    {
        QTest::addColumn<double>("fps");
        QTest::addColumn<bool>("said");
        QTest::newRow("much slower") << 10.0 << true;
        QTest::newRow("a hair under") << 14.6 << false;
        QTest::newRow("the same") << 15.0 << false;
        QTest::newRow("faster") << 30.0 << false;
        QTest::newRow("unknown") << 0.0 << false;
    }
    void aSlowerVideoIsNotMadeFaster()
    {
        QFETCH(const double, fps);
        QFETCH(const bool, said);
        const auto notes = adviseOn(video(720, 1258, fps), Canvas, 15).notes;
        QCOMPARE(notes.join('\n').contains("fewer images per second"), said);
    }

    void withoutTheMeasuresOfTheModuleOnlyTheRateIsChecked()
    {
        const auto notes = adviseOn(video(320, 100, 10), QSize(), 15).notes; // absurd size, but no canvas to judge it by
        QCOMPARE(notes.size(), 1);
        QVERIFY(notes.at(0).contains("fewer images"));
    }

    void aLengthIsWrittenInSecondsThenMinutes()
    {
        QVERIFY(bosun::durationText(0).isEmpty());
        QVERIFY(bosun::durationText(-3).isEmpty());
        QCOMPARE(bosun::durationText(9.4), QStringLiteral("9 s"));
        QCOMPARE(bosun::durationText(59.6), QStringLiteral("1:00"));
        QCOMPARE(bosun::durationText(75.4), QStringLiteral("1:15"));
        QCOMPARE(bosun::durationText(3600), QStringLiteral("60:00"));
    }
};

} // namespace

QTEST_GUILESS_MAIN(TestVideoAdvice)
#include "test_videoadvice.moc"
