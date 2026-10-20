// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

namespace galleond {

// A single output device. Native implementations and tests share the same
// volume/mute policy; methods throw on failure rather than guessing a value.
class AudioEndpoint
{
public:
    virtual ~AudioEndpoint() = default;
    [[nodiscard]] virtual float volume() = 0;
    virtual void setVolume(float scalar) = 0;
    [[nodiscard]] virtual bool muted() = 0;
    virtual void setMuted(bool muted) = 0;
};

void changeEndpointVolume(AudioEndpoint &endpoint, int percent);
void toggleEndpointMute(AudioEndpoint &endpoint);

// Called on the initialized COM worker for each command, so a default output
// switch or device removal does not leave a cached endpoint behind.
[[nodiscard]] std::unique_ptr<AudioEndpoint> createDefaultAudioEndpoint();

} // namespace galleond
