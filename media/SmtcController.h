// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "SmtcSession.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace galleond {

// Qt-facing controller. Native calls never block the UI; both queues are bounded.
class SmtcController final : public MediaController
{
    Q_OBJECT
public:
    explicit SmtcController(std::unique_ptr<SmtcSession> session, QObject *parent = nullptr);
    ~SmtcController() override;
    [[nodiscard]] State state() const override { return m_snapshot.state; }
    [[nodiscard]] NowPlaying nowPlaying() const override { return m_snapshot.track; }
    [[nodiscard]] bool shuffleOn() const override { return m_snapshot.shuffle; }
    [[nodiscard]] Loop loopMode() const override { return m_snapshot.loop; }
    void playPause() override;
    void next() override;
    void previous() override;
    void stop() override;
    void seek(int seconds) override;
    void toggleShuffle() override;
    void cycleRepeat() override;
    void restartTrack() override;
    // System output volume/mute use Core Audio, independently of the player.
    void changeVolume(int percent) override;
    void toggleMute() override;

private:
    void enqueue(SmtcAction action, int value = 0);
    void run(std::stop_token stop);
    void publish(SmtcSnapshot snapshot);
    void deliver();

    std::unique_ptr<SmtcSession> m_session;
    SmtcSnapshot m_snapshot;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<SmtcCommand> m_commands;
    SmtcSnapshot m_pending;
    bool m_deliveryQueued = false;
    std::jthread m_worker; // joined before any state or native session is destroyed
};

} // namespace galleond
