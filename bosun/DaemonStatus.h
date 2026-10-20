// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#ifdef Q_OS_WIN
#include <QTimer>
#include <QLocalSocket>
#else
#include <QDBusServiceWatcher>
#endif

namespace bosun {

// Reports whether galleond is available and requests its lifecycle operations.
// Linux uses the session bus and systemd; Windows uses a per-user local endpoint
// and starts the sibling executable. This window never opens the device.
class DaemonStatus : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY changed)

public:
    static constexpr const char *BusName = "org.galleoncore.Deck";
    static constexpr const char *ServiceName = "galleond.service";

    // Empty selects systemctl on Linux or the sibling galleond.exe on Windows.
    // Tests can supply a fake program.
    explicit DaemonStatus(QString systemctl = {}, QObject *parent = nullptr);

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
#ifdef Q_OS_WIN
    void setRunning(bool running);
    QTimer m_poll;
    QLocalSocket m_probe;
#else
    QDBusServiceWatcher m_watcher;
#endif
    bool m_running = false;
};

} // namespace bosun
