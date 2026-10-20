// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FileNames.h"

#include <QDir>
#include <QFileInfo>

namespace bosun::filenames {

QString safeName(const QString &wished, const QString &fallback)
{
    QString name;
    for (const QChar character : wished.trimmed()) {
        const bool plain = character.isLetterOrNumber() || character == QLatin1Char(' ') || character == QLatin1Char('-')
                           || character == QLatin1Char('_') || character == QLatin1Char('.');
        name.append(plain ? character : QLatin1Char('-'));
    }
    while (name.startsWith(QLatin1Char('.')) || name.startsWith(QLatin1Char(' ')))
        name.remove(0, 1);
    name = name.left(MaxNameLength).trimmed();
    return name.isEmpty() ? fallback : name;
}

QString freePath(const QString &folder, const QString &base, const QString &suffix)
{
    const QDir dir(folder);
    for (int number = 1; number < 10000; ++number) {
        const QString file = number == 1 ? base + suffix : QStringLiteral("%1-%2%3").arg(base).arg(number).arg(suffix);
        if (!QFileInfo::exists(dir.filePath(file)))
            return dir.filePath(file);
    }
    return dir.filePath(base + QStringLiteral("-x") + suffix); // not reached in practice
}

} // namespace bosun::filenames
