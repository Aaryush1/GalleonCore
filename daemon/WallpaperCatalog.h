// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

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
    // Several folders laid out the same way: a name that two of them have is listed once, from
    // the first. The order is the same whichever folder a file is in.
    explicit WallpaperCatalog(QStringList roots);

    // Reads the folders again (a file was added or removed while the program runs).
    void rescan();
    [[nodiscard]] const QStringList &roots() const { return m_roots; }

    // The user's own folder, ~/.local/share/galleoncore/wallpaper: where added pictures go. It is
    // not made until something is written in it.
    [[nodiscard]] static QString userRoot();
    // The folders to read: the one asked for on the command line, the one of the environment
    // variable GALLEONCORE_WALLPAPERS, the user's own, the system ones and `buildTree` (the
    // wallpapers of the source tree, when running from the build), those that exist, once each.
    [[nodiscard]] static QStringList defaultRoots(const QString &fromCommandLine, const QString &buildTree = {});

    [[nodiscard]] const QList<WallpaperEntry> &entries() const { return m_entries; }
    // The entry with this name, or nullptr. Names come from D-Bus or the UI:
    // only a listed name is ever turned into a path.
    [[nodiscard]] const WallpaperEntry *find(const QString &name) const;

    [[nodiscard]] static bool isAcceptable(const QString &file);

private:
    QStringList m_roots;
    QList<WallpaperEntry> m_entries;
};

} // namespace galleond
