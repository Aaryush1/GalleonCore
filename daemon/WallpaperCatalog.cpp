// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WallpaperCatalog.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>

#include <algorithm>

namespace galleond {

namespace {

bool hasImageExtension(const QString &suffix)
{
    const QString lower = suffix.toLower();
    return lower == QLatin1String("jpg") || lower == QLatin1String("jpeg") || lower == QLatin1String("png");
}

} // namespace

bool WallpaperCatalog::isAcceptable(const QString &file)
{
    const QFileInfo info(file);
    if (!info.isFile() || info.isSymLink() || !hasImageExtension(info.suffix()))
        return false;
    if (info.size() <= 0 || info.size() > MaxFileBytes)
        return false;

    // The content decides, not the extension. size() reads the header only.
    QImageReader reader(info.absoluteFilePath());
    reader.setDecideFormatFromContent(true);
    const QByteArray format = reader.format();
    if (format != "jpeg" && format != "png")
        return false;
    const QSize size = reader.size();
    return size.isValid() && size.width() > 0 && size.height() > 0 && size.width() <= MaxDimension
           && size.height() <= MaxDimension;
}

WallpaperCatalog::WallpaperCatalog(const QString &root)
{
    if (root.isEmpty())
        return;
    const QDir rootDir(root);
    if (!rootDir.exists())
        return;

    const QFileInfoList styles = rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &styleInfo : styles) {
        if (styleInfo.isSymLink())
            continue;
        const QDir styleDir(styleInfo.absoluteFilePath());
        const QFileInfoList files = styleDir.entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo &file : files) {
            if (!isAcceptable(file.absoluteFilePath()))
                continue;
            m_entries.append({styleInfo.fileName(),
                              styleInfo.fileName() + QLatin1Char('/') + file.fileName(),
                              file.absoluteFilePath()});
        }
    }
}

const WallpaperEntry *WallpaperCatalog::find(const QString &name) const
{
    const auto it = std::find_if(m_entries.begin(), m_entries.end(),
                                 [&name](const WallpaperEntry &entry) { return entry.name == name; });
    return it == m_entries.end() ? nullptr : &*it;
}

} // namespace galleond
