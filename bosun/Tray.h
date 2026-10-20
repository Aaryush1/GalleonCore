// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "TrayMenu.h"

#include <QIcon>
#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>

namespace bosun {

// The icon in the notification area (the system tray of the desktop) and its menu. It only
// shows the entries of trayEntries() and says which one was chosen; what they do is up to the
// owner. A click on the icon shows or hides the window.
class Tray : public QObject
{
    Q_OBJECT

public:
    explicit Tray(const QIcon &icon, QObject *parent = nullptr);

    // False when the desktop has no notification area: the window then stays an ordinary window.
    [[nodiscard]] static bool isAvailable();

    void setIcon(const QIcon &icon);
    void show();
    // The menu is rebuilt for the state of the window and of galleond.
    void update(bool windowShown, bool daemonRunning);

Q_SIGNALS:
    void chosen(bosun::TrayEntry::Action action);

private:
    QSystemTrayIcon m_icon;
    QMenu m_menu;
};

} // namespace bosun
