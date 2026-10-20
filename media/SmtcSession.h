// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "MediaController.h"

#include <cstdint>
#include <memory>

namespace galleond {

struct SmtcSnapshot
{
    MediaController::State state = MediaController::State::NoPlayer;
    NowPlaying track;
    bool shuffle = false;
    MediaController::Loop loop = MediaController::Loop::None;
    std::uint64_t session = 0;
};

enum class SmtcAction { PlayPause, Next, Previous, Stop, Seek, Shuffle, Repeat, Restart, Volume, Mute };
struct SmtcCommand
{
    SmtcAction action;
    int value = 0; // signed seconds for Seek, percentage points for Volume
    std::uint64_t session = 0;
};

// All methods run on one worker thread, including apartment setup and teardown.
// Implementations must bound external operations; no Qt UI objects live here.
class SmtcSession
{
public:
    virtual ~SmtcSession() = default;
    virtual void start() {}
    virtual void stop() noexcept {}
    [[nodiscard]] virtual SmtcSnapshot read() = 0;
    virtual void command(const SmtcCommand &command) = 0;
};

[[nodiscard]] std::unique_ptr<SmtcSession> createNativeSmtcSession();

} // namespace galleond
