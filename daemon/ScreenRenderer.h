// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QImage>
#include <QLocale>
#include <QString>
#include <QVariantMap>

#include <memory>

namespace galleond {

// Renders one of the widgets/ (WallpaperClock.qml, NowPlaying.qml...) off-screen
// at the size of the Galleon's screen. Uses Qt Quick's software backend: no GPU, no window shown, the same
// pixels on every machine (which also makes it testable).
//
// One instance keeps one scene alive and re-renders it, so it must live and
// be used in the thread of the QGuiApplication.
class ScreenRenderer
{
public:
    // `widgetFile`: path of the widget's .qml file.
    explicit ScreenRenderer(const QString &widgetFile);
    ~ScreenRenderer();
    ScreenRenderer(const ScreenRenderer &) = delete;
    ScreenRenderer &operator=(const ScreenRenderer &) = delete;

    [[nodiscard]] bool isValid() const;

    // A frame of `size` with the widget's properties set from `properties`
    // (those not listed keep their last value). It must have a `ready`
    // property that tells when its images are decoded. A null image on failure.
    [[nodiscard]] QImage render(const QVariantMap &properties, const QSize &size);

    // Makes `image` available to the widget as image://galleon/<id>: how a
    // cover reaches a widget without a file. Only the latest image is kept.
    void setProvidedImage(const QString &id, const QImage &image);

    // For the wallpaper widget: a frame of `size` showing the wallpaper (a file path, or empty for plain
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
