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
