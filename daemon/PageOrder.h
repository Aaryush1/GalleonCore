// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "PagesConfig.h"
#include "SteamLibrary.h"
#include "UsageStats.h"

#include <QDateTime>
#include <QList>

namespace galleond {

// A page as the module shows it: its application keys in order of use and the installed Steam games
// on its free keys, when the page asks for it (`sort`, `steam`). Pure: no I/O, no hardware.
struct ArrangedPage
{
    Page page;
    int highlightKey = -1; // the key to mark as the most used, or -1
};

// The score of an application key: its launches from the module, faded; for a Steam game also its play
// time (see SteamLibrary::score). 0 for any other key.
[[nodiscard]] double keyScore(const KeyConfig &key, const UsageStats &usage, const QList<SteamGame> &steam,
                              const QDateTime &now = QDateTime::currentDateTimeUtc());

[[nodiscard]] ArrangedPage arrangePage(const Page &page, const UsageStats &usage, const QList<SteamGame> &steam,
                                       const QDateTime &now = QDateTime::currentDateTimeUtc());

} // namespace galleond
