// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <CropMath.h>

#include <QTest>

using namespace bosun::crop;

namespace {

const QSize Canvas(720, 1258);

class TestCropMath : public QObject
{
    Q_OBJECT

    // What every crop must be, whatever was asked for.
    static void checkValid(const QRect &crop, const QSize &source, const QSize &canvas, const QString &what)
    {
        const QByteArray message = what.toUtf8();
        QVERIFY2(QRect(QPoint(0, 0), source).contains(crop), message.constData());
        QVERIFY2(crop.width() >= 1 && crop.height() >= 1, message.constData());
        const double expected = static_cast<double>(crop.width()) * canvas.height() / canvas.width();
        QVERIFY2(std::abs(crop.height() - expected) <= 1.0 || crop.height() == source.height(), message.constData());
    }

private Q_SLOTS:
    void aWideVideoIsCroppedInTheMiddleOfItsWidth()
    {
        // 1080 high is all there is: 618 wide in the proportions of the canvas.
        QCOMPARE(centered(QSize(1920, 1080), Canvas), QRect(651, 0, 618, 1080));
    }

    void aNarrowVideoIsCroppedInTheMiddleOfItsHeight()
    {
        QCOMPARE(centered(QSize(704, 1280), Canvas), QRect(0, 25, 704, 1230));
    }

    void aVideoOfTheRightProportionsIsKeptWhole()
    {
        QCOMPARE(centered(QSize(720, 1258), Canvas), QRect(0, 0, 720, 1258));
        QCOMPARE(centered(QSize(1440, 2516), Canvas), QRect(0, 0, 1440, 2516));
    }

    void nothingIsCroppedOfNothing()
    {
        QVERIFY(centered(QSize(), Canvas).isEmpty());
        QVERIFY(centered(QSize(100, 100), QSize()).isEmpty());
        QVERIFY(fitted(QRect(0, 0, 10, 10), QSize(0, 5), Canvas).isEmpty());
    }

    void aWantedRectangleIsMadeValid_data()
    {
        QTest::addColumn<QRect>("wanted");
        QTest::addColumn<QRect>("expected");
        const QSize source(1920, 1080); // the widest crop is 618 wide, 1080 high
        Q_UNUSED(source)
        QTest::newRow("already good") << QRect(100, 0, 618, 1080) << QRect(100, 0, 618, 1080);
        QTest::newRow("wrong proportions: the width decides") << QRect(100, 0, 309, 999) << QRect(100, 0, 309, 540);
        QTest::newRow("too large") << QRect(0, 0, 5000, 5000) << QRect(0, 0, 618, 1080);
        QTest::newRow("too small") << QRect(50, 50, 2, 2) << QRect(50, 50, 16, 28);
        QTest::newRow("pushed out on the left") << QRect(-300, 0, 618, 1080) << QRect(0, 0, 618, 1080);
        QTest::newRow("pushed out on the right") << QRect(1900, 0, 618, 1080) << QRect(1302, 0, 618, 1080);
        QTest::newRow("pushed out at the bottom") << QRect(100, 900, 309, 540) << QRect(100, 540, 309, 540);
    }
    void aWantedRectangleIsMadeValid()
    {
        QFETCH(const QRect, wanted);
        QFETCH(const QRect, expected);
        QCOMPARE(fitted(wanted, QSize(1920, 1080), Canvas), expected);
    }

    void aVideoSmallerThanTheMinimumGivesAsMuchAsItHolds()
    {
        const QRect crop = fitted(QRect(0, 0, 100, 100), QSize(10, 10), Canvas);
        checkValid(crop, QSize(10, 10), Canvas, "tiny video");
        QVERIFY(crop.width() <= 10);
    }

    void movingKeepsTheSizeAndStaysInsideTheVideo()
    {
        const QSize source(1920, 1080);
        const QRect crop(651, 0, 618, 1080);
        QCOMPARE(moved(crop, QPoint(100, 0), source, Canvas), QRect(751, 0, 618, 1080));
        QCOMPARE(moved(crop, QPoint(-5000, 0), source, Canvas), QRect(0, 0, 618, 1080));
        QCOMPARE(moved(crop, QPoint(5000, 0), source, Canvas), QRect(1302, 0, 618, 1080));
        QCOMPARE(moved(crop, QPoint(0, 300), source, Canvas), crop); // no room to go down: it is as high as the video
    }

    void zoomingKeepsTheAnchorWhereItIs()
    {
        const QSize source(1920, 1080);
        const QRect crop(500, 0, 618, 1080);
        // Twice as close, around the middle of the crop: half the size, same centre.
        const QRect closer = zoomed(crop, 2.0, QPointF(809, 540), source, Canvas);
        QCOMPARE(closer, QRect(655, 270, 309, 540));
        // Around the top left corner: that corner stays.
        const QRect corner = zoomed(crop, 2.0, QPointF(500, 0), source, Canvas);
        QCOMPARE(corner.topLeft(), QPoint(500, 0));
        QCOMPARE(corner.size(), QSize(309, 540));
    }

    void zoomingOutIsStoppedByTheVideo()
    {
        const QRect crop(655, 270, 309, 540);
        const QRect widest = zoomed(crop, 0.01, QPointF(809, 540), QSize(1920, 1080), Canvas);
        QCOMPARE(widest.size(), QSize(618, 1080));
        QCOMPARE(widest.y(), 0);
    }

    void zoomingInIsStoppedByTheMinimum()
    {
        const QRect closest = zoomed(QRect(655, 270, 309, 540), 1000.0, QPointF(809, 540), QSize(1920, 1080), Canvas);
        QCOMPARE(closest.width(), MinWidth);
    }

    void aFactorThatIsNotAPositiveNumberChangesNothing()
    {
        const QRect crop(655, 270, 309, 540);
        for (const double factor : {0.0, -2.0, std::nan(""), HUGE_VAL})
            QCOMPARE(zoomed(crop, factor, QPointF(800, 500), QSize(1920, 1080), Canvas), crop);
    }

    void whateverIsAskedTheCropIsValid()
    {
        for (const QSize &source : {QSize(1920, 1080), QSize(704, 1280), QSize(720, 1258), QSize(3840, 2160), QSize(100, 3000), QSize(2000, 40)}) {
            for (const int x : {-5000, -1, 0, 17, 900, 100000}) {
                for (const int width : {-5, 0, 1, 16, 333, 5000}) {
                    const QRect crop = fitted(QRect(x, x / 2, width, width), source, Canvas);
                    checkValid(crop, source, Canvas, QStringLiteral("fitted %1x%2 x=%3 w=%4").arg(source.width()).arg(source.height()).arg(x).arg(width));
                    for (const double factor : {0.3, 0.9, 1.0, 1.7, 40.0}) {
                        const QRect again = zoomed(crop, factor, QPointF(x, x), source, Canvas);
                        checkValid(again, source, Canvas, QStringLiteral("zoomed %1").arg(factor));
                    }
                }
            }
        }
    }
};

} // namespace

QTEST_GUILESS_MAIN(TestCropMath)
#include "test_cropmath.moc"
