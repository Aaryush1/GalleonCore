// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "galleon/Layout.h"

#include <cstddef>

namespace galleon::layout {

bool GridGeometry::isValid() const
{
    return columns > 0 && rows > 0 && keyWidthMm > 0.0 && keyHeightMm > 0.0
        && gapXMm >= 0.0 && gapYMm >= 0.0;
}

double GridGeometry::widthMm() const
{
    return columns * keyWidthMm + (columns - 1) * gapXMm;
}

double GridGeometry::heightMm() const
{
    return rows * keyHeightMm + (rows - 1) * gapYMm;
}

double GridGeometry::aspectRatio() const
{
    return isValid() ? widthMm() / heightMm() : 0.0;
}

std::vector<Rect> keyRects(const GridGeometry &grid, const Rect &frame)
{
    if (!grid.isValid() || frame.width <= 0.0 || frame.height <= 0.0)
        return {};

    // Scale each axis separately: if the frame does not have the grid's
    // exact aspect ratio, keys stretch instead of overflowing the frame.
    const double scaleX = frame.width / grid.widthMm();
    const double scaleY = frame.height / grid.heightMm();

    std::vector<Rect> rects;
    rects.reserve(static_cast<std::size_t>(grid.columns) * static_cast<std::size_t>(grid.rows));
    for (int row = 0; row < grid.rows; ++row) {
        for (int column = 0; column < grid.columns; ++column) {
            rects.push_back(Rect{
                frame.x + column * (grid.keyWidthMm + grid.gapXMm) * scaleX,
                frame.y + row * (grid.keyHeightMm + grid.gapYMm) * scaleY,
                grid.keyWidthMm * scaleX,
                grid.keyHeightMm * scaleY,
            });
        }
    }
    return rects;
}

} // namespace galleon::layout
