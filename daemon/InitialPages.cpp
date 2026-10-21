// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "InitialPages.h"
#include "PagesConfig.h"
#include "DesktopEntries.h"
#include "SteamLibrary.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace galleond {
QStringList windowsAppCategories(const QString &name, const QString &folder)
{
    const QString lower = name.toLower();
    for (const QString &helper : {QStringLiteral("software center"), QStringLiteral("removable drives"), QStringLiteral("icon mode"), QStringLiteral("video capture")}) {
        if (lower.contains(helper))
            return {QStringLiteral("Utility")};
    }
    const QString path = QString(folder).replace(QLatin1Char('\\'), QLatin1Char('/')).toLower();
    const QStringList mediaNames{QStringLiteral("firefox"), QStringLiteral("chrome"), QStringLiteral("edge"),
        QStringLiteral("brave"), QStringLiteral("opera"), QStringLiteral("vivaldi"), QStringLiteral("chromium"),
        QStringLiteral("vlc"), QStringLiteral("spotify"), QStringLiteral("foobar"), QStringLiteral("music"),
        QStringLiteral("musique"), QStringLiteral("video"), QStringLiteral("vidéo"), QStringLiteral("media"),
        QStringLiteral("média"), QStringLiteral("mpc-"), QStringLiteral("mpv"), QStringLiteral("winamp"),
        QStringLiteral("clementine"), QStringLiteral("strawberry"), QStringLiteral("itunes"), QStringLiteral("plex"),
        QStringLiteral("kodi"), QStringLiteral("deezer"), QStringLiteral("tidal")};
    for (const QString &word : mediaNames) {
        if (lower.contains(word))
            return {QStringLiteral("AudioVideo")};
    }
    if (path.contains(QStringLiteral("/games")) || path.contains(QStringLiteral("/jeux"))
        || path.contains(QStringLiteral("/epic games")) || lower == QStringLiteral("steam")
        || lower.contains(QStringLiteral("epic games")) || lower.contains(QStringLiteral("gog galaxy")))
        return {QStringLiteral("Game")};
    return {QStringLiteral("Utility")};
}

QByteArray windowsInitialPagesJson(const QString &systemFolder, const QString &screensaverFolder)
{
    QJsonObject root = QJsonDocument::fromJson(defaultPagesJson()).object();
    QJsonObject pages = root.value(QStringLiteral("pages")).toObject();
    QJsonObject control = pages.value(QStringLiteral("main")).toObject();
    QJsonArray controlKeys = control.value(QStringLiteral("keys")).toArray();
    for (qsizetype index = 0; index < controlKeys.size(); ++index) {
        QJsonObject key = controlKeys.at(index).toObject();
        const QString label = key.value(QStringLiteral("label")).toString();
        if (label == QStringLiteral("Previous")) key.insert(QStringLiteral("label"), QStringLiteral("Précédent"));
        else if (label == QStringLiteral("Play")) key.insert(QStringLiteral("label"), QStringLiteral("Lecture"));
        else if (label == QStringLiteral("Next")) key.insert(QStringLiteral("label"), QStringLiteral("Suivant"));
        controlKeys.replace(index, key);
    }
    const auto addControl = [&](int key, const QString &label, const QString &icon, const QString &command) {
        controlKeys.append(QJsonObject{{QStringLiteral("key"), key}, {QStringLiteral("label"), label},
            {QStringLiteral("icon"), icon}, {QStringLiteral("action"), QJsonObject{{QStringLiteral("type"), QStringLiteral("media")},
                {QStringLiteral("command"), command}}}});
    };
    addControl(4, QStringLiteral("Muet"), QStringLiteral("audio-volume-muted"), QStringLiteral("mute"));
    addControl(7, QStringLiteral("Début"), QStringLiteral("media-seek-backward"), QStringLiteral("restart"));
    addControl(9, QStringLiteral("Aléatoire"), QStringLiteral("media-playlist-shuffle"), QStringLiteral("shuffle"));
    addControl(10, QStringLiteral("Répéter"), QStringLiteral("media-playlist-repeat"), QStringLiteral("repeat"));
    control.insert(QStringLiteral("keys"), controlKeys);
    pages.insert(QStringLiteral("main"), control);
    QJsonArray themes;
    const auto addTheme = [&](const QString &page, const QString &name, const QString &color, const QString &when = {}) {
        QJsonObject theme{{QStringLiteral("page"), page}, {QStringLiteral("name"), name}, {QStringLiteral("color"), color}};
        if (!when.isEmpty())
            theme.insert(QStringLiteral("when"), when);
        themes.append(theme);
    };
    addTheme(QStringLiteral("main"), QStringLiteral("Contrôle"), QStringLiteral("#ffd600"), QStringLiteral("media"));
    addTheme(QStringLiteral("multimedia"), QStringLiteral("Multimédia"), QStringLiteral("#00bcd4"));
    addTheme(QStringLiteral("jeux"), QStringLiteral("Jeu Vidéo"), QStringLiteral("#1a237e"));
    addTheme(QStringLiteral("systeme"), QStringLiteral("Système"), QStringLiteral("#43a047"));
    addTheme(QStringLiteral("numpad"), QStringLiteral("Pavé Numérique"), QStringLiteral("#ffffff"), QStringLiteral("no-media"));
    pages.insert(QStringLiteral("multimedia"), QJsonObject{{QStringLiteral("keys"), QJsonArray{}},
        {QStringLiteral("menu"), QJsonObject{{QStringLiteral("category"), QStringLiteral("AudioVideo")}, {QStringLiteral("max"), 12}}},
        {QStringLiteral("sort"), QStringLiteral("usage")}});
    pages.insert(QStringLiteral("jeux"), QJsonObject{{QStringLiteral("keys"), QJsonArray{}},
        {QStringLiteral("menu"), QJsonObject{{QStringLiteral("category"), QStringLiteral("Game")}, {QStringLiteral("max"), 12}}},
        {QStringLiteral("steam"), QJsonObject{{QStringLiteral("max"), 12}}}, {QStringLiteral("sort"), QStringLiteral("usage")}});
    QJsonArray systemKeys;
    const QStringList executables{QStringLiteral("Taskmgr.exe"), QStringLiteral("control.exe"), QStringLiteral("msinfo32.exe"),
        QStringLiteral("msconfig.exe"), QStringLiteral("resmon.exe"), QStringLiteral("cleanmgr.exe")};
    const QStringList labels{QStringLiteral("Gestionnaire des tâches"), QStringLiteral("Panneau de configuration"),
        QStringLiteral("Informations système"), QStringLiteral("Configuration système"), QStringLiteral("Moniteur de ressources"),
        QStringLiteral("Nettoyage de disque")};
    for (qsizetype index = 0; index < executables.size(); ++index) {
        const QString file = QDir(systemFolder).absoluteFilePath(executables.at(index));
        if (systemFolder.isEmpty() || !QFileInfo(file).isFile())
            continue;
        systemKeys.append(QJsonObject{{QStringLiteral("key"), static_cast<int>(index)}, {QStringLiteral("label"), labels.at(index)},
            {QStringLiteral("icon"), QStringLiteral("preferences-system")},
            {QStringLiteral("action"), QJsonObject{{QStringLiteral("type"), QStringLiteral("app")}, {QStringLiteral("desktop"), file}}}});
    }
    pages.insert(QStringLiteral("systeme"), QJsonObject{{QStringLiteral("keys"), systemKeys}});
    QJsonArray numberKeys;
    const QStringList names{QStringLiteral("kp7"), QStringLiteral("kp8"), QStringLiteral("kp9"), QStringLiteral("kp4"),
        QStringLiteral("kp5"), QStringLiteral("kp6"), QStringLiteral("kp1"), QStringLiteral("kp2"), QStringLiteral("kp3"),
        QStringLiteral("kp0"), QStringLiteral("kpdot"), QStringLiteral("kpenter")};
    const QStringList numbers{QStringLiteral("7"), QStringLiteral("8"), QStringLiteral("9"), QStringLiteral("4"),
        QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
        QStringLiteral("0"), QStringLiteral("."), QStringLiteral("Entrée")};
    for (qsizetype index = 0; index < names.size(); ++index)
        numberKeys.append(QJsonObject{{QStringLiteral("key"), static_cast<int>(index)}, {QStringLiteral("label"), numbers.at(index)},
            {QStringLiteral("action"), QJsonObject{{QStringLiteral("type"), QStringLiteral("key")}, {QStringLiteral("key"), names.at(index)}}}});
    pages.insert(QStringLiteral("numpad"), QJsonObject{{QStringLiteral("keys"), numberKeys}});
    root.insert(QStringLiteral("pages"), pages);
    root.insert(QStringLiteral("themes"), themes);
    root.insert(QStringLiteral("dials"), QJsonObject{
        {QStringLiteral("0"), QJsonObject{{QStringLiteral("turn"), QJsonObject{{QStringLiteral("type"), QStringLiteral("media")},
            {QStringLiteral("command"), QStringLiteral("volume")}, {QStringLiteral("step"), 2}}},
            {QStringLiteral("press"), QJsonObject{{QStringLiteral("type"), QStringLiteral("theme")}}}}},
        {QStringLiteral("1"), QJsonObject{{QStringLiteral("turn"), QJsonObject{{QStringLiteral("type"), QStringLiteral("media")},
            {QStringLiteral("command"), QStringLiteral("seek")}, {QStringLiteral("seconds"), 5}}},
            {QStringLiteral("press"), QJsonObject{{QStringLiteral("type"), QStringLiteral("wallpaper-mode")}}}}}});
    if (!screensaverFolder.isEmpty()) {
        QJsonArray videos;
        for (const QString &title : {QStringLiteral("Hackerne cyberpunk sous la pluie"), QStringLiteral("Refuge néon sous la lune rose")})
            videos.append(QJsonObject{{QStringLiteral("file"), QDir(screensaverFolder).absoluteFilePath(title + QStringLiteral(".mp4"))},
                {QStringLiteral("title"), title}});
        // Dimensions measured on the owner's module; see docs/ROADMAP.md and screensaver geometry notes.
        root.insert(QStringLiteral("screensaver"), QJsonObject{{QStringLiteral("video"), videos}, {QStringLiteral("idle_minutes"), 5},
            {QStringLiteral("fps"), 15}, {QStringLiteral("while_playing"), false},
            {QStringLiteral("geometry"), QJsonObject{{QStringLiteral("screen_mm"), QJsonArray{62.6, 33.0}},
                {QStringLiteral("key_mm"), QJsonArray{13.7, 13.7}}, {QStringLiteral("gap_mm"), QJsonArray{5.4, 5.4}},
                {QStringLiteral("screen_to_keys_mm"), 5.4}}}});
    }
    return QJsonDocument(root).toJson();
}

QJsonObject fixedWindowsPages(QJsonObject root)
{
    QJsonObject pages = root.value(QStringLiteral("pages")).toObject();
    const QJsonArray themes = root.value(QStringLiteral("themes")).toArray();
    for (const auto &themeValue : themes) {
        const auto theme = themeValue.toObject();
        const QString name = theme.value(QStringLiteral("page")).toString();
        QJsonObject page = pages.value(name).toObject();
        // Migrate the recognizable auto-generated Plus link without deleting the extra page's data.
        if (page.contains(QStringLiteral("menu"))) {
            QJsonArray keys = page.value(QStringLiteral("keys")).toArray();
            const QString moreName = name + QStringLiteral("-plus");
            const QString label = (theme.value(QStringLiteral("name")).toString() + QStringLiteral("-Plus")).left(MaxLabelLength);
            for (qsizetype index = 0; index < keys.size(); ++index) {
                const auto key = keys.at(index).toObject();
                const auto action = key.value(QStringLiteral("action")).toObject();
                if (key.value(QStringLiteral("key")).toInt(-1) != 11 || key.value(QStringLiteral("label")).toString() != label
                    || action.value(QStringLiteral("type")) != QStringLiteral("page")
                    || action.value(QStringLiteral("page")) != moreName)
                    continue;
                keys.removeAt(index);
                const auto extras = pages.value(moreName).toObject().value(QStringLiteral("keys")).toArray();
                for (const auto &extra : extras) {
                    QJsonObject replacement = extra.toObject();
                    const auto type = replacement.value(QStringLiteral("action")).toObject().value(QStringLiteral("type")).toString();
                    if (replacement.value(QStringLiteral("key")).toInt(-1) == 0 && (type == QStringLiteral("app") || type == QStringLiteral("steam"))) {
                        replacement.insert(QStringLiteral("key"), 11);
                        keys.append(replacement);
                        break;
                    }
                }
                break;
            }
            page.insert(QStringLiteral("keys"), keys);
            pages.insert(name, page);
        }
    }
    for (auto it = pages.begin(); it != pages.end(); ++it) {
        QJsonObject page = it.value().toObject();
        // Existing files keep their explicit positions and empty keys. Never rediscover on reload/edit.
        page.remove(QStringLiteral("menu"));
        page.remove(QStringLiteral("steam"));
        page.remove(QStringLiteral("sort"));
        it.value() = page;
    }
    root.insert(QStringLiteral("pages"), pages);
    return root;
}

QJsonObject seedWindowsLaunchers(QJsonObject root, const QList<DesktopApp> &apps, const QList<SteamGame> &games)
{
    QJsonObject pages = root.value(QStringLiteral("pages")).toObject();
    for (auto it = pages.begin(); it != pages.end(); ++it) {
        QJsonObject page = it.value().toObject();
        const QString category = page.value(QStringLiteral("menu")).toObject().value(QStringLiteral("category")).toString();
        if (category.isEmpty())
            continue;
        QJsonArray keys = page.value(QStringLiteral("keys")).toArray();
        QSet<int> seenGames;
        const auto append = [&](const QString &label, const QJsonObject &action) {
            if (keys.size() < galleon::protocol::KeyCount)
                keys.append(QJsonObject{{QStringLiteral("key"), static_cast<int>(keys.size())},
                    {QStringLiteral("label"), label.left(MaxLabelLength)}, {QStringLiteral("action"), action}});
        };
        if (page.contains(QStringLiteral("steam"))) {
            for (const SteamGame &game : games) {
                if (SteamLibrary::isTool(game) || seenGames.contains(game.appid))
                    continue;
                seenGames.insert(game.appid);
                append(game.name, QJsonObject{{QStringLiteral("type"), QStringLiteral("steam")}, {QStringLiteral("appid"), game.appid}});
            }
        }
        for (const DesktopApp &app : apps) {
            const bool browser = category == QStringLiteral("AudioVideo") && !app.categories.contains(QStringLiteral("Game"))
                && (app.categories.contains(QStringLiteral("WebBrowser"))
                    || windowsAppCategories(app.name, QString()).contains(QStringLiteral("AudioVideo")));
            if ((!app.categories.contains(category) && !browser)
                || (app.steamAppId > 0 && seenGames.contains(app.steamAppId)))
                continue;
            if (app.steamAppId > 0)
                seenGames.insert(app.steamAppId);
            append(app.name, QJsonObject{{QStringLiteral("type"), QStringLiteral("app")}, {QStringLiteral("desktop"), app.file}});
        }
        page.insert(QStringLiteral("keys"), keys);
        it.value() = page;
    }
    root.insert(QStringLiteral("pages"), pages);
    return fixedWindowsPages(root);
}

QByteArray linuxInitialPagesJson(const QList<DesktopApp> &apps, const QList<SteamGame> &games)
{
    QJsonObject root = QJsonDocument::fromJson(windowsInitialPagesJson({}, {})).object();
    QJsonArray themes = root.value(QStringLiteral("themes")).toArray();
    QJsonObject numberTheme = themes.at(4).toObject();
    numberTheme.remove(QStringLiteral("when")); // Start on Contrôle, not the numpad when no player is active.
    themes.replace(4, numberTheme);
    root.insert(QStringLiteral("themes"), themes);
    QJsonObject pages = root.value(QStringLiteral("pages")).toObject();
    QJsonObject system = pages.value(QStringLiteral("systeme")).toObject();
    system.insert(QStringLiteral("menu"), QJsonObject{{QStringLiteral("category"), QStringLiteral("Utility")},
        {QStringLiteral("max"), galleon::protocol::KeyCount}});
    pages.insert(QStringLiteral("systeme"), system);
    root.insert(QStringLiteral("pages"), pages);
    return QJsonDocument(seedWindowsLaunchers(root, apps, games)).toJson();
}

QByteArray preparePagesJson(const QByteArray &bytes)
{
#ifdef Q_OS_WIN
    if (!parsePagesConfig(bytes).config)
        return bytes;
    return QJsonDocument(fixedWindowsPages(QJsonDocument::fromJson(bytes).object())).toJson();
#else
    return bytes;
#endif
}

QByteArray initialPagesJson()
{
#ifdef Q_OS_WIN
    // Warmed before opening HID. Used only as fallback, never to refill an existing file.
    static const QByteArray initial = QJsonDocument(seedWindowsLaunchers(
        QJsonDocument::fromJson(windowsInitialPagesJson(
            QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("System32")),
            QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../share/galleoncore/screensavers")))).object(),
        DesktopEntries::list(false), SteamLibrary::installedGames())).toJson();
    return initial;
#else
    // Discover once before opening HID, then keep every slot fixed after the first edit.
    static const QByteArray initial = linuxInitialPagesJson(DesktopEntries::list(), SteamLibrary::installedGames());
    return initial;
#endif
}
}
