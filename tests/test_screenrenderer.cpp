// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <ScreenRenderer.h>

#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

using galleond::ScreenRenderer;

namespace {

const QString WidgetFile = QStringLiteral(GALLEON_SOURCE_WIDGETS "/WallpaperClock.qml");
constexpr int Width = 720;
constexpr int Height = 384;

bool similar(QRgb a, QRgb b, int tolerance)
{
    return qAbs(qRed(a) - qRed(b)) <= tolerance && qAbs(qGreen(a) - qGreen(b)) <= tolerance
           && qAbs(qBlue(a) - qBlue(b)) <= tolerance;
}

int countBrightPixels(const QImage &image, const QRect &area)
{
    int count = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            if (qGray(image.pixel(x, y)) > 200)
                ++count;
        }
    }
    return count;
}

} // namespace

class TestScreenRenderer : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void framesHaveTheScreenSize()
    {
        ScreenRenderer renderer(WidgetFile);
        QVERIFY(renderer.isValid());
        const QImage frame = renderer.render({}, QDateTime::currentDateTime(), QSize(Width, Height));
        QCOMPARE(frame.size(), QSize(Width, Height));
    }

    void withoutWallpaperTheBackgroundIsBlackAndTheClockIsDrawn()
    {
        ScreenRenderer renderer(WidgetFile);
        const QImage frame = renderer.render({}, QDateTime(QDate(2026, 10, 18), QTime(12, 34)), QSize(Width, Height));
        QVERIFY(!frame.isNull());
        QVERIFY(similar(frame.pixel(Width - 5, Height - 5), qRgb(0, 0, 0), 8)); // far corner
        // White digits over the left part.
        QVERIFY(countBrightPixels(frame, QRect(20, 80, 380, 200)) > 500);
    }

    void aWallpaperFillsTheScreenWithItsColour()
    {
        QTemporaryDir dir;
        QImage red(1600, 900, QImage::Format_RGB32);
        red.fill(QColor(200, 30, 30));
        const QString file = dir.filePath("red.png");
        QVERIFY(red.save(file));

        ScreenRenderer renderer(WidgetFile);
        const QImage frame = renderer.render(file, QDateTime(QDate(2026, 10, 18), QTime(8, 5)), QSize(Width, Height));
        QVERIFY(!frame.isNull());
        // The right edge is not darkened: the wallpaper colour shows through.
        QVERIFY2(similar(frame.pixel(Width - 5, Height / 2), qRgb(200, 30, 30), 12),
                 qPrintable(QString::number(frame.pixel(Width - 5, Height / 2), 16)));
        // The left edge is darkened for the text.
        QVERIFY(qRed(frame.pixel(5, 5)) < 120);
    }

    void theTimeChangesTheFrame()
    {
        ScreenRenderer renderer(WidgetFile);
        const QImage first = renderer.render({}, QDateTime(QDate(2026, 10, 18), QTime(10, 11)), QSize(Width, Height));
        const QImage second = renderer.render({}, QDateTime(QDate(2026, 10, 18), QTime(23, 59)), QSize(Width, Height));
        QVERIFY(first != second);
        const QImage again = renderer.render({}, QDateTime(QDate(2026, 10, 18), QTime(10, 11)), QSize(Width, Height));
        QCOMPARE(again, first); // same input, same pixels
    }

    void theLanguageAndClockFormatFollowTheLocale()
    {
        ScreenRenderer renderer(WidgetFile);
        const QDateTime afternoon(QDate(2026, 10, 18), QTime(15, 7)); // "15:07" or "3:07 PM"
        const QImage french = renderer.render({}, afternoon, QSize(Width, Height), QLocale(QLocale::French, QLocale::France));
        const QImage american = renderer.render({}, afternoon, QSize(Width, Height), QLocale(QLocale::English, QLocale::UnitedStates));
        const QImage frenchAgain = renderer.render({}, afternoon, QSize(Width, Height), QLocale(QLocale::French, QLocale::France));
        QVERIFY(!french.isNull() && !american.isNull());
        QVERIFY(french != american);
        QCOMPARE(frenchAgain, french);
    }

    void secondsAreShownOnlyWhenAsked()
    {
        // Same locale and instant: only the seconds setting may change the frame.
        ScreenRenderer renderer(WidgetFile);
        const QDateTime moment(QDate(2026, 10, 18), QTime(9, 41, 27));
        const QLocale locale(QLocale::French, QLocale::France);
        const QImage plain = renderer.render({}, moment, QSize(Width, Height), locale);
        const QImage again = renderer.render({}, moment, QSize(Width, Height), locale);
        QCOMPARE(again, plain);
        // 27 s or 28 s: with no seconds shown the frame does not change.
        const QImage nextSecond = renderer.render({}, moment.addSecs(1), QSize(Width, Height), locale);
        QCOMPARE(nextSecond, plain);
    }

    void aBrokenWallpaperGivesNoFrame()
    {
        QTemporaryDir dir;
        QFile broken(dir.filePath("broken.jpg"));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("not an image");
        broken.close();

        ScreenRenderer renderer(WidgetFile);
        QVERIFY(renderer.render(dir.filePath("broken.jpg"), QDateTime::currentDateTime(), QSize(Width, Height)).isNull());
        // And the renderer still works afterwards.
        QVERIFY(!renderer.render({}, QDateTime::currentDateTime(), QSize(Width, Height)).isNull());
    }

    void aMissingWidgetIsReported()
    {
        ScreenRenderer renderer(QStringLiteral("/nonexistent/Widget.qml"));
        QVERIFY(!renderer.isValid());
        QVERIFY(renderer.render({}, QDateTime::currentDateTime(), QSize(Width, Height)).isNull());
    }

    void invalidSizesAreRejected()
    {
        ScreenRenderer renderer(WidgetFile);
        QVERIFY(renderer.render({}, QDateTime::currentDateTime(), QSize()).isNull());
        QVERIFY(renderer.render({}, QDateTime::currentDateTime(), QSize(-1, 10)).isNull());
    }
};

// A GUI application: Qt Quick needs one, even off-screen.
int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    const QGuiApplication app(argc, argv);
    TestScreenRenderer test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_screenrenderer.moc"
