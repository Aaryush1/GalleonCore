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
const QString NowPlayingFile = QStringLiteral(GALLEON_SOURCE_WIDGETS "/NowPlaying.qml");
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

    // ---- the "now playing" widget (720 x 384: cover 24..360, bar at y 314..320, x 388..696)

    QVariantMap track(double position, double duration, bool playing = true, const QString &coverId = {})
    {
        return {{"title", "A Title"},
                {"artist", "An Artist"},
                {"positionSeconds", position},
                {"durationSeconds", duration},
                {"playing", playing},
                {"coverId", coverId},
                {"now", QDateTime(QDate(2026, 10, 19), QTime(7, 5))},
                {"locale", QVariant::fromValue(QLocale(QLocale::French, QLocale::France))}};
    }

    void theCoverAndTheTimelineAreDrawn()
    {
        ScreenRenderer renderer(NowPlayingFile);
        QVERIFY(renderer.isValid());
        QImage cover(300, 300, QImage::Format_RGB32);
        cover.fill(QColor(200, 30, 30));
        renderer.setProvidedImage("c1", cover);

        const QImage frame = renderer.render(track(50, 100, true, "c1"), QSize(Width, Height));
        QVERIFY(!frame.isNull());
        QVERIFY(similar(frame.pixel(190, 190), qRgb(200, 30, 30), 14)); // the cover
        QVERIFY(similar(frame.pixel(465, 317), qRgb(0x4c, 0xaf, 0x50), 14)); // 25 %: filled, green
        QVERIFY(similar(frame.pixel(619, 317), qRgb(0x33, 0x3a, 0x44), 14)); // 75 %: the empty track
        QVERIFY(similar(frame.pixel(700, 200), qRgb(0x14, 0x17, 0x1c), 6));  // right margin: background
    }

    void theThemeLineIsDrawnAlongTheTopOfBothScreens()
    {
        const QColor red(0xe5, 0x39, 0x35);
        ScreenRenderer clock(WidgetFile);
        QImage frame = clock.render({{"themeName", "Son"}, {"themeColor", red}, {"now", QDateTime(QDate(2026, 10, 19), QTime(7, 5))}},
                                    QSize(Width, Height));
        QVERIFY(!frame.isNull());
        QVERIFY(similar(frame.pixel(5, 2), red.rgb(), 6));
        QVERIFY(similar(frame.pixel(Width - 5, 3), red.rgb(), 6));

        ScreenRenderer nowPlaying(NowPlayingFile);
        QVariantMap properties = track(50, 100, true, "");
        properties.insert("themeName", "Son");
        properties.insert("themeColor", red);
        frame = nowPlaying.render(properties, QSize(Width, Height));
        QVERIFY(similar(frame.pixel(5, 2), red.rgb(), 6));
        QVERIFY(similar(frame.pixel(Width / 2, 3), red.rgb(), 6));

        // No theme: no line.
        properties.insert("themeName", "");
        frame = nowPlaying.render(properties, QSize(Width, Height));
        QVERIFY(similar(frame.pixel(5, 2), qRgb(0x14, 0x17, 0x1c), 6));
    }

    void anApplicationIconIsShownWholeWithRoomAroundIt()
    {
        ScreenRenderer renderer(NowPlayingFile);
        QImage icon(256, 256, QImage::Format_ARGB32);
        icon.fill(QColor(220, 120, 0)); // a full square: a crop would fill the whole cover area
        renderer.setProvidedImage("icon1", icon);
        QVariantMap properties = track(10, 100, true, "icon1");
        properties["coverIsIcon"] = true;
        const QImage frame = renderer.render(properties, QSize(Width, Height));
        QVERIFY(similar(frame.pixel(190, 190), qRgb(220, 120, 0), 14));          // the icon, in the middle
        QVERIFY(similar(frame.pixel(36, 36), qRgb(0x23, 0x28, 0x30), 8));        // the room around it
        properties["coverIsIcon"] = false;                                       // the same picture as a cover fills the square
        QVERIFY(similar(renderer.render(properties, QSize(Width, Height)).pixel(36, 36), qRgb(220, 120, 0), 14));
    }

    void aPausedTrackHasAGreyBar()
    {
        ScreenRenderer renderer(NowPlayingFile);
        const QImage frame = renderer.render(track(50, 100, false), QSize(Width, Height));
        QVERIFY(similar(frame.pixel(465, 317), qRgb(0x8a, 0x92, 0x9d), 14));
    }

    void withoutACoverThereIsAPlaceholder()
    {
        ScreenRenderer renderer(NowPlayingFile);
        const QImage frame = renderer.render(track(0, 100), QSize(Width, Height));
        QVERIFY(!frame.isNull());
        QVERIFY(similar(frame.pixel(40, 40), qRgb(0x23, 0x28, 0x30), 8)); // the placeholder square
    }

    void aStreamHasNoBar()
    {
        ScreenRenderer renderer(NowPlayingFile);
        const QImage frame = renderer.render(track(12, 0), QSize(Width, Height));
        QVERIFY(similar(frame.pixel(465, 317), qRgb(0x14, 0x17, 0x1c), 6));
        QVERIFY(similar(frame.pixel(619, 317), qRgb(0x14, 0x17, 0x1c), 6));
    }

    void theTimelineMovesTheBarAndTheTimes()
    {
        ScreenRenderer renderer(NowPlayingFile);
        const QImage early = renderer.render(track(10, 100), QSize(Width, Height));
        const QImage late = renderer.render(track(90, 100), QSize(Width, Height));
        QVERIFY(similar(early.pixel(619, 317), qRgb(0x33, 0x3a, 0x44), 14)); // 75 %: not reached at 10 %
        QVERIFY(similar(late.pixel(619, 317), qRgb(0x4c, 0xaf, 0x50), 14));  // reached at 90 %
        QVERIFY(early != late);
        // A second later, only the time text changes.
        QVERIFY(renderer.render(track(59, 600), QSize(Width, Height)) != renderer.render(track(61, 600), QSize(Width, Height)));
    }

    void aPositionOutsideTheRangeStaysInsideTheBar()
    {
        ScreenRenderer renderer(NowPlayingFile);
        for (const double position : {-50.0, 1e9, 0.0}) {
            const QImage frame = renderer.render(track(position, 100), QSize(Width, Height));
            QVERIFY(!frame.isNull());
            QVERIFY(similar(frame.pixel(700, 317), qRgb(0x14, 0x17, 0x1c), 6)); // never past the end of the bar
        }
    }

    void aVeryLongTitleStaysInItsColumn()
    {
        ScreenRenderer renderer(NowPlayingFile);
        QVariantMap properties = track(10, 100);
        properties["title"] = QString(300, QLatin1Char('W')) + QStringLiteral(" and more words after");
        properties["artist"] = QString(300, QLatin1Char('M'));
        const QImage frame = renderer.render(properties, QSize(Width, Height));
        for (int y = 0; y < Height; ++y)
            QVERIFY(similar(frame.pixel(Width - 8, y), qRgb(0x14, 0x17, 0x1c), 6)); // the right margin is never painted
    }

    void aCoverThatIsNotThereGivesNoFrame()
    {
        ScreenRenderer renderer(NowPlayingFile);
        // An id that no image was provided for: no half-drawn screen.
        QVERIFY(renderer.render(track(10, 100, true, "never-provided"), QSize(Width, Height)).isNull());
        // And the renderer recovers.
        QVERIFY(!renderer.render(track(10, 100), QSize(Width, Height)).isNull());
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
