// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

// Pure protocol layer: bytes in, events out; images in, reports out.
// No hidapi, no Qt, no I/O. Everything here is unit-tested and fuzzed.
//
// Protocol facts must come from node-elgato-stream-deck, galleon-deck's
// docs/PROTOCOL.md or real captures in docs/protocol/. Never guess bytes.

#include <cstdint>
#include <span>
#include <vector>

namespace galleon::protocol {

// USB IDs of the Galleon 100 SD (see docs/protocol/README.md).
inline constexpr std::uint16_t CorsairVendorId = 0x1b1c;
inline constexpr std::uint16_t StreamDeckProductId = 0x2b18;
inline constexpr std::uint16_t KeyboardProductId = 0x2b0c;

inline constexpr int KeyCount = 12;
inline constexpr int DialCount = 2;

// The deck stays in host mode only while a keep-alive feature report is sent
// at this interval (source: galleon-deck, README "How it works").
inline constexpr int KeepAliveIntervalMs = 500;

struct InputEvent
{
    enum class Type : std::uint8_t {
        KeyPressed,
        KeyReleased,
        DialRotated,
        DialPressed,
        DialReleased,
    };

    Type type;
    int index; // key 0..KeyCount-1 or dial 0..DialCount-1
    int value; // dial steps for DialRotated, 0 otherwise

    friend bool operator==(const InputEvent &, const InputEvent &) = default;
};

// Decode one input report read from the deck.
// Malformed or unknown reports yield no event: this function must never
// read out of bounds, whatever the input.
std::vector<InputEvent> parseInputReport(std::span<const std::uint8_t> report);

// Feature report that keeps the deck in host mode.
// Empty until the bytes are confirmed from a source; the transport refuses to
// send empty reports.
std::vector<std::uint8_t> keepAliveReport();

} // namespace galleon::protocol
