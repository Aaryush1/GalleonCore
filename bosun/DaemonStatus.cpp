// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DaemonStatus.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace bosun {

DaemonStatus::DaemonStatus(QString systemctl, QObject *parent)
    : QObject(parent)
    , m_systemctl(std::move(systemctl))
    , m_watcher(QString::fromLatin1(BusName), QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange)
{
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] { look(); });
    look();
}

void DaemonStatus::look()
{
    const QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    const bool running = bus && bus->isServiceRegistered(QString::fromLatin1(BusName));
    if (running == m_running)
        return;
    m_running = running;
    Q_EMIT changed();
}

QStringList DaemonStatus::userCommand(const QString &verb)
{
    return {QStringLiteral("--user"), verb, QString::fromLatin1(ServiceName)};
}

QString DaemonStatus::ask(const QString &verb)
{
    const QString program = QFileInfo(m_systemctl).isAbsolute() ? m_systemctl : QStandardPaths::findExecutable(m_systemctl);
    if (program.isEmpty())
        return QCoreApplication::translate("DaemonStatus", "systemctl was not found: start galleond by hand.");
    // The request goes out and is forgotten: whether it worked shows when the name appears on the bus.
    if (!QProcess::startDetached(program, userCommand(verb)))
        return QCoreApplication::translate("DaemonStatus", "systemctl could not be started.");
    return {};
}

QString DaemonStatus::start()
{
    return running() ? QString() : ask(QStringLiteral("start"));
}

QString DaemonStatus::stop()
{
    return running() ? ask(QStringLiteral("stop")) : QString();
}

} // namespace bosun
