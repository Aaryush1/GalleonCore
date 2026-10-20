// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SmtcController.h"

#include <QDebug>
#include <QMetaObject>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>

namespace galleond {

SmtcController::SmtcController(std::unique_ptr<SmtcSession> session, QObject *parent)
    : MediaController(parent), m_session(std::move(session))
{
    Q_ASSERT(m_session);
    m_worker = std::jthread([this](std::stop_token stop) { run(std::move(stop)); });
}

SmtcController::~SmtcController()
{
    m_worker.request_stop();
    m_wake.notify_all();
    m_worker.join();
}

void SmtcController::enqueue(SmtcAction action, int value)
{
    if (m_snapshot.state == State::NoPlayer && action != SmtcAction::Volume && action != SmtcAction::Mute)
        return;
    const std::lock_guard lock(m_mutex);
    if (m_commands.size() >= 16)
        return;
    m_commands.push_back({action, value, m_snapshot.session});
    m_wake.notify_one();
}

void SmtcController::playPause() { enqueue(SmtcAction::PlayPause); }
void SmtcController::next() { enqueue(SmtcAction::Next); }
void SmtcController::previous() { enqueue(SmtcAction::Previous); }
void SmtcController::stop() { enqueue(SmtcAction::Stop); }
void SmtcController::seek(int seconds) { enqueue(SmtcAction::Seek, seconds); }
void SmtcController::toggleShuffle() { enqueue(SmtcAction::Shuffle); }
void SmtcController::cycleRepeat() { enqueue(SmtcAction::Repeat); }
void SmtcController::restartTrack() { enqueue(SmtcAction::Restart); }
void SmtcController::changeVolume(int percent)
{
    if (percent != 0)
        enqueue(SmtcAction::Volume, percent);
}
void SmtcController::toggleMute() { enqueue(SmtcAction::Mute); }

void SmtcController::publish(SmtcSnapshot snapshot)
{
    snapshot.track.title = cleanMetadataText(snapshot.track.title);
    snapshot.track.artist = cleanMetadataText(snapshot.track.artist);
    if (!std::isfinite(snapshot.track.durationSeconds) || snapshot.track.durationSeconds < 0)
        snapshot.track.durationSeconds = 0;
    if (!std::isfinite(snapshot.track.positionSeconds) || snapshot.track.positionSeconds < 0)
        snapshot.track.positionSeconds = 0;
    if (snapshot.track.durationSeconds > 0)
        snapshot.track.positionSeconds = std::min(snapshot.track.positionSeconds, snapshot.track.durationSeconds);
    if (snapshot.state == State::NoPlayer)
        snapshot = {};
    const std::lock_guard lock(m_mutex);
    m_pending = std::move(snapshot);
    if (!m_deliveryQueued) {
        m_deliveryQueued = true;
        QMetaObject::invokeMethod(this, [this] { deliver(); }, Qt::QueuedConnection);
    }
}

void SmtcController::deliver()
{
    SmtcSnapshot snapshot;
    {
        const std::lock_guard lock(m_mutex);
        snapshot = m_pending;
        m_deliveryQueued = false;
    }
    const bool stateChanged = snapshot.state != m_snapshot.state;
    const bool trackChanged = !(snapshot.track == m_snapshot.track) || stateChanged
                              || snapshot.shuffle != m_snapshot.shuffle || snapshot.loop != m_snapshot.loop;
    m_snapshot = std::move(snapshot);
    if (stateChanged)
        Q_EMIT MediaController::stateChanged();
    if (trackChanged)
        Q_EMIT nowPlayingChanged();
}

void SmtcController::run(std::stop_token stop)
{
    bool failed = false;
    bool commandFailed = false;
    try {
        m_session->start();
        while (!stop.stop_requested()) {
            std::deque<SmtcCommand> commands;
            {
                const std::lock_guard lock(m_mutex);
                commands.swap(m_commands);
            }
            for (const auto &command : commands) {
                if (stop.stop_requested())
                    break;
                try {
                    m_session->command(command);
                    commandFailed = false;
                } catch (const std::exception &) {
                    if (!commandFailed)
                        qWarning() << "galleond: Windows media/audio command unavailable";
                    commandFailed = true;
                    // An unplugged audio output must not erase the player or
                    // discard the remaining commands in this batch.
                }
            }
            try {
                if (stop.stop_requested())
                    break;
                publish(m_session->read());
                failed = false;
            } catch (const std::exception &) {
                if (!failed)
                    qWarning() << "galleond: SMTC session unavailable";
                failed = true;
                publish({});
            }
            std::unique_lock lock(m_mutex);
            m_wake.wait_for(lock, std::chrono::seconds(1), [&] { return stop.stop_requested() || !m_commands.empty(); });
        }
    } catch (const std::exception &) {
        qWarning() << "galleond: SMTC initialization failed";
        publish({});
    }
    m_session->stop();
}

} // namespace galleond
