// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>

#include <optional>

namespace galleond {

// A node of a Valve text file (VDF): values and sub-sections by key. Keys are looked up
// without regard to case, as Steam itself writes them with varying case.
struct VdfNode
{
    QMap<QString, QString> values;  // keys in lower case
    QMap<QString, VdfNode> children; // keys in lower case

    [[nodiscard]] const VdfNode *child(const QString &key) const;
    [[nodiscard]] QString value(const QString &key) const;
};

struct SteamGame
{
    int appid = 0;
    QString name;
    qint64 playtimeMinutes = 0; // from the user's local Steam data, all time
    qint64 lastPlayed = 0;      // seconds since the epoch, 0 = never
};

// What the local Steam installation says about the games installed on this machine: no network,
// no account, no key. Steam keeps, for each game the user has, its play time and the date of the last
// launch in userdata/<id>/config/localconfig.vdf, and each installed game has an appmanifest_<id>.acf
// with its name, in the library folders listed in steamapps/libraryfolders.vdf.
//
// All of these files are read as untrusted: regular files, bounded in size and depth, and every value
// is checked. A game named in a file that cannot be read is simply left out.
class SteamLibrary
{
public:
    static constexpr qint64 MaxVdfBytes = 8LL * 1024 * 1024;
    static constexpr int MaxDepth = 16;
    static constexpr int MaxLibraryFolders = 16;
    static constexpr int MaxManifestsPerFolder = 500;
    static constexpr qint64 MaxManifestBytes = qint64{256} * 1024;

    // The text file as a tree; nothing if it is malformed, too deep or too large.
    [[nodiscard]] static std::optional<VdfNode> parseVdf(const QByteArray &text);

    // Game name and identifier of one appmanifest_*.acf.
    [[nodiscard]] static std::optional<SteamGame> parseManifest(const QByteArray &text);

    // appid -> {play time, last launch} from localconfig.vdf.
    [[nodiscard]] static QMap<int, SteamGame> parseLocalConfig(const QByteArray &text);

    // The library folders of libraryfolders.vdf (absolute paths).
    [[nodiscard]] static QStringList parseLibraryFolders(const QByteArray &text);

    // True for what Steam installs as tools (Proton, runtimes, redistributables): not games.
    [[nodiscard]] static bool isTool(const SteamGame &game);

    // The installed games of the Steam folder `root` (default: the user's), with their play data.
    [[nodiscard]] static QList<SteamGame> installedGames(const QString &root = defaultRoot());
    // Every game the user has played, installed or not, with its play time and last launch (no name).
    [[nodiscard]] static QMap<int, SteamGame> playedGames(const QString &root = defaultRoot());
    [[nodiscard]] static QString defaultRoot();

    // The cover picture of a game that Steam keeps in its cache (a regular file, not a link), or empty.
    [[nodiscard]] static QString coverImage(int appid, const QString &root = defaultRoot());

    // Play time in hours, faded by the time since the last launch (see UsageStats::fade).
    [[nodiscard]] static double score(const SteamGame &game, const QDateTime &now = QDateTime::currentDateTimeUtc());
};

} // namespace galleond
