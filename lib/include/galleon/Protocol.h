// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

// Pure protocol layer: bytes in, events out; images in, reports out.
// No hidapi, no Qt, no I/O. Everything here is unit-tested and fuzzed.
//
// Protocol facts must come from node-elgato-stream-deck, galleon-deck's
// docs/PROTOCOL.md or real captures in docs/protocol/. Never guess bytes.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace galleon::protocol {

// USB IDs of the Galleon 100 SD (see docs/protocol/README.md).
inline constexpr std::uint16_t CorsairVendorId = 0x1b1c;
inline constexpr std::uint16_t StreamDeckProductId = 0x2b18;
inline constexpr std::uint16_t KeyboardProductId = 0x2b0c;

// USB interface of the module that carries keys, screen and dials (measured
// 2026-10-18; rust-elgato-streamdeck PR 63 also opens only this one).
inline constexpr int ControlInterface = 0;

inline constexpr int KeyCount = 12;
inline constexpr int DialCount = 2;

// The deck stays in host mode only while a keep-alive feature report is sent
// at this interval (source: galleon-deck, README "How it works").
inline constexpr int KeepAliveIntervalMs = 500;

// Report layout of the module's control interface (interface 0, usage page
// 0x0C). Measured: the module's own HID report descriptor, read with
// HIDIOCGRDESC on 2026-10-18 (docs/protocol/README.md). The reports are
// numbered, and the sizes below include the report ID byte:
//   output  ID 2  : 1023 data bytes -> 1024 bytes (images)
//   input   ID 1  :  511 data bytes ->  512 bytes (keys, dials)
//   feature ID 3, 4, 5, ... : 31 data bytes -> 32 bytes
inline constexpr std::uint8_t OutputReportId = 0x02;
inline constexpr std::size_t OutputReportSize = 1024;
inline constexpr std::uint8_t FeatureReportId = 0x03;
inline constexpr std::size_t FeatureReportSize = 32;

// Image sizes. Measured: feature report 8 of the module (read 2026-10-18)
// ends with `d0 02 80 01` = 720 x 384 and holds `a0 00 a0 00` = 160 x 160.
// galleon-deck's docs/PROTOCOL.md gives the same sizes.
inline constexpr int ScreenWidth = 720;
inline constexpr int ScreenHeight = 384;
inline constexpr int KeyImageSize = 160;

// Limit on a JPEG we accept to cut into reports: a 720 x 384 frame is far
// below it, and the part counter is 16 bits.
inline constexpr std::size_t MaxJpegSize = 4 * 1024 * 1024;

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

// Feature report that keeps the deck in host mode: report ID 3 then 0x27,
// padded to 32 bytes. Source: galleon-deck docs/PROTOCOL.md ("03 27, every
// 500 ms"); the report ID 3 and the 32-byte size are confirmed by the
// module's descriptor.
std::vector<std::uint8_t> keepAliveReport();

// Feature report that sets the brightness of the module: report ID 3, 0x08,
// the percentage, padded to 32 bytes. Source: galleon-deck ("03 08 <0-100>").
// Empty if `percent` is outside 0..100.
std::vector<std::uint8_t> brightnessReport(int percent);

// Cuts a JPEG into the output reports that draw it on the top screen.
// Header of 16 bytes (galleon-deck, "Top screen output report"):
//   02 0c  x(u16le) y(u16le) w(u16le) h(u16le)  last(u8)  part(u16le)  len(u16le)  00
// then up to 1008 bytes of JPEG, the report zero-padded to 1024 bytes.
// The byte order of the fields, `last` = 1 on the final report and `part`
// counting from 0 follow node-elgato-stream-deck's conventions for the other
// models; they are verified on the hardware, not by the sources alone.
// Empty if the rectangle is not inside the 720 x 384 screen, or the JPEG is
// empty or larger than MaxJpegSize.
std::vector<std::vector<std::uint8_t>> screenImageReports(std::span<const std::uint8_t> jpeg,
                                                          int x, int y, int width, int height);

// Cuts a 160 x 160 JPEG into the output reports that draw it on a key (0..11).
// Header of 8 bytes (galleon-deck, "Key image output report"):
//   02 07  key(u8)  last(u8)  len(u16le)  part(u16le)
// then up to 1016 bytes of JPEG, zero-padded to 1024 bytes.
// Empty if the key is out of range, or the JPEG is empty or too large.
std::vector<std::vector<std::uint8_t>> keyImageReports(int key, std::span<const std::uint8_t> jpeg);

} // namespace galleon::protocol
