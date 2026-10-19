// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "PagesConfig.h"

#include <galleon/Protocol.h>

#include <QImage>
#include <QSize>

#include <array>

namespace galleond {

// One video frame cut for the module: the part behind the top screen and the
// part behind each of the 12 keys, as if the picture were seen through them
// (the gaps between the keys are left out, so the image looks continuous).
//
// The picture is the module seen from the front, in millimetres: the screen on
// top, then the grid of keys below it, both centred on the same vertical axis.
// Pure functions: no I/O, testable.
class ScreensaverSplitter
{
public:
    struct Frames
    {
        QImage screen;                                          // 720 x 384
        std::array<QImage, galleon::protocol::KeyCount> keys;   // 160 x 160 each
        [[nodiscard]] bool isValid() const { return !screen.isNull(); }
    };

    explicit ScreensaverSplitter(const ScreensaverConfig &config);

    [[nodiscard]] bool isValid() const { return m_valid; }
    // The size a video frame must have: 720 px wide (the screen's width in
    // pixels), as high as the module is in proportion.
    [[nodiscard]] QSize canvasSize() const { return m_canvas; }

    // Cuts `frame`, which must be canvasSize() (or the same proportions).
    // Empty frames when the picture is null or the splitter is not valid.
    [[nodiscard]] Frames split(const QImage &frame) const;

private:
    bool m_valid = false;
    QSize m_canvas;
    double m_widthMm = 0, m_heightMm = 0;
    double m_screenX = 0;                 // the screen's left edge, in mm
    double m_gridX = 0, m_gridY = 0;      // the key grid's top left corner, in mm
    ScreensaverConfig m_config;
};

} // namespace galleond
