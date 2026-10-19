// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "DesktopEntries.h"
#include "PagesConfig.h"
#include "SteamLibrary.h"
#include "UsageStats.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMap>

namespace galleond {

// What a page can be filled from, read once by the caller (it is I/O) and handed in.
struct ArrangeSources
{
    QList<SteamGame> installedSteam;  // installed Steam games (names)
    QMap<int, SteamGame> playedSteam; // every game with play data, installed or not
    QList<DesktopApp> menu;           // the menu applications of the page's category
    QHash<QString, int> steamOfApp;   // desktop file ID -> the Steam game its Exec starts
};

// A page as the module shows it: when the page asks for it, free keys get the applications of a menu
// category and the installed Steam games, most used first, and the application keys are put in order of
// use. Pure: no I/O, no hardware.
struct ArrangedPage
{
    Page page;
    int highlightKey = -1; // the key to mark as the most used, or -1
};

// The score of an application key: its launches from the module, faded; plus, for a Steam game (or an
// application that starts one), its play time (see SteamLibrary::score). 0 for any other key.
[[nodiscard]] double keyScore(const KeyConfig &key, const UsageStats &usage, const ArrangeSources &sources,
                              const QDateTime &now = QDateTime::currentDateTimeUtc());

[[nodiscard]] ArrangedPage arrangePage(const Page &source, const UsageStats &usage, const ArrangeSources &sources,
                                       const QDateTime &now = QDateTime::currentDateTimeUtc());

} // namespace galleond
