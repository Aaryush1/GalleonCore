// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VideoAdvice.h"

#include <QCoreApplication>

#include <cmath>

namespace bosun {

namespace {

constexpr double CutWorthSaying = 0.05; // below 5% of the picture lost, say nothing
constexpr int SmallWidthPercent = 90;   // narrower than this share of the screen: "enlarged"

QString words(const char *text)
{
    return QCoreApplication::translate("VideoAdvice", text);
}

// The share of the picture that is cut off, and where.
struct Cut
{
    double share = 0;
    bool atTheSides = false;
};

Cut cutOf(const galleond::VideoInfo &info, const QSize &canvas)
{
    const double videoRatio = static_cast<double>(info.width) / info.height;
    const double canvasRatio = static_cast<double>(canvas.width()) / canvas.height();
    if (videoRatio > canvasRatio)
        return {.share = 1 - (canvasRatio / videoRatio), .atTheSides = true};
    return {.share = 1 - (videoRatio / canvasRatio), .atTheSides = false};
}

QString detailsOf(const galleond::VideoInfo &info)
{
    QStringList parts{QStringLiteral("%1 × %2").arg(info.width).arg(info.height)};
    if (info.fps > 0)
        parts << words("%1 fps").arg(info.fps, 0, 'f', 1);
    if (info.seconds > 0)
        parts << durationText(info.seconds);
    if (!info.codec.isEmpty())
        parts << info.codec;
    return parts.join(QStringLiteral(" · "));
}

} // namespace

QString durationText(double seconds)
{
    if (seconds <= 0)
        return {};
    const int whole = static_cast<int>(std::lround(seconds));
    return whole >= 60 ? QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'))
                       : QStringLiteral("%1 s").arg(whole);
}

VideoAdvice adviseOn(const galleond::VideoInfo &info, const QSize &canvas, int displayFps)
{
    VideoAdvice advice;
    if (!info.valid) {
        advice.notes << words("ffprobe could not read this video.");
        return advice;
    }
    advice.details = detailsOf(info);
    if (!canvas.isEmpty()) {
        if (const Cut cut = cutOf(info, canvas); cut.share >= CutWorthSaying) {
            advice.notes << words("About %1% of the picture is cut off %2 (the module needs the proportions of %3 × %4).")
                                .arg(std::lround(cut.share * 100))
                                .arg(cut.atTheSides ? words("at the sides") : words("at the top and bottom"))
                                .arg(canvas.width())
                                .arg(canvas.height());
        }
        if (info.width < (canvas.width() * SmallWidthPercent) / 100)
            advice.notes << words("Smaller than the screen (%1 px wide): it will be enlarged.").arg(canvas.width());
    }
    if (info.fps > 0 && info.fps < displayFps - 0.5)
        advice.notes << words("The video has fewer images per second than the setting (%1): none are made up.").arg(displayFps);
    return advice;
}

} // namespace bosun
