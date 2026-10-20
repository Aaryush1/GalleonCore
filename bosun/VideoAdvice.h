// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <VideoProbe.h>

#include <QSize>
#include <QString>
#include <QStringList>

namespace bosun {

// What the window says of a video that ffprobe has described.
struct VideoAdvice
{
    QString details;    // "704 × 1280 · 24.0 fps · 9 s · h264"
    QStringList notes;  // what will not be quite right once the video is on the module
};

// Pure: the same video, canvas and rate give the same words.
// `canvas` is the size a video is cut to (empty when the module's measures are missing: then
// nothing is said of the proportions or the size); `displayFps` is the rate of the screensaver.
// The module scales a video to cover the picture and cuts what goes beyond, so a video of
// other proportions loses its edges; one narrower than the screen is enlarged; one with fewer
// images per second than the display rate is shown at its own rate (none are made up).
[[nodiscard]] VideoAdvice adviseOn(const galleond::VideoInfo &info, const QSize &canvas, int displayFps);

// "9 s", "1:16"; empty for no length.
[[nodiscard]] QString durationText(double seconds);

} // namespace bosun
