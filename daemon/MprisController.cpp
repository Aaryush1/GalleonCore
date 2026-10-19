// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MprisController.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDebug>

#include <algorithm>
#include <utility>

namespace galleond {

namespace {

constexpr int NamesIntervalMs = 3000;
constexpr int StateIntervalMs = 1000;
constexpr int CallTimeoutMs = 1500;
constexpr int MaxPlayers = 16; // a session never has this many: bounds the polling

const QString PlayerPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString PlayerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString PropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

MediaController::State stateFromStatus(const QString &status)
{
    if (status == QLatin1String("Playing"))
        return MediaController::State::Playing;
    if (status == QLatin1String("Paused"))
        return MediaController::State::Paused;
    return MediaController::State::Stopped;
}

} // namespace

MprisController::MprisController(QDBusConnection bus, QString namePrefix, QObject *parent)
    : MediaController(parent)
    , m_bus(std::move(bus))
    , m_prefix(std::move(namePrefix))
{
    m_clock.start();
    m_namesTimer.setInterval(NamesIntervalMs);
    m_stateTimer.setInterval(StateIntervalMs);
    connect(&m_namesTimer, &QTimer::timeout, this, &MprisController::refreshNames);
    connect(&m_stateTimer, &QTimer::timeout, this, &MprisController::setPlayers);
    m_namesTimer.start();
    m_stateTimer.start();
    refreshNow();
}

MediaController::State MprisController::state() const
{
    const QString name = current();
    return name.isEmpty() ? State::NoPlayer : m_players.value(name).state;
}

QString MprisController::current() const
{
    QString best;
    for (auto it = m_players.constBegin(); it != m_players.constEnd(); ++it) {
        if (best.isEmpty()) {
            best = it.key();
            continue;
        }
        const Player &candidate = it.value();
        const Player &chosen = m_players.value(best);
        const bool candidatePlaying = candidate.state == State::Playing;
        const bool chosenPlaying = chosen.state == State::Playing;
        if (candidatePlaying != chosenPlaying ? candidatePlaying
            : candidate.lastPlaying != chosen.lastPlaying ? candidate.lastPlaying > chosen.lastPlaying
                                                          : it.key() < best)
            best = it.key();
    }
    return best;
}

void MprisController::refreshNow()
{
    refreshNames();
}

void MprisController::refreshNames()
{
    if (!m_bus.isConnected())
        return;
    // ListNames asynchronously (registeredServiceNames() would block).
    const QDBusMessage listNames = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
                                                                  QStringLiteral("/org/freedesktop/DBus"),
                                                                  QStringLiteral("org.freedesktop.DBus"),
                                                                  QStringLiteral("ListNames"));
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(listNames, CallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusPendingReply<QStringList> reply = *call;
        if (reply.isError())
            return;

        QStringList found;
        for (const QString &name : reply.value()) {
            if (name.startsWith(m_prefix) && !name.endsWith(QLatin1String(".playerctld")) && found.size() < MaxPlayers)
                found.append(name);
        }
        for (auto it = m_players.begin(); it != m_players.end();) {
            if (found.contains(it.key()))
                ++it;
            else
                it = m_players.erase(it);
        }
        for (const QString &name : std::as_const(found)) {
            if (!m_players.contains(name))
                m_players.insert(name, Player{});
        }
        setPlayers(); // reads the states, and reports a change
    });
}

void MprisController::setPlayers()
{
    // Reads the state of every known player, then reports a change if the
    // answer of the current one is different from what was last reported.
    for (auto it = m_players.constBegin(); it != m_players.constEnd(); ++it)
        refreshState(it.key());
    if (m_players.isEmpty() && m_reported != State::NoPlayer) {
        m_reported = State::NoPlayer;
        Q_EMIT stateChanged();
    }
}

void MprisController::refreshState(const QString &name)
{
    QDBusMessage message = QDBusMessage::createMethodCall(name, PlayerPath, PropertiesInterface, QStringLiteral("Get"));
    message << PlayerInterface << QStringLiteral("PlaybackStatus");
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, CallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, name](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        auto player = m_players.find(name);
        if (player == m_players.end())
            return;
        const QDBusPendingReply<QDBusVariant> reply = *call;
        player->state = reply.isError() ? State::Stopped : stateFromStatus(reply.value().variant().toString());
        if (player->state == State::Playing)
            player->lastPlaying = m_clock.elapsed();

        const State now = state();
        if (now != m_reported) {
            m_reported = now;
            Q_EMIT stateChanged();
        }
    });
}

void MprisController::callPlayer(const QString &method, const QVariantList &arguments)
{
    const QString name = current();
    if (name.isEmpty())
        return;
    QDBusMessage message = QDBusMessage::createMethodCall(name, PlayerPath, PlayerInterface, method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, CallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, name, method](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        if (call->isError())
            qWarning() << "galleond: the player" << name << "refused" << method << call->error().message();
        refreshState(name); // the state changed (or not): the icon must follow
    });
}

void MprisController::playPause()
{
    callPlayer(QStringLiteral("PlayPause"));
}

void MprisController::next()
{
    callPlayer(QStringLiteral("Next"));
}

void MprisController::previous()
{
    callPlayer(QStringLiteral("Previous"));
}

void MprisController::stop()
{
    callPlayer(QStringLiteral("Stop"));
}

void MprisController::seek(int seconds)
{
    // MPRIS Seek takes an offset in microseconds.
    callPlayer(QStringLiteral("Seek"), {QVariant::fromValue<qlonglong>(static_cast<qlonglong>(seconds) * 1000000LL)});
}

void MprisController::changeVolume(int percent)
{
    m_pendingVolume = std::clamp(m_pendingVolume + percent, -200, 200); // bounded: a dial cannot queue more
    if (!m_volumeBusy)
        applyPendingVolume();
}

void MprisController::applyPendingVolume()
{
    const QString name = current();
    if (name.isEmpty() || m_pendingVolume == 0) {
        m_pendingVolume = 0;
        return;
    }
    // Read the volume, then write it back moved: Volume is a property between
    // 0.0 and 1.0 (some players do not support it: the read then fails and
    // nothing is written).
    const int percent = std::exchange(m_pendingVolume, 0);
    m_volumeBusy = true;
    QDBusMessage get = QDBusMessage::createMethodCall(name, PlayerPath, PropertiesInterface, QStringLiteral("Get"));
    get << PlayerInterface << QStringLiteral("Volume");
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(get, CallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, name, percent](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusPendingReply<QDBusVariant> reply = *call;
        bool ok = false;
        const double volume = reply.isError() ? 0.0 : reply.value().variant().toDouble(&ok);
        if (!ok) {
            m_volumeBusy = false;
            m_pendingVolume = 0; // the player has no volume: drop what piled up
            return;
        }
        const double target = std::clamp(volume + (percent / 100.0), 0.0, 1.0);
        QDBusMessage set = QDBusMessage::createMethodCall(name, PlayerPath, PropertiesInterface, QStringLiteral("Set"));
        set << PlayerInterface << QStringLiteral("Volume") << QVariant::fromValue(QDBusVariant(target));
        auto *setWatcher = new QDBusPendingCallWatcher(m_bus.asyncCall(set, CallTimeoutMs), this);
        connect(setWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *done) {
            done->deleteLater();
            m_volumeBusy = false;
            if (m_pendingVolume != 0)
                applyPendingVolume(); // steps came in meanwhile
        });
    });
}

} // namespace galleond
