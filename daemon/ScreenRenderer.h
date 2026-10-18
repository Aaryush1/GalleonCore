// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QImage>
#include <QLocale>
#include <QString>

#include <memory>

namespace galleond {

// Renders widgets/WallpaperClock.qml off-screen at the size of the Galleon's
// screen. Uses Qt Quick's software backend: no GPU, no window shown, the same
// pixels on every machine (which also makes it testable).
//
// One instance keeps one scene alive and re-renders it, so it must live and
// be used in the thread of the QGuiApplication.
class ScreenRenderer
{
public:
    // `widgetFile`: path of WallpaperClock.qml.
    explicit ScreenRenderer(const QString &widgetFile);
    ~ScreenRenderer();
    ScreenRenderer(const ScreenRenderer &) = delete;
    ScreenRenderer &operator=(const ScreenRenderer &) = delete;

    [[nodiscard]] bool isValid() const;

    // A frame of `size` showing the wallpaper (a file path, or empty for plain
    // black) with `now` over it, written in the language and the time format of
    // `locale` (the system's by default). A null image on failure.
    [[nodiscard]] QImage render(const QString &wallpaperFile,
                                const QDateTime &now,
                                const QSize &size,
                                const QLocale &locale = QLocale());

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace galleond
