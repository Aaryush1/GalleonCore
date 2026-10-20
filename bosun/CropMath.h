// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>

// The rectangle that is kept of a video when it is cropped to the picture of the module:
// always the proportions of that picture, always inside the video. Pure functions in the
// pixels of the video; the window only turns mouse movements into calls of these.
namespace bosun::crop {

// The smallest side of a crop, in pixels of the video (less is not worth enlarging).
inline constexpr int MinWidth = 16;

// The largest crop that fits in `source`, in the middle. Empty when a size is empty.
[[nodiscard]] QRect centered(const QSize &source, const QSize &canvas);

// `wanted` made valid: the proportions of `canvas` (the width decides), at least MinWidth wide
// (or as wide as the video allows), no larger than `source`, and moved inside it if it was not.
[[nodiscard]] QRect fitted(const QRect &wanted, const QSize &source, const QSize &canvas);

// `crop` moved by `delta`, kept inside the video.
[[nodiscard]] QRect moved(const QRect &crop, const QPoint &delta, const QSize &source, const QSize &canvas);

// `crop` made smaller (`factor` above 1) or larger (below 1) around `anchor`, a point of the
// video that stays at the same place of the rectangle. A factor that is not a positive number
// changes nothing.
[[nodiscard]] QRect zoomed(const QRect &crop, double factor, const QPointF &anchor, const QSize &source, const QSize &canvas);

} // namespace bosun::crop
