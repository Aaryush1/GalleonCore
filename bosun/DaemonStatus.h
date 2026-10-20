// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>

namespace bosun {

// Whether galleond is running (does it own org.galleoncore.Deck on the session bus?), so that
// the window can say whether a change reaches the module at once, and can start or stop the
// daemon through its systemd user service. Starting and stopping only ask systemd: this
// window never talks to the device.
class DaemonStatus : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY changed)

public:
    static constexpr const char *BusName = "org.galleoncore.Deck";
    static constexpr const char *ServiceName = "galleond.service";

    // `systemctl` is the program to ask (a name or a path); tests give a fake one.
    explicit DaemonStatus(QString systemctl = QStringLiteral("systemctl"), QObject *parent = nullptr);

    [[nodiscard]] bool running() const { return m_running; }

    // The arguments given to systemctl to start or stop the service of the user. Pure.
    [[nodiscard]] static QStringList userCommand(const QString &verb);

    // "" when the request was sent (the answer is the name appearing on the bus), else why not.
    Q_INVOKABLE [[nodiscard]] QString start();
    Q_INVOKABLE [[nodiscard]] QString stop();

Q_SIGNALS:
    void changed();

private:
    [[nodiscard]] QString ask(const QString &verb);
    void look();

    QString m_systemctl;
    QDBusServiceWatcher m_watcher;
    bool m_running = false;
};

} // namespace bosun
