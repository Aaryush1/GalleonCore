// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

// Pure geometry for spreading one image over the key grid.
// No Qt, no I/O: unit-tested, and shared by galleond and the settings app.
//
// Physical sizes are not hard-coded here: they come from the device
// description in data/devices/, filled in from real measurements.

#include <vector>

namespace galleon::layout {

// Physical description of a key grid, in millimetres.
struct GridGeometry
{
    int columns = 0;
    int rows = 0;
    double keyWidthMm = 0.0;
    double keyHeightMm = 0.0;
    double gapXMm = 0.0; // horizontal space between two keys
    double gapYMm = 0.0; // vertical space between two keys

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] double widthMm() const;
    [[nodiscard]] double heightMm() const;
    // Width / height of the whole grid, gaps included. The crop frame drawn
    // on the source image must use this ratio for the result to look
    // continuous across the keys.
    [[nodiscard]] double aspectRatio() const;
};

struct Rect
{
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    friend bool operator==(const Rect &, const Rect &) = default;
};

// Splits `frame` (the area of the source image chosen by the user, which
// covers the whole grid) into one rectangle per key, row by row, left to
// right. The parts of the frame that fall into the gaps are left out, as if
// the image were seen through the grid.
//
// Returns an empty vector if the geometry is invalid or the frame is empty.
std::vector<Rect> keyRects(const GridGeometry &grid, const Rect &frame);

} // namespace galleon::layout
