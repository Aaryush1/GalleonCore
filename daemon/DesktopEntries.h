// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QStringList>
#include <QString>

namespace galleond {

// An application described by a freedesktop .desktop file.
struct DesktopApp
{
    QString file; // absolute path of the .desktop file
    QString id;   // "firefox.desktop"
    QString name; // in the user's language when the file has it
    QString icon;     // icon theme name; empty if the file has none or gives a path
    QString iconPath; // absolute path of an image, when the file gives a path (Snap packages do)
    QStringList categories; // the menu categories (Game, Utility, ...) of the file
    int steamAppId = 0;     // the Steam game that an Exec of steam://rungameid/<n> starts, else 0

    [[nodiscard]] bool isValid() const { return !file.isEmpty(); }
};

// Looks up the applications the desktop knows, to put one on a key without
// typing its command: the key only names a .desktop file, and the desktop's own
// launcher (gio) starts it, with the file's Exec, working directory, terminal
// flag and the rest.
class DesktopEntries
{
public:
    // `desktop`: a desktop file ID ("firefox.desktop", "org.kde.dolphin.desktop")
    // looked up in the applications folders of XDG_DATA_HOME and XDG_DATA_DIRS,
    // or the absolute path of a .desktop file. Not valid or not found: an
    // invalid DesktopApp.
    [[nodiscard]] static DesktopApp lookup(const QString &desktop);

    // Every application that wants to be listed (no Hidden, no NoDisplay), by
    // name, for a picker. A file counts only if it is an Application with an Exec.
    [[nodiscard]] static QList<DesktopApp> list();
#ifdef Q_OS_WIN
    // Initial button labels do not require Shell icon extraction or Steam artwork.
    [[nodiscard]] static QList<DesktopApp> list(bool includeIcons);
#endif

    // The listed applications of one menu category ("Game"): the same as list(), filtered.
    [[nodiscard]] static QList<DesktopApp> listCategory(const QString &category);

    // The names accepted in a pages file.
    [[nodiscard]] static bool isValidReference(const QString &desktop);

    // Starts the application through the desktop's launcher (gio launch, else
    // gtk-launch, else kioclient). Returns false if none could be started. The
    // program gets the file as ONE argument; no shell is involved.
    static bool launch(const DesktopApp &app);

private:
    [[nodiscard]] static DesktopApp read(const QString &file, const QString &id);
#ifdef Q_OS_WIN
    [[nodiscard]] static DesktopApp read(const QString &file, const QString &id, bool includeIcons);
#endif
};

} // namespace galleond
