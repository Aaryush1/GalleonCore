// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PageName.h"

namespace bosun {

namespace {

constexpr int MaxPageName = 32;

// "Création Musicale" -> "creation-musicale".
QString slug(const QString &name)
{
    QString out;
    for (const QChar character : name.normalized(QString::NormalizationForm_D)) {
        if (character.category() == QChar::Mark_NonSpacing)
            continue; // the accent of a letter
        const char16_t unit = character.toLower().unicode();
        const bool plain = (unit >= u'a' && unit <= u'z') || (unit >= u'0' && unit <= u'9');
        if (plain)
            out.append(QChar(unit));
        else if (!out.isEmpty() && !out.endsWith(QLatin1Char('-')))
            out.append(QLatin1Char('-'));
    }
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    return out;
}

} // namespace

QString pageNameFor(const QString &name, const QStringList &existing)
{
    QString base = slug(name).left(MaxPageName);
    while (base.endsWith(QLatin1Char('-')))
        base.chop(1);
    if (base.isEmpty())
        base = QStringLiteral("page");
    if (!existing.contains(base))
        return base;
    for (int number = 2; number < 1000; ++number) {
        const QString suffix = QStringLiteral("-%1").arg(number);
        const QString candidate = base.left(MaxPageName - static_cast<int>(suffix.size())) + suffix;
        if (!existing.contains(candidate))
            return candidate;
    }
    return base + QStringLiteral("-x"); // not reached in practice
}

} // namespace bosun
