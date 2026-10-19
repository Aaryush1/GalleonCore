// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include <cmath>

namespace galleond {

inline constexpr int MaxMetadataLength = 200;

// What a player says about itself is untrusted: no control characters (a
// newline in a title would break the layout), no unbounded length.
[[nodiscard]] inline QString cleanMetadataText(const QString &text)
{
    QString out;
    out.reserve(qMin(text.size(), MaxMetadataLength));
    for (const QChar c : text) {
        if (out.size() >= MaxMetadataLength)
            break;
        out.append(c.isPrint() || c == QLatin1Char(' ') ? c : QLatin1Char(' '));
    }
    return out.simplified();
}

// The track being played, as the player describes it.
struct NowPlaying
{
    QString title;
    QString artist;
    QString artUrl;             // as given by the player: file:// or https://, checked before use
    QString url;                // xesam:url, what is being played (a file:// path for a local file)
    QString playerIcon;         // icon theme name of the player application, for a track without a cover
    double positionSeconds = 0; // 0 when unknown
    double durationSeconds = 0; // 0 when unknown (a stream)

    [[nodiscard]] bool isEmpty() const { return title.isEmpty() && artist.isEmpty(); }
    // Position rounded to the second: what a timeline shows.
    friend bool operator==(const NowPlaying &a, const NowPlaying &b)
    {
        return a.title == b.title && a.artist == b.artist && a.artUrl == b.artUrl && a.url == b.url
               && a.playerIcon == b.playerIcon
               && std::floor(a.positionSeconds) == std::floor(b.positionSeconds)
               && std::floor(a.durationSeconds) == std::floor(b.durationSeconds);
    }
};

// What the button pages need from a music player. The real one talks MPRIS
// over D-Bus; the tests use a fake.
class MediaController : public QObject
{
    Q_OBJECT

public:
    enum class State : std::uint8_t {
        NoPlayer,
        Stopped,
        Paused,
        Playing,
    };

    // The repeat mode of a player, as MPRIS has it: it does not tell an album from a playlist.
    enum class Loop : std::uint8_t {
        None,
        Playlist,
        Track,
    };

    using QObject::QObject;

    [[nodiscard]] virtual State state() const = 0;
    // The track of the current player; empty when there is none.
    [[nodiscard]] virtual NowPlaying nowPlaying() const = 0;

    virtual void playPause() = 0;
    virtual void next() = 0;
    virtual void previous() = 0;
    virtual void stop() = 0;
    // `percent`: signed, in percent of the full volume.
    virtual void changeVolume(int percent) = 0;
    // `seconds`: signed offset from the current position.
    virtual void seek(int seconds) = 0;

    // More controls, for a player that supports them (a default that does nothing, so that a
    // fake or a limited controller needs no change): shuffle on/off, repeat none/playlist/track,
    // mute the system sound, bring the player's window to the front, full screen on/off (a video
    // player), and back to the start of the track.
    // What the player says now (false and none without a player or when it does not say).
    [[nodiscard]] virtual bool shuffleOn() const { return false; }
    [[nodiscard]] virtual Loop loopMode() const { return Loop::None; }
    virtual void toggleShuffle() {}
    virtual void cycleRepeat() {}
    virtual void toggleMute() {}
    virtual void raisePlayer() {}
    virtual void toggleFullscreen() {}
    virtual void restartTrack() {}

    // The name used by the icons of a key: "playing", "paused" or "stopped".
    [[nodiscard]] static QString stateName(State state)
    {
        switch (state) {
        case State::Playing:
            return QStringLiteral("playing");
        case State::Paused:
            return QStringLiteral("paused");
        case State::Stopped:
        case State::NoPlayer:
            break;
        }
        return QStringLiteral("stopped");
    }

Q_SIGNALS:
    void stateChanged();
    // The track, its timeline (once per second while playing) or the state changed.
    void nowPlayingChanged();
};

} // namespace galleond
