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
};

} // namespace galleond
