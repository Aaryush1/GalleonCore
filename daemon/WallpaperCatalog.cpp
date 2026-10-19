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

// Natural order: letters without regard to case, runs of digits as numbers (2 before 10).
// Written out, so that it is the same on every machine whatever the locale or the libraries.
bool naturalLess(const QString &a, const QString &b)
{
    qsizetype i = 0;
    qsizetype j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            qsizetype ei = i;
            qsizetype ej = j;
            while (ei < a.size() && a[ei].isDigit())
                ++ei;
            while (ej < b.size() && b[ej].isDigit())
                ++ej;
            // Leading zeros do not count; a longer number is a bigger one; same length: digit by digit.
            QStringView na = QStringView(a).mid(i, ei - i);
            QStringView nb = QStringView(b).mid(j, ej - j);
            while (na.size() > 1 && na.front() == QLatin1Char('0'))
                na = na.mid(1);
            while (nb.size() > 1 && nb.front() == QLatin1Char('0'))
                nb = nb.mid(1);
            if (na.size() != nb.size())
                return na.size() < nb.size();
            if (const int order = na.compare(nb); order != 0)
                return order < 0;
            i = ei;
            j = ej;
            continue;
        }
        const QChar ca = a[i].toLower();
        const QChar cb = b[j].toLower();
        if (ca != cb)
            return ca < cb;
        ++i;
        ++j;
    }
    // One is the start of the other: the shorter first; then the exact text, so that the order is total.
    if (a.size() - i != b.size() - j)
        return (a.size() - i) < (b.size() - j);
    return a < b;
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

    // A natural order, the same on every machine and in every language: styles by
    // name, then the files of a style by name with numbers read as numbers
    // (wallpaper-2 before wallpaper-10), not by the file system's order.
    const auto sorted = [](QFileInfoList list) {
        std::stable_sort(list.begin(), list.end(),
                         [](const QFileInfo &x, const QFileInfo &y) { return naturalLess(x.fileName(), y.fileName()); });
        return list;
    };

    const QFileInfoList styles = sorted(rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name));
    for (const QFileInfo &styleInfo : styles) {
        if (styleInfo.isSymLink())
            continue;
        const QDir styleDir(styleInfo.absoluteFilePath());
        const QFileInfoList files = sorted(styleDir.entryInfoList(QDir::Files, QDir::Name));
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
