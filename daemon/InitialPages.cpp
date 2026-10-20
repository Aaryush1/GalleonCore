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

namespace galleond {
QStringList windowsAppCategories(const QString &name, const QString &folder)
{
    const QString lower = name.toLower();
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

QByteArray initialPagesJson()
{
#ifdef Q_OS_WIN
    QJsonObject root = QJsonDocument::fromJson(windowsInitialPagesJson(
        QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("System32")),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../share/galleoncore/screensavers")))).object();
    QJsonObject pages = root.value(QStringLiteral("pages")).toObject();
    for (const QString &pageName : {QStringLiteral("multimedia"), QStringLiteral("jeux")}) {
        QJsonObject page = pages.value(pageName).toObject();
        QJsonArray keys;
        if (pageName == QStringLiteral("jeux")) {
            for (const SteamGame &game : SteamLibrary::installedGames()) {
                if (keys.size() >= 12)
                    break;
                keys.append(QJsonObject{{QStringLiteral("key"), static_cast<int>(keys.size())}, {QStringLiteral("label"), game.name.left(MaxLabelLength)},
                    {QStringLiteral("action"), QJsonObject{{QStringLiteral("type"), QStringLiteral("steam")}, {QStringLiteral("appid"), game.appid}}}});
            }
        }
        const QString category = page.value(QStringLiteral("menu")).toObject().value(QStringLiteral("category")).toString();
        for (const DesktopApp &app : DesktopEntries::listCategory(category)) {
            if (keys.size() >= 12)
                break;
            keys.append(QJsonObject{{QStringLiteral("key"), static_cast<int>(keys.size())}, {QStringLiteral("label"), app.name.left(MaxLabelLength)},
                {QStringLiteral("action"), QJsonObject{{QStringLiteral("type"), QStringLiteral("app")}, {QStringLiteral("desktop"), app.file}}}});
        }
        page.insert(QStringLiteral("keys"), keys);
        pages.insert(pageName, page);
    }
    root.insert(QStringLiteral("pages"), pages);
    return QJsonDocument(root).toJson();
#else
    return defaultPagesJson();
#endif
}
}
