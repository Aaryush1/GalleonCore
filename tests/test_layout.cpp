// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// The geometry below is made up for the tests: round numbers that make the
// expected rectangles easy to check by hand. Real values live in
// data/devices/ and come from measurements.

#include <galleon/Layout.h>

#include <QTest>

using namespace galleon::layout;

namespace {

// 4 × 3 keys of 10 × 10 mm with 2 mm gaps: the grid is 46 × 34 mm.
GridGeometry testGrid()
{
    GridGeometry grid;
    grid.columns = 4;
    grid.rows = 3;
    grid.keyWidthMm = 10.0;
    grid.keyHeightMm = 10.0;
    grid.gapXMm = 2.0;
    grid.gapYMm = 2.0;
    return grid;
}

} // namespace

class TestLayout : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void gridSizeIncludesGaps()
    {
        const auto grid = testGrid();
        QCOMPARE(grid.widthMm(), 46.0);
        QCOMPARE(grid.heightMm(), 34.0);
        QCOMPARE(grid.aspectRatio(), 46.0 / 34.0);
    }

    void oneRectPerKeyRowByRow()
    {
        // 1 px per mm, so pixels equal millimetres.
        const auto rects = keyRects(testGrid(), Rect{0.0, 0.0, 46.0, 34.0});
        QCOMPARE(rects.size(), std::size_t{12});
        QCOMPARE(rects.front(), (Rect{0.0, 0.0, 10.0, 10.0}));
        QCOMPARE(rects[1], (Rect{12.0, 0.0, 10.0, 10.0}));
        QCOMPARE(rects[4], (Rect{0.0, 12.0, 10.0, 10.0}));
        QCOMPARE(rects.back(), (Rect{36.0, 24.0, 10.0, 10.0}));
    }

    void lastKeyEndsAtFrameEdge()
    {
        const Rect frame{100.0, 50.0, 920.0, 680.0}; // 20 px per mm
        const auto rects = keyRects(testGrid(), frame);
        const auto &last = rects.back();
        QCOMPARE(last.x + last.width, frame.x + frame.width);
        QCOMPARE(last.y + last.height, frame.y + frame.height);
    }

    void zeroGapsTileTheFrame()
    {
        auto grid = testGrid();
        grid.gapXMm = 0.0;
        grid.gapYMm = 0.0;
        const auto rects = keyRects(grid, Rect{0.0, 0.0, 40.0, 30.0});
        QCOMPARE(rects[1].x, rects[0].x + rects[0].width);
        QCOMPARE(rects[4].y, rects[0].y + rects[0].height);
    }

    void invalidInputYieldsNothing()
    {
        QVERIFY(keyRects(GridGeometry{}, Rect{0.0, 0.0, 46.0, 34.0}).empty());
        QVERIFY(keyRects(testGrid(), Rect{0.0, 0.0, 0.0, 34.0}).empty());

        auto negativeGap = testGrid();
        negativeGap.gapXMm = -1.0;
        QVERIFY(keyRects(negativeGap, Rect{0.0, 0.0, 46.0, 34.0}).empty());
        QCOMPARE(GridGeometry{}.aspectRatio(), 0.0);
    }
};

QTEST_GUILESS_MAIN(TestLayout)
#include "test_layout.moc"
