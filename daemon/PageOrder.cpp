// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PageOrder.h"

#include <QSet>

#include <algorithm>
#include <utility>
#include <vector>

namespace galleond {

namespace {

double playScore(const ArrangeSources &sources, int appid, const QDateTime &now)
{
    const auto it = sources.playedSteam.constFind(appid);
    return it == sources.playedSteam.constEnd() ? 0.0 : SteamLibrary::score(*it, now);
}

// The Steam game a key starts, if it starts one (0 otherwise).
int steamGameOf(const KeyConfig &key, const ArrangeSources &sources)
{
    if (key.action.type == ActionType::Steam)
        return key.action.appid;
    if (key.action.type == ActionType::App)
        return sources.steamOfApp.value(key.action.desktop, 0);
    return 0;
}

} // namespace

double keyScore(const KeyConfig &key, const UsageStats &usage, const ArrangeSources &sources, const QDateTime &now)
{
    if (key.action.type == ActionType::App) {
        double score = usage.score(key.action.desktop, now);
        if (const int appid = steamGameOf(key, sources); appid > 0)
            score += playScore(sources, appid, now);
        return score;
    }
    if (key.action.type != ActionType::Steam)
        return 0.0;
    return usage.score(QStringLiteral("steam:%1").arg(key.action.appid), now) + playScore(sources, key.action.appid, now);
}

ArrangedPage arrangePage(const Page &source, const UsageStats &usage, const ArrangeSources &sources, const QDateTime &now)
{
    ArrangedPage result;
    result.page = source;
    Page &page = result.page;

    // The games and applications already on the page, so that nothing is shown twice (a menu shortcut of a Steam game
    // and the same game from Steam's own list are one game).
    QSet<QString> presentApps;
    QSet<int> presentGames;
    const auto notePresent = [&](const KeyConfig &key) {
        if (key.action.type == ActionType::App)
            presentApps.insert(key.action.desktop);
        if (const int appid = steamGameOf(key, sources); appid > 0)
            presentGames.insert(appid);
    };
    for (const auto &key : page.keys) {
        if (key)
            notePresent(*key);
    }

    const auto fill = [&page](std::vector<KeyConfig> &candidates, int max) {
        std::size_t added = 0;
        for (std::size_t place = 0; place < page.keys.size() && added < static_cast<std::size_t>(max) && added < candidates.size(); ++place) {
            if (page.keys[place])
                continue;
            candidates[added].key = static_cast<int>(place);
            page.keys[place] = candidates[added++];
        }
    };

    // Free keys first get applications of the menu category, most used first.
    if (page.menuMax > 0) {
        std::vector<KeyConfig> candidates;
        for (const DesktopApp &app : sources.menu) {
            SteamGame asGame;
            asGame.name = app.name;
            asGame.appid = app.steamAppId;
            if (presentApps.contains(app.id) || (app.steamAppId > 0 && presentGames.contains(app.steamAppId)) || SteamLibrary::isTool(asGame))
                continue;
            KeyConfig key;
            key.action.type = ActionType::App;
            key.action.desktop = app.id;
            candidates.push_back(key);
        }
        std::stable_sort(candidates.begin(), candidates.end(), [&](const KeyConfig &a, const KeyConfig &b) {
            const double sa = keyScore(a, usage, sources, now);
            const double sb = keyScore(b, usage, sources, now);
            return sa != sb ? sa > sb : a.action.desktop.compare(b.action.desktop, Qt::CaseInsensitive) < 0;
        });
        const std::size_t before = candidates.size();
        fill(candidates, page.menuMax);
        for (const auto &key : page.keys) { // note what was just added
            if (key)
                notePresent(*key);
        }
        (void)before;
    }

    // Then the installed Steam games that are not on the page yet.
    if (page.steamMax > 0) {
        std::vector<std::pair<double, SteamGame>> scored;
        for (const SteamGame &game : sources.installedSteam) {
            if (presentGames.contains(game.appid))
                continue;
            KeyConfig probe;
            probe.action.type = ActionType::Steam;
            probe.action.appid = game.appid;
            scored.emplace_back(keyScore(probe, usage, sources, now), game);
        }
        std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) {
            return a.first != b.first ? a.first > b.first : a.second.name.compare(b.second.name, Qt::CaseInsensitive) < 0;
        });
        std::vector<KeyConfig> candidates;
        for (const auto &entry : scored) {
            KeyConfig key;
            key.label = entry.second.name.left(MaxLabelLength);
            key.action.type = ActionType::Steam;
            key.action.appid = entry.second.appid;
            candidates.push_back(key);
        }
        fill(candidates, page.steamMax);
    }

    // The application keys in order of use: the same places, the most used in the first one.
    if (page.sort == PageSort::Usage) {
        std::vector<std::size_t> places;
        std::vector<KeyConfig> keys;
        for (std::size_t place = 0; place < page.keys.size(); ++place) {
            const auto &key = page.keys[place];
            if (key && (key->action.type == ActionType::App || key->action.type == ActionType::Steam)) {
                places.push_back(place);
                keys.push_back(*key);
            }
        }
        std::stable_sort(keys.begin(), keys.end(), [&](const KeyConfig &a, const KeyConfig &b) {
            return keyScore(a, usage, sources, now) > keyScore(b, usage, sources, now);
        });
        for (std::size_t i = 0; i < places.size(); ++i) {
            keys[i].key = static_cast<int>(places[i]);
            page.keys[places[i]] = keys[i];
        }
        if (!keys.empty() && keyScore(keys.front(), usage, sources, now) > 0.0)
            result.highlightKey = static_cast<int>(places.front());
    }
    return result;
}

} // namespace galleond
