// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

namespace galleond {

struct WallpaperEntry
{
    QString style; // the folder: "manga-classic"
    QString name;  // "manga-classic/manga-classic-01.jpg": unique, used on D-Bus
    QString path;  // absolute path of the file
};

// The wallpapers found under a folder laid out like public/wallpaper:
// one sub-folder per style, JPEG or PNG files inside.
//
// Image files are untrusted input: a file is listed only if it is a regular
// file (no symbolic link), small enough, a JPEG or PNG by its content, and
// of a sane size in pixels. Anything else is skipped silently.
class WallpaperCatalog
{
public:
    static constexpr qint64 MaxFileBytes = 32LL * 1024 * 1024;
    static constexpr int MaxDimension = 8192;

    explicit WallpaperCatalog(const QString &root = {});

    [[nodiscard]] const QList<WallpaperEntry> &entries() const { return m_entries; }
    // The entry with this name, or nullptr. Names come from D-Bus or the UI:
    // only a listed name is ever turned into a path.
    [[nodiscard]] const WallpaperEntry *find(const QString &name) const;

    [[nodiscard]] static bool isAcceptable(const QString &file);

private:
    QList<WallpaperEntry> m_entries;
};

} // namespace galleond
