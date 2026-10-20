// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

namespace galleond {

struct EditResult
{
    bool ok = false;
    QStringList errors; // one line each; empty when ok
};

// What a key does and shows, as the settings window describes it. Only the fields of its
// `type` are used: "app" (desktop), "media" (command, and step for volume or seconds for seek),
// "page" (page), "back", "key" (keyName: a key of the keypad) or "steam" (appid).
struct KeySpec
{
    QString type;
    QString desktop;
    QString command;
    int step = 0;
    int seconds = 0;
    QString page;
    QString keyName;
    int appid = 0;
    QString label; // empty: the default (an application's own name)
    QString icon;  // empty: the default (an application's own icon)

    friend bool operator==(const KeySpec &, const KeySpec &) = default;
};

// Changes one key of the pages file for the user, so that putting an
// application on a key needs no JSON. A running galleond reads the change by
// itself.
//
// The edit is careful: a file that is already invalid is left alone (its errors
// are returned), the application must exist, the result is validated like any
// pages file before anything is written, the previous file is kept as
// `<file>.bak`, and the new one is written atomically with the permissions the
// old one had.
class PagesEditor
{
public:
    // Puts the application `desktop` (a .desktop file ID or path) on key `key`
    // of page `page`. The key keeps no old label or icon: the application's own
    // are used. A missing file is started from the built-in pages.
    static EditResult assignApp(const QString &file, const QString &page, int key, const QString &desktop);

    // Empties key `key` of page `page`.
    static EditResult clearKey(const QString &file, const QString &page, int key);
    // Puts on key `key` of `page` what `spec` says, whatever was there (the old label and icon
    // go; the words of a shuffle or repeat key stay when it stays one).
    static EditResult setKey(const QString &file, const QString &page, int key, const KeySpec &spec);

    // A second page for `parent` (a "plus" page): `name` is made, empty but for a key that goes
    // back, and a key of `parent` leads to it (the last free one, the 12th when it is free). Refused
    // when `parent` has no free key or `name` exists.
    static EditResult addSubPage(const QString &file, const QString &parent, const QString &name, const QString &moreLabel,
                                 const QString &backLabel);

    // The themes (the tabs of the window). `page` is the name of the page a new theme opens: it is
    // made, empty. A theme that is gone takes its page with it unless something still leads there.
    static EditResult addTheme(const QString &file, const QString &name, const QString &color, const QString &page);
    static EditResult setTheme(const QString &file, int index, const QString &name, const QString &color);
    // The theme of the home page cannot be removed (it is where the module starts), nor can the last one.
    static EditResult removeTheme(const QString &file, int index);
    // Moves theme `index` by `delta` places in the list (the order of the tabs).
    static EditResult moveTheme(const QString &file, int index, int delta);

    // The screensaver. Each change is validated like any pages file before it is written,
    // and leaves the rest of the file (the geometry, the other videos) as it was.
    static EditResult setScreensaverIdleMinutes(const QString &file, int minutes);
    static EditResult setScreensaverFps(const QString &file, int fps);
    static EditResult setScreensaverWhilePlaying(const QString &file, bool whilePlaying);
    // The title of video `index` (its place in the list); an empty title takes the title away.
    static EditResult setScreensaverVideoTitle(const QString &file, int index, const QString &title);
    // Adds the video file `video` (as given: an absolute path, "~/..." or relative to the folder
    // of the pages file) at the end of the list, with a title when it is not empty. Refused
    // when the list is full or the file is already in it. The file is not checked here: the
    // caller has looked at it.
    static EditResult addScreensaverVideo(const QString &file, const QString &video, const QString &title);
    // Takes video `index` out of the list. The last one gone, the screensaver is off.
    static EditResult removeScreensaverVideo(const QString &file, int index);
};

} // namespace galleond
