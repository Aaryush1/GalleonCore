// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The button pages of the Stream Deck module, read from a JSON file
// (~/.config/galleoncore/pages.json). The file is external data: it is
// validated completely, and a file with any error is refused as a whole, so a
// typo can never leave a half-understood page on the keys.

#include <galleon/Protocol.h>

#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>

namespace galleond {

enum class ActionType : std::uint8_t {
    None,
    Media, // a command for the music player (MPRIS)
    Page,  // open another page
    Back,  // return to the previous page
    App,   // start an application, named by its .desktop file
};

enum class MediaCommand : std::uint8_t {
    PlayPause,
    Next,
    Previous,
    Stop,
    Volume, // `step`: percent of the full volume, signed
    Seek,   // `seconds`, signed
};

struct Action
{
    ActionType type = ActionType::None;
    MediaCommand command = MediaCommand::PlayPause;
    int step = 0;    // Volume: -100..100, never 0
    int seconds = 0; // Seek: -3600..3600, never 0
    QString page;    // Page: the name of a page of the file
    QString desktop; // App: a desktop file ID ("firefox.desktop") or the path of a .desktop file

    friend bool operator==(const Action &, const Action &) = default;
};

// A key picture: one icon name, or one per state of the player.
struct IconSpec
{
    QString name;                   // used when no state-specific name applies
    QHash<QString, QString> byState; // "playing", "paused", "stopped"

    [[nodiscard]] QString forState(const QString &state) const
    {
        const auto it = byState.constFind(state);
        return it != byState.constEnd() ? it.value() : name;
    }
    friend bool operator==(const IconSpec &, const IconSpec &) = default;
};

struct KeyConfig
{
    int key = 0; // 0 = top left, row by row, to 11
    QString label;
    IconSpec icon;
    Action action;

    friend bool operator==(const KeyConfig &, const KeyConfig &) = default;
};

struct Page
{
    std::array<std::optional<KeyConfig>, galleon::protocol::KeyCount> keys;

    friend bool operator==(const Page &, const Page &) = default;
};

struct DialConfig
{
    Action turn;  // Media volume or seek: applied once per click, in the direction of the turn
    Action press;

    friend bool operator==(const DialConfig &, const DialConfig &) = default;
};

// When the screen shows the track being played instead of the wallpaper.
enum class NowPlayingMode : std::uint8_t {
    Playing,         // while a player is playing (default)
    PlayingOrPaused, // also while it is paused
    Never,
};

struct ScreenConfig
{
    NowPlayingMode nowPlaying = NowPlayingMode::Playing;
    bool remoteCovers = true; // download https covers; false: local files only
    bool videoThumbnails = true; // a picture of a local video made with FFmpeg when there is no cover

    friend bool operator==(const ScreenConfig &, const ScreenConfig &) = default;
};

struct PagesConfig
{
    ScreenConfig screen;
    QString home;
    QMap<QString, Page> pages;
    std::array<DialConfig, galleon::protocol::DialCount> dials;

    friend bool operator==(const PagesConfig &, const PagesConfig &) = default;
};

struct PagesParseResult
{
    std::optional<PagesConfig> config; // empty when `errors` is not
    QStringList errors;                // one line each, with the path of the field
};

inline constexpr qsizetype MaxPagesFileBytes = 256 * 1024;
inline constexpr int MaxPages = 32;
inline constexpr int MaxLabelLength = 24;
inline constexpr int MaxNameLength = 64;
inline constexpr int MaxPageDepth = 8; // pages opened one from another, at run time

// Parses and validates the content of a pages file. Never throws, never reads
// outside `json`; any problem is an entry of `errors` and no config is returned.
[[nodiscard]] PagesParseResult parsePagesConfig(const QByteArray &json);

// The configuration used when the user has no (valid) file.
[[nodiscard]] QByteArray defaultPagesJson();

} // namespace galleond
