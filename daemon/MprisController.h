// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "MediaController.h"

#include <QDBusConnection>
#include <QElapsedTimer>
#include <QHash>
#include <QTimer>

namespace galleond {

// Controls a music player through MPRIS (org.mpris.MediaPlayer2.*), the
// standard D-Bus interface of Linux players (Spotify, VLC, Elisa, browsers...).
//
// The player is the one playing, else the one that played last, else any. The
// session is polled (names every 3 s, state every second) with asynchronous
// calls only: nothing here blocks the event loop, so the daemon keeps polling
// its keys. After a command the state is read again at once.
class MprisController : public MediaController
{
    Q_OBJECT

public:
    // `namePrefix` selects which players are considered; the tests use a
    // private one. `playerctld` (a proxy of the last player) is always skipped:
    // it would count the same player twice.
    explicit MprisController(QDBusConnection bus = QDBusConnection::sessionBus(),
                             QString namePrefix = QStringLiteral("org.mpris.MediaPlayer2."),
                             QObject *parent = nullptr);

    [[nodiscard]] State state() const override;
    [[nodiscard]] NowPlaying nowPlaying() const override;

    void playPause() override;
    void next() override;
    void previous() override;
    void stop() override;
    void changeVolume(int percent) override;
    void seek(int seconds) override;

    // For tests: poll now instead of waiting for the timers.
    void refreshNow();

private:
    struct Player
    {
        State state = State::Stopped;
        NowPlaying track;
        qint64 lastPlaying = 0; // ordering: the highest was playing most recently
    };

    void refreshNames();
    void refreshState(const QString &name);
    void setPlayers();
    [[nodiscard]] QString current() const;
    void callPlayer(const QString &method, const QVariantList &arguments = {});

    QDBusConnection m_bus;
    QString m_prefix;
    QHash<QString, Player> m_players;
    QTimer m_namesTimer;
    QTimer m_stateTimer;
    QElapsedTimer m_clock;
    State m_reported = State::NoPlayer;
    NowPlaying m_reportedTrack;
    // Volume steps arrive faster than a D-Bus round trip: they add up here and
    // one read-then-write at a time applies them, so none is lost.
    int m_pendingVolume = 0;
    bool m_volumeBusy = false;

    void applyPendingVolume();
};

} // namespace galleond
