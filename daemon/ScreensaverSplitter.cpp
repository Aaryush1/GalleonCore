// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScreensaverSplitter.h"

#include <galleon/Layout.h>

#include <algorithm>
#include <cmath>

namespace galleond {

namespace {

constexpr int MaxCanvasHeight = 4096; // a sane bound for the frame buffer

QImage crop(const QImage &frame, double x, double y, double width, double height, double scale, const QSize &out)
{
    const QRect area(static_cast<int>(std::lround(x * scale)), static_cast<int>(std::lround(y * scale)),
                     std::max(1, static_cast<int>(std::lround(width * scale))),
                     std::max(1, static_cast<int>(std::lround(height * scale))));
    const QRect inside = area.intersected(frame.rect());
    if (inside.isEmpty())
        return {};
    return frame.copy(inside).scaled(out, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

} // namespace

ScreensaverSplitter::ScreensaverSplitter(const ScreensaverConfig &config)
    : m_config(config)
{
    if (!config.hasGeometry)
        return;
    const double gridWidth = (galleon::protocol::KeyColumns * config.keyWidthMm) + ((galleon::protocol::KeyColumns - 1) * config.gapXMm);
    const double gridHeight = (galleon::protocol::KeyRows * config.keyHeightMm) + ((galleon::protocol::KeyRows - 1) * config.gapYMm);
    m_widthMm = std::max(config.screenWidthMm, gridWidth);
    m_heightMm = config.screenHeightMm + config.screenToKeysMm + gridHeight;
    if (m_widthMm <= 0 || m_heightMm <= 0)
        return;
    m_screenX = (m_widthMm - config.screenWidthMm) / 2;
    m_gridX = (m_widthMm - gridWidth) / 2;
    m_gridY = config.screenHeightMm + config.screenToKeysMm;

    const int height = static_cast<int>(std::lround(galleon::protocol::ScreenWidth * m_heightMm / m_widthMm));
    if (height < 2 || height > MaxCanvasHeight)
        return;
    m_canvas = QSize(galleon::protocol::ScreenWidth, height);
    m_valid = true;
}

ScreensaverSplitter::Frames ScreensaverSplitter::split(const QImage &frame) const
{
    Frames out;
    if (!m_valid || frame.isNull())
        return out;
    // A frame with other pixel dimensions but the same proportions is fine.
    const double scale = frame.width() / m_widthMm;
    if (frame.width() < 2 || std::abs(frame.height() - (m_heightMm * scale)) > 2.0)
        return out;

    out.screen = crop(frame, m_screenX, 0, m_config.screenWidthMm, m_config.screenHeightMm, scale,
                      QSize(galleon::protocol::ScreenWidth, galleon::protocol::ScreenHeight));

    galleon::layout::GridGeometry grid;
    grid.columns = galleon::protocol::KeyColumns;
    grid.rows = galleon::protocol::KeyRows;
    grid.keyWidthMm = m_config.keyWidthMm;
    grid.keyHeightMm = m_config.keyHeightMm;
    grid.gapXMm = m_config.gapXMm;
    grid.gapYMm = m_config.gapYMm;
    const auto rects = galleon::layout::keyRects(grid, {.x=m_gridX, .y=m_gridY, .width=grid.widthMm(), .height=grid.heightMm()});
    for (std::size_t i = 0; i < rects.size() && i < out.keys.size(); ++i) {
        out.keys[i] = crop(frame, rects[i].x, rects[i].y, rects[i].width, rects[i].height, scale,
                           QSize(galleon::protocol::KeyImageSize, galleon::protocol::KeyImageSize));
    }
    return out;
}

} // namespace galleond
