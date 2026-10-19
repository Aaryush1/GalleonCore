// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "MediaController.h"

#include <QDBusConnection>
#include <QDBusMessage>
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
    // The program that sets the system volume when no player is there (default: wpctl
    // when it is installed). Empty: the dial then does nothing without a player. For the tests.
    void setSystemVolumeProgram(const QString &program) { m_systemVolumeProgram = program; }

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

    // The icon theme name of a player application, from its MPRIS DesktopEntry
    // ("vlc" -> the Icon= of vlc.desktop). Falls back to the entry itself; empty
    // for an entry that is not a plain name.
    [[nodiscard]] static QString iconNameForDesktopEntry(const QString &desktopEntry);

    // For tests: poll now instead of waiting for the timers.
    void refreshNow();
    // For tests: slow the periodic polling down, to show that a change is seen
    // through the player's notifications and not by the next poll.
    void setPollIntervals(int namesMs, int stateMs);

private Q_SLOTS:
    // The player says its status or its track changed: read it at once.
    void onPropertiesChanged(const QDBusMessage &message);
    // A player appeared or quit.
    void onNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);

private:
    struct Player
    {
        State state = State::Stopped;
        NowPlaying track;
        QString iconName; // from the root interface, read once
        qint64 lastPlaying = 0; // ordering: the highest was playing most recently
    };

    void refreshNames();
    void refreshState(const QString &name);
    void fetchIdentity(const QString &name);
    void setPlayers();
    [[nodiscard]] QString current() const;
    void callPlayer(const QString &method, const QVariantList &arguments = {});

    QDBusConnection m_bus;
    QString m_prefix;
    QHash<QString, Player> m_players;
    QTimer m_namesTimer;
    QTimer m_stateTimer;
    QTimer m_changeTimer; // joins the bursts of notifications a player sends at a track change
    QElapsedTimer m_clock;
    State m_reported = State::NoPlayer;
    NowPlaying m_reportedTrack;
    // Volume steps arrive faster than a D-Bus round trip: they add up here and
    // one read-then-write at a time applies them, so none is lost.
    // Without any player, the dial sets the system volume (PipeWire, wpctl) instead.
    void applySystemVolume(int percent);
    QString m_systemVolumeProgram;
    int m_pendingSystemVolume = 0;
    QTimer m_systemVolumeTimer;
    int m_pendingVolume = 0;
    bool m_volumeBusy = false;

    void applyPendingVolume();
};

} // namespace galleond
