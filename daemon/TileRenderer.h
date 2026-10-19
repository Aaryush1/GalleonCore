// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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
    [[nodiscard]] QImage render(const KeyConfig &key, const QString &state, bool pressed) const;

    // The 160 x 160 picture of an empty key.
    [[nodiscard]] QImage blank() const;

private:
    [[nodiscard]] QImage loadIcon(const QString &name, int size) const;

    QString m_iconFolder;
};

} // namespace galleond
