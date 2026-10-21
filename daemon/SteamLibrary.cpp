// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SteamLibrary.h"
#include <limits>
#ifdef Q_OS_WIN
#include <QSettings>
#endif

#include "UsageStats.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <cstdint>

namespace galleond {
int SteamLibrary::shortcutAppId(const QByteArray &bytes)
{
    if (bytes.isEmpty() || bytes.size() > 8192 || bytes.contains('\0'))
        return 0;
    QString text = QString::fromUtf8(bytes);
    if (text.startsWith(QChar(0xfeff)))
        text.remove(0, 1);
    bool inShortcut = false;
    bool found = false;
    int result = 0;
    static const QRegularExpression game(QStringLiteral("^steam://rungameid/([1-9][0-9]{0,9})$"), QRegularExpression::CaseInsensitiveOption);
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inShortcut = line.compare(QStringLiteral("[InternetShortcut]"), Qt::CaseInsensitive) == 0;
            continue;
        }
        const qsizetype equal = line.indexOf(QLatin1Char('='));
        if (!inShortcut || equal < 0 || line.left(equal).trimmed().compare(QStringLiteral("URL"), Qt::CaseInsensitive) != 0)
            continue;
        if (found)
            return 0;
        found = true;
        const auto match = game.match(line.mid(equal + 1).trimmed());
        bool valid = false;
        const qlonglong id = match.captured(1).toLongLong(&valid);
        if (!match.hasMatch() || !valid || id > std::numeric_limits<int>::max())
            return 0;
        result = static_cast<int>(id);
    }
    return result;
}


namespace {

// A quoted-string tokenizer and a recursive reader for the VDF format:
//   "key" "value"
//   "key" { ... }
class VdfReader
{
public:
    explicit VdfReader(const QByteArray &text)
        : m_text(text)
    {
    }

    std::optional<VdfNode> readRoot()
    {
        VdfNode root;
        if (!readBody(root, 0, true))
            return std::nullopt;
        return root;
    }

private:
    enum class Token : std::uint8_t { String, Open, Close, End, Error };

    Token next(QString &out)
    {
        while (m_pos < m_text.size()) {
            const char c = m_text.at(m_pos);
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++m_pos;
            } else if (c == '/' && m_pos + 1 < m_text.size() && m_text.at(m_pos + 1) == '/') {
                while (m_pos < m_text.size() && m_text.at(m_pos) != '\n')
                    ++m_pos;
            } else {
                break;
            }
        }
        if (m_pos >= m_text.size())
            return Token::End;
        const char c = m_text.at(m_pos);
        if (c == '{') {
            ++m_pos;
            return Token::Open;
        }
        if (c == '}') {
            ++m_pos;
            return Token::Close;
        }
        if (c != '"')
            return Token::Error;
        ++m_pos;
        QByteArray bytes;
        while (m_pos < m_text.size() && m_text.at(m_pos) != '"') {
            char d = m_text.at(m_pos++);
            if (d == '\\' && m_pos < m_text.size()) {
                const char e = m_text.at(m_pos++);
                d = e; // \\ and \" and anything else: the character itself
                if (e == 'n')
                    d = '\n';
                else if (e == 't')
                    d = '\t';
            }
            bytes.append(d);
            if (bytes.size() > 4096)
                return Token::Error; // no key or value is this long
        }
        if (m_pos >= m_text.size())
            return Token::Error; // unterminated
        ++m_pos;
        out = QString::fromUtf8(bytes);
        return Token::String;
    }

    bool readBody(VdfNode &node, int depth, bool top)
    {
        if (depth > SteamLibrary::MaxDepth)
            return false;
        for (;;) {
            QString key;
            const Token first = next(key);
            if (first == Token::End)
                return top; // the end is fine at the top only
            if (first == Token::Close)
                return !top;
            if (first != Token::String)
                return false;
            QString value;
            const Token second = next(value);
            if (second == Token::String) {
                node.values.insert(key.toLower(), value);
            } else if (second == Token::Open) {
                VdfNode child;
                if (!readBody(child, depth + 1, false))
                    return false;
                node.children.insert(key.toLower(), child);
            } else {
                return false;
            }
        }
    }

    const QByteArray &m_text;
    qsizetype m_pos = 0;
};

// A regular file read whole if it is not larger than `limit`; empty otherwise.
QByteArray readSmallFile(const QString &path, qint64 limit)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.size() > limit)
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.read(limit + 1);
}

bool parseInt(const QString &text, qint64 &out, qint64 max = (1LL << 40))
{
    bool ok = false;
    const qint64 v = text.toLongLong(&ok);
    if (!ok || v < 0 || v > max)
        return false;
    out = v;
    return true;
}

} // namespace

const VdfNode *VdfNode::child(const QString &key) const
{
    const auto it = children.constFind(key.toLower());
    return it == children.constEnd() ? nullptr : &*it;
}

QString VdfNode::value(const QString &key) const
{
    return values.value(key.toLower());
}

std::optional<VdfNode> SteamLibrary::parseVdf(const QByteArray &text)
{
    if (text.size() > MaxVdfBytes)
        return std::nullopt;
    return VdfReader(text).readRoot();
}

std::optional<SteamGame> SteamLibrary::parseManifest(const QByteArray &text)
{
    const auto root = parseVdf(text);
    const VdfNode *state = root ? root->child(QStringLiteral("AppState")) : nullptr;
    if (!state)
        return std::nullopt;
    qint64 appid = 0;
    if (!parseInt(state->value(QStringLiteral("appid")), appid, 2'000'000'000) || appid == 0)
        return std::nullopt;
    SteamGame game;
    game.appid = static_cast<int>(appid);
    game.name = state->value(QStringLiteral("name")).left(80);
    if (game.name.isEmpty())
        return std::nullopt;
    qint64 last = 0;
    if (parseInt(state->value(QStringLiteral("LastPlayed")), last))
        game.lastPlayed = last;
    return game;
}

QMap<int, SteamGame> SteamLibrary::parseLocalConfig(const QByteArray &text)
{
    QMap<int, SteamGame> games;
    const auto root = parseVdf(text);
    const VdfNode *node = root ? root->child(QStringLiteral("UserLocalConfigStore")) : nullptr;
    for (const char *key : {"Software", "Valve", "Steam", "apps"}) {
        node = node ? node->child(QLatin1String(key)) : nullptr;
    }
    if (!node)
        return games;
    for (auto it = node->children.constBegin(); it != node->children.constEnd() && games.size() < 5000; ++it) {
        qint64 appid = 0;
        if (!parseInt(it.key(), appid, 2'000'000'000) || appid == 0)
            continue;
        SteamGame game;
        game.appid = static_cast<int>(appid);
        qint64 value = 0;
        if (parseInt(it->value(QStringLiteral("Playtime")), value))
            game.playtimeMinutes = value;
        if (parseInt(it->value(QStringLiteral("LastPlayed")), value))
            game.lastPlayed = value;
        games.insert(game.appid, game);
    }
    return games;
}

QStringList SteamLibrary::parseLibraryFolders(const QByteArray &text)
{
    QStringList folders;
    const auto root = parseVdf(text);
    const VdfNode *node = root ? root->child(QStringLiteral("libraryfolders")) : nullptr;
    if (!node)
        return folders;
    for (auto it = node->children.constBegin(); it != node->children.constEnd() && folders.size() < MaxLibraryFolders; ++it) {
        const QString path = it->value(QStringLiteral("path"));
        if (QDir::isAbsolutePath(path) && !path.contains(QLatin1Char('\0')))
            folders.append(QDir::cleanPath(path));
    }
    return folders;
}

bool SteamLibrary::isTool(const SteamGame &game)
{
    static const QRegularExpression tools(
        QStringLiteral("^(Proton|Steam Linux Runtime|Steamworks Common|Steam Controller Configs)|Redistributable"),
        QRegularExpression::CaseInsensitiveOption);
    return tools.match(game.name).hasMatch() || game.appid == 228980;
}

QString SteamLibrary::defaultRoot()
{
#ifdef Q_OS_WIN
    const QSettings steam(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
    const QString installed = steam.value(QStringLiteral("SteamPath")).toString();
    if (!installed.isEmpty() && QFileInfo(installed + QStringLiteral("/steamapps")).isDir())
        return QFileInfo(installed).canonicalFilePath();
#endif
    for (const QString &candidate : {QDir::homePath() + QStringLiteral("/.steam/steam"),
                                     QDir::homePath() + QStringLiteral("/.local/share/Steam")}) {
        if (QFileInfo(candidate + QStringLiteral("/steamapps")).isDir())
            return QFileInfo(candidate).canonicalFilePath();
    }
    return {};
}

QMap<int, SteamGame> SteamLibrary::playedGames(const QString &root)
{
    QMap<int, SteamGame> played;
    if (root.isEmpty() || !QFileInfo(root).isDir())
        return played;
    const QDir userdata(root + QStringLiteral("/userdata"));
    for (const QString &user : userdata.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        bool numeric = false;
        user.toLongLong(&numeric);
        if (!numeric)
            continue;
        played = parseLocalConfig(readSmallFile(userdata.filePath(user + QStringLiteral("/config/localconfig.vdf")), MaxVdfBytes));
        if (!played.isEmpty())
            break;
    }
    return played;
}

QList<SteamGame> SteamLibrary::installedGames(const QString &root)
{
    QList<SteamGame> games;
    if (root.isEmpty() || !QFileInfo(root).isDir())
        return games;

    // Play time and last launch: the user's own data (first user folder found).
    QMap<int, SteamGame> played;
    const QDir userdata(root + QStringLiteral("/userdata"));
    for (const QString &user : userdata.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        bool numeric = false;
        user.toLongLong(&numeric);
        if (!numeric)
            continue;
        played = parseLocalConfig(readSmallFile(userdata.filePath(user + QStringLiteral("/config/localconfig.vdf")), MaxVdfBytes));
        if (!played.isEmpty())
            break;
    }

    QStringList folders{root};
    folders += parseLibraryFolders(readSmallFile(root + QStringLiteral("/steamapps/libraryfolders.vdf"), MaxManifestBytes));
    QSet<QString> seenFolders;
    QSet<int> seenGames;
    for (const QString &folder : std::as_const(folders)) {
        const QString canonical = QFileInfo(folder).canonicalFilePath();
        if (canonical.isEmpty() || seenFolders.contains(canonical))
            continue;
        seenFolders.insert(canonical);
        const QStringList manifests =
            QDir(canonical + QStringLiteral("/steamapps")).entryList({QStringLiteral("appmanifest_*.acf")}, QDir::Files, QDir::Name);
        for (qsizetype i = 0; i < manifests.size() && i < MaxManifestsPerFolder; ++i) {
            const auto game = parseManifest(readSmallFile(canonical + QStringLiteral("/steamapps/") + manifests.at(i), MaxManifestBytes));
            if (!game || isTool(*game) || seenGames.contains(game->appid))
                continue;
            seenGames.insert(game->appid);
            SteamGame full = *game;
            if (const auto it = played.constFind(full.appid); it != played.constEnd()) {
                full.playtimeMinutes = it->playtimeMinutes;
                full.lastPlayed = std::max(full.lastPlayed, it->lastPlayed);
            }
            games.append(full);
        }
    }
    return games;
}

QString SteamLibrary::coverImage(int appid, const QString &root)
{
    if (root.isEmpty() || appid <= 0)
        return {};
    const QString folder = QStringLiteral("%1/appcache/librarycache/%2/").arg(root).arg(appid);
    for (const char *name : {"library_600x900.jpg", "logo.png"}) {
        const QFileInfo info(folder + QLatin1String(name));
        if (info.isFile() && !info.isSymLink())
            return info.absoluteFilePath();
    }
    // Newer clients can prefix artwork with a content hash inside the app folder.
    QDirIterator artwork(folder, {QStringLiteral("*library_600x900*.jpg"), QStringLiteral("*header*.jpg"), QStringLiteral("*icon*.jpg")}, QDir::Files | QDir::NoSymLinks);
    if (artwork.hasNext())
        return artwork.next();
    // Older Steam caches use flat app-id-prefixed filenames rather than subdirectories.
    for (const char *suffix : {"_library_600x900.jpg", "_header.jpg", "_icon.jpg", "_logo.png"}) {
        const QFileInfo info(QStringLiteral("%1/appcache/librarycache/%2%3").arg(root).arg(appid).arg(QLatin1String(suffix)));
        if (info.isFile() && !info.isSymLink())
            return info.absoluteFilePath();
    }
    return {};
}

double SteamLibrary::score(const SteamGame &game, const QDateTime &now)
{
    return (static_cast<double>(game.playtimeMinutes) / 60.0) * UsageStats::fade(game.lastPlayed, now.toSecsSinceEpoch());
}

} // namespace galleond
