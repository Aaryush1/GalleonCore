// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CropMath.h"

#include <algorithm>
#include <cmath>

namespace bosun::crop {

namespace {

bool usable(const QSize &source, const QSize &canvas)
{
    return !source.isEmpty() && !canvas.isEmpty();
}

// The height that goes with a width in the proportions of the canvas.
int heightFor(int width, const QSize &canvas)
{
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(width) * canvas.height() / canvas.width())));
}

// The widest crop of the canvas proportions that the video holds.
int widestWidth(const QSize &source, const QSize &canvas)
{
    const double byHeight = static_cast<double>(source.height()) * canvas.width() / canvas.height();
    return std::max(1, static_cast<int>(std::floor(std::min(static_cast<double>(source.width()), byHeight))));
}

} // namespace

QRect centered(const QSize &source, const QSize &canvas)
{
    if (!usable(source, canvas))
        return {};
    const int width = widestWidth(source, canvas);
    const int height = std::min(source.height(), heightFor(width, canvas));
    return {(source.width() - width) / 2, (source.height() - height) / 2, width, height};
}

QRect fitted(const QRect &wanted, const QSize &source, const QSize &canvas)
{
    if (!usable(source, canvas))
        return {};
    const int largest = widestWidth(source, canvas);
    const int width = std::clamp(wanted.width(), std::min(MinWidth, largest), largest);
    const int height = std::min(source.height(), heightFor(width, canvas));
    const int x = std::clamp(wanted.x(), 0, source.width() - width);
    const int y = std::clamp(wanted.y(), 0, source.height() - height);
    return {x, y, width, height};
}

QRect moved(const QRect &crop, const QPoint &delta, const QSize &source, const QSize &canvas)
{
    return fitted(crop.translated(delta), source, canvas);
}

QRect zoomed(const QRect &crop, double factor, const QPointF &anchor, const QSize &source, const QSize &canvas)
{
    if (!usable(source, canvas) || !std::isfinite(factor) || factor <= 0 || crop.isEmpty())
        return fitted(crop, source, canvas);
    const double width = crop.width() / factor;
    const double height = crop.height() / factor;
    // The anchor keeps its relative place in the rectangle.
    const double relativeX = (anchor.x() - crop.x()) / crop.width();
    const double relativeY = (anchor.y() - crop.y()) / crop.height();
    const QRect wanted(static_cast<int>(std::lround(anchor.x() - (relativeX * width))),
                       static_cast<int>(std::lround(anchor.y() - (relativeY * height))), static_cast<int>(std::lround(width)),
                       static_cast<int>(std::lround(height)));
    return fitted(wanted, source, canvas);
}

} // namespace bosun::crop
