// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PageOrder.h"

#include <QSet>

#include <algorithm>
#include <utility>
#include <vector>

namespace galleond {

double keyScore(const KeyConfig &key, const UsageStats &usage, const QList<SteamGame> &steam, const QDateTime &now)
{
    if (key.action.type == ActionType::App)
        return usage.score(key.action.desktop, now);
    if (key.action.type != ActionType::Steam)
        return 0.0;
    double score = usage.score(QStringLiteral("steam:%1").arg(key.action.appid), now);
    for (const SteamGame &game : steam) {
        if (game.appid == key.action.appid) {
            score += SteamLibrary::score(game, now);
            break;
        }
    }
    return score;
}

ArrangedPage arrangePage(const Page &source, const UsageStats &usage, const QList<SteamGame> &steam, const QDateTime &now)
{
    ArrangedPage result;
    result.page = source;
    Page &page = result.page;

    // Free keys get the most played games that are not on the page already.
    if (page.steamMax > 0) {
        QSet<int> present;
        for (const auto &key : page.keys) {
            if (key && key->action.type == ActionType::Steam)
                present.insert(key->action.appid);
        }
        std::vector<std::pair<double, SteamGame>> candidates;
        for (const SteamGame &game : steam) {
            if (present.contains(game.appid))
                continue;
            KeyConfig probe;
            probe.action.type = ActionType::Steam;
            probe.action.appid = game.appid;
            candidates.emplace_back(keyScore(probe, usage, steam, now), game);
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
            return a.first != b.first ? a.first > b.first : a.second.name.compare(b.second.name, Qt::CaseInsensitive) < 0;
        });
        std::size_t added = 0;
        for (std::size_t place = 0; place < page.keys.size() && added < static_cast<std::size_t>(page.steamMax) && added < candidates.size();
             ++place) {
            if (page.keys[place])
                continue;
            const SteamGame &game = candidates[added++].second;
            KeyConfig key;
            key.key = static_cast<int>(place);
            key.label = game.name.left(MaxLabelLength);
            key.action.type = ActionType::Steam;
            key.action.appid = game.appid;
            page.keys[place] = key;
        }
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
            return keyScore(a, usage, steam, now) > keyScore(b, usage, steam, now);
        });
        for (std::size_t i = 0; i < places.size(); ++i) {
            keys[i].key = static_cast<int>(places[i]);
            page.keys[places[i]] = keys[i];
        }
        if (!keys.empty() && keyScore(keys.front(), usage, steam, now) > 0.0)
            result.highlightKey = static_cast<int>(places.front());
    }
    return result;
}

} // namespace galleond
