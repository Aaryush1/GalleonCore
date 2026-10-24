// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DaemonStatus.h"
#include "LocalControl.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>

namespace bosun {
DaemonStatus::DaemonStatus(QString systemctl, QObject *parent)
    : DaemonStatus(std::move(systemctl), galleond::localControlName(), parent)
{
}
DaemonStatus::DaemonStatus(QString program, QString endpoint, QObject *parent)
    : QObject(parent), m_systemctl(std::move(program)), m_endpoint(std::move(endpoint))
{
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, &DaemonStatus::look);
    connect(&m_probe, &QLocalSocket::connected, this, [this] {
        setRunning(true);
        m_probe.abort();
    });
    connect(&m_probe, &QLocalSocket::errorOccurred, this, [this] { setRunning(false); });
    m_poll.start();
    look();
}

void DaemonStatus::setRunning(bool running)
{
    if (m_running != running) {
        m_running = running;
        Q_EMIT changed();
    }
}

void DaemonStatus::look()
{
    if (m_probe.state() != QLocalSocket::UnconnectedState)
        return;
    m_probe.connectToServer(m_endpoint);
    QTimer::singleShot(500, &m_probe, [this] {
        if (m_probe.state() != QLocalSocket::UnconnectedState) {
            m_probe.abort();
            setRunning(false);
        }
    });
}

QStringList DaemonStatus::userCommand(const QString &verb)
{
    Q_UNUSED(verb)
    return {};
}

QString DaemonStatus::ask(const QString &verb)
{
    if (verb == QStringLiteral("stop")) {
        galleond::sendLocalCommand(m_endpoint, {galleond::LocalAction::Stop, {}}, this);
        return {};
    }
    const QString program = m_systemctl.isEmpty()
        ? QCoreApplication::applicationDirPath() + QStringLiteral("/galleond.exe") : m_systemctl;
    if (!QFileInfo(program).isAbsolute() || !QFileInfo(program).isFile()
        || !QProcess::startDetached(program, {}))
        return QCoreApplication::translate("DaemonStatus", "galleond could not be started.");
    return {};
}
QString DaemonStatus::showGaming() {
    if (!running()) return tr("Start the daemon to display Gaming on the pad");
    galleond::sendLocalCommand(m_endpoint, {galleond::LocalAction::Gaming, {}}, this);
    return {};
}
QString DaemonStatus::start() { return running() ? QString{} : ask(QStringLiteral("start")); }
QString DaemonStatus::stop() { return running() ? ask(QStringLiteral("stop")) : QString{}; }
} // namespace bosun
