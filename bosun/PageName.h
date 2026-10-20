// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

namespace bosun {

// The name of the page a new tab opens, made from the name the user typed: lower case letters,
// digits and "-" (accents dropped, anything else a "-"), at most 32 characters, and not one of
// `existing` ("music", "music-2", "music-3"...). "page" when nothing usable is left. Pure.
[[nodiscard]] QString pageNameFor(const QString &name, const QStringList &existing);

} // namespace bosun
