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
#include <QElapsedTimer>
#include <algorithm>
#include <utility>

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
        if (!QFileInfo(file).isFile())
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
    return QJsonDocument(root).toJson();
}

QJsonObject paginateWindowsLaunchers(QJsonObject root, const QList<DesktopApp> &apps, const QList<SteamGame> &games)
{
    QJsonObject pages = root.value(QStringLiteral("pages")).toObject();
    const QJsonArray themes = root.value(QStringLiteral("themes")).toArray();
    for (const auto &themeValue : themes) {
        const QJsonObject theme = themeValue.toObject();
        const QString name = theme.value(QStringLiteral("page")).toString();
        const QString moreName = name + QStringLiteral("-plus");
        QJsonObject page = pages.value(name).toObject();
        const QString category = page.value(QStringLiteral("menu")).toObject().value(QStringLiteral("category")).toString();
        if (category.isEmpty() || pages.contains(moreName) || moreName.size() > 32 || pages.size() >= MaxPages)
            continue;
        QJsonArray launchers = page.value(QStringLiteral("keys")).toArray();
        bool onlyLaunchers = true;
        QSet<QString> seen;
        const auto identity = [](const QJsonObject &key) {
            const QJsonObject action = key.value(QStringLiteral("action")).toObject();
            return action.value(QStringLiteral("type")).toString() == QStringLiteral("steam")
                ? QStringLiteral("steam:%1").arg(action.value(QStringLiteral("appid")).toInt())
                : action.value(QStringLiteral("desktop")).toString().toCaseFolded();
        };
        for (const auto &key : std::as_const(launchers)) {
            const QString type = key.toObject().value(QStringLiteral("action")).toObject().value(QStringLiteral("type")).toString();
            onlyLaunchers = onlyLaunchers && (type == QStringLiteral("app") || type == QStringLiteral("steam"));
            seen.insert(identity(key.toObject()));
        }
        if (!onlyLaunchers)
            continue; // Custom navigation and control keys belong to the user.
        QMap<int, QJsonObject> placed;
        for (const auto &key : std::as_const(launchers))
            placed.insert(key.toObject().value(QStringLiteral("key")).toInt(), key.toObject());
        launchers = {};
        for (const auto &key : std::as_const(placed))
            launchers.append(key);
        const auto append = [&](const QString &label, const QJsonObject &action) {
            const QJsonObject key{{QStringLiteral("label"), label.left(MaxLabelLength)}, {QStringLiteral("action"), action}};
            const QString id = identity(key);
            if (launchers.size() < 22 && !seen.contains(id)) {
                seen.insert(id);
                launchers.append(key);
            }
        };
        if (page.value(QStringLiteral("steam")).toObject().value(QStringLiteral("max")).toInt() > 0) {
            for (const SteamGame &game : games) {
                if (SteamLibrary::isTool(game))
                    continue;
                append(game.name, QJsonObject{{QStringLiteral("type"), QStringLiteral("steam")}, {QStringLiteral("appid"), game.appid}});
            }
        }
        for (const DesktopApp &app : apps) {
            if (!app.categories.contains(category))
                continue;
            if (app.steamAppId > 0 && seen.contains(QStringLiteral("steam:%1").arg(app.steamAppId)))
                continue;
            append(app.name, QJsonObject{{QStringLiteral("type"), QStringLiteral("app")}, {QStringLiteral("desktop"), app.file}});
        }
        // A single page retains all 12 positions. Two pages reserve key 11 for navigation.
        if (launchers.size() <= 12) {
            for (qsizetype index = placed.size(); index < launchers.size(); ++index) {
                int free = 0;
                while (placed.contains(free))
                    ++free;
                QJsonObject key = launchers.at(index).toObject();
                key.insert(QStringLiteral("key"), free);
                placed.insert(free, key);
            }
            QJsonArray keys;
            for (const auto &key : std::as_const(placed))
                keys.append(key);
            page.insert(QStringLiteral("keys"), keys);
            pages.insert(name, page);
            continue;
        }
        QJsonArray first;
        QJsonArray second;
        for (qsizetype index = 0; index < launchers.size(); ++index) {
            QJsonObject key = launchers.at(index).toObject();
            key.insert(QStringLiteral("key"), static_cast<int>(index < 11 ? index : index - 11));
            if (index < 11)
                first.append(key);
            else
                second.append(key);
        }
        first.append(QJsonObject{{QStringLiteral("key"), 11},
            {QStringLiteral("label"), (theme.value(QStringLiteral("name")).toString() + QStringLiteral("-Plus")).left(MaxLabelLength)},
            {QStringLiteral("action"), QJsonObject{{QStringLiteral("type"), QStringLiteral("page")}, {QStringLiteral("page"), moreName}}}});
        second.append(QJsonObject{{QStringLiteral("key"), 11}, {QStringLiteral("label"), QStringLiteral("Retour")},
            {QStringLiteral("action"), QJsonObject{{QStringLiteral("type"), QStringLiteral("back")}}}});
        page.insert(QStringLiteral("keys"), first);
        pages.insert(name, page);
        pages.insert(moreName, QJsonObject{{QStringLiteral("keys"), second}, {QStringLiteral("sort"), page.value(QStringLiteral("sort"))}});
    }
    root.insert(QStringLiteral("pages"), pages);
    return root;
}

QByteArray preparePagesJson(const QByteArray &bytes)
{
#ifdef Q_OS_WIN
    if (!parsePagesConfig(bytes).config)
        return bytes;
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    bool automatic = false;
    const QJsonObject pages = root.value(QStringLiteral("pages")).toObject();
    for (auto page = pages.constBegin(); page != pages.constEnd(); ++page) {
        if (!page.value().toObject().contains(QStringLiteral("menu")) || pages.contains(page.key() + QStringLiteral("-plus")))
            continue;
        bool launchersOnly = true;
        const auto keys = page.value().toObject().value(QStringLiteral("keys")).toArray();
        for (const auto &key : keys) {
            const QString type = key.toObject().value(QStringLiteral("action")).toObject().value(QStringLiteral("type")).toString();
            launchersOnly = launchersOnly && (type == QStringLiteral("app") || type == QStringLiteral("steam"));
        }
        automatic = automatic || launchersOnly;
    }
    if (!automatic)
        return bytes;
    // Shared by initial content and reloads; never re-enumerate the Shell per key or edit.
    static QElapsedTimer age;
    static QList<DesktopApp> apps;
    static QList<SteamGame> games;
    if (!age.isValid() || age.elapsed() > 300000) {
        apps = DesktopEntries::list();
        games = SteamLibrary::installedGames();
        age.start();
    }
    const QByteArray expanded = QJsonDocument(paginateWindowsLaunchers(root, apps, games)).toJson();
    return parsePagesConfig(expanded).config ? expanded : bytes;
#else
    return bytes;
#endif
}

QByteArray initialPagesJson()
{
#ifdef Q_OS_WIN
    return preparePagesJson(windowsInitialPagesJson(
        QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("System32")),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../share/galleoncore/screensavers"))));
#else
    return defaultPagesJson();
#endif
}
}
