// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Tray.h"

namespace bosun {

Tray::Tray(const QIcon &icon, QObject *parent)
    : QObject(parent)
    , m_icon(icon)
{
    m_icon.setContextMenu(&m_menu);
    m_icon.setToolTip(QStringLiteral("Galleon Bosun"));
    // A left click shows or hides the window; the menu comes with the right click.
    connect(&m_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            Q_EMIT chosen(TrayEntry::Action::ToggleWindow);
    });
}

bool Tray::isAvailable()
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void Tray::setIcon(const QIcon &icon)
{
    m_icon.setIcon(icon);
}

void Tray::show()
{
    m_icon.show();
}

void Tray::update(bool windowShown, bool daemonRunning)
{
    m_menu.clear();
    for (const TrayEntry &entry : trayEntries(windowShown, daemonRunning)) {
        QAction *item = m_menu.addAction(entry.label);
        const TrayEntry::Action action = entry.action;
        connect(item, &QAction::triggered, this, [this, action] { Q_EMIT chosen(action); });
    }
}

} // namespace bosun
