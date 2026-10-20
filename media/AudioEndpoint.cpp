// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AudioEndpoint.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace galleond {

void changeEndpointVolume(AudioEndpoint &endpoint, int percent)
{
    if (percent == 0)
        return;
    const float current = endpoint.volume();
    if (!std::isfinite(current) || current < 0.0F || current > 1.0F)
        throw std::runtime_error("Invalid audio endpoint volume");
    const double target = std::clamp(static_cast<double>(current) + static_cast<double>(percent) / 100.0, 0.0, 1.0);
    endpoint.setVolume(static_cast<float>(target));
    // Changing the level preserves mute; only the explicit mute action toggles it.
}

void toggleEndpointMute(AudioEndpoint &endpoint)
{
    endpoint.setMuted(!endpoint.muted());
}

} // namespace galleond
