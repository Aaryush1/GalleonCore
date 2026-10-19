// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

namespace galleond {

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

    using QObject::QObject;

    [[nodiscard]] virtual State state() const = 0;

    virtual void playPause() = 0;
    virtual void next() = 0;
    virtual void previous() = 0;
    virtual void stop() = 0;
    // `percent`: signed, in percent of the full volume.
    virtual void changeVolume(int percent) = 0;
    // `seconds`: signed offset from the current position.
    virtual void seek(int seconds) = 0;

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
};

} // namespace galleond
