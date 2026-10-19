// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "DesktopEntries.h"
#include "PagesConfig.h"

#include <QImage>
#include <QString>

namespace galleond {

// Draws the picture of one key: its icon and its label. Plain QPainter, so a
// tile costs little and renders the same off-screen.
//
// An icon name is looked up first as a file in the user's icon folder (the
// folder of the pages file), then in the desktop's icon theme. A file there is
// untrusted input like any other image: a regular file, no symbolic link, not
// large, of a sane size in pixels. An icon that is not found is not an error:
// the tile shows its label alone.
class TileRenderer
{
public:
    static constexpr qint64 MaxIconFileBytes = 2LL * 1024 * 1024;
    static constexpr int MaxIconDimension = 2048;

    explicit TileRenderer(QString iconFolder = {});

    // `state` is "playing", "paused" or "stopped" (or empty) and picks the icon
    // of a key that has one per state; `pressed` is the feedback while held.
    // `fallbackIconFile`: the absolute path of an image to use when the key's icon
    // name gives nothing: an application's own icon, from its .desktop file. It
    // never comes from the pages file (which only allows names), and gets the
    // same checks as any icon file.
    // `tint`: the colour of the current theme (0xRRGGBB, or -1 for none): the key's
    // background is a darker shade of it, and lighter while held.
    [[nodiscard]] QImage render(const KeyConfig &key, const QString &state, bool pressed,
                                const QString &fallbackIconFile = {}, qint64 tint = -1) const;

    // The key background for a theme colour: the same hue, much darker.
    [[nodiscard]] static QColor tintBackground(std::uint32_t rgb);

    // What is drawn for a key: its configuration, where an application key that
    // has no label and no icon takes the name and the icon of `app` (the
    // application it starts; invalid when it was not found). `iconFile` gets the
    // application's icon when that one is given by path. Used by the daemon and
    // by the offline preview, so that both show the same keys.
    [[nodiscard]] static KeyConfig effectiveKey(const KeyConfig &key, const DesktopApp &app, QString &iconFile);

    // An image file as an icon of at most `size` pixels: a regular file (a link
    // is followed), a PNG, JPEG or SVG of a sane size; a null image otherwise.
    [[nodiscard]] static QImage loadIconFile(const QString &path, int size);

    // The 160 x 160 picture of an empty key.
    [[nodiscard]] QImage blank(qint64 tint = -1) const;

private:
    [[nodiscard]] QImage loadIcon(const QString &name, int size) const;

    QString m_iconFolder;
};

} // namespace galleond
