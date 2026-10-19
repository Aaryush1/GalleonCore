// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

// Pure protocol layer: bytes in, events out; images in, reports out.
// No hidapi, no Qt, no I/O. Everything here is unit-tested and fuzzed.
//
// Protocol facts must come from node-elgato-stream-deck, galleon-deck's
// docs/PROTOCOL.md or real captures in docs/protocol/. Never guess bytes.

#include <array>
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

// What the module last said about its keys and dial buttons. The module sends
// the complete state each time, so events are the differences with this.
struct InputState
{
    std::array<bool, KeyCount> keys{};
    std::array<bool, DialCount> dialButtons{};
};

enum class InputStatus : std::uint8_t {
    Ok,       // a key or dial report, understood (it may carry no change)
    Ignored,  // not a key or dial report: another report ID, or a type we do not use
    Invalid,  // it claims to be a key or dial report but is malformed: dropped
};

struct InputResult
{
    InputStatus status = InputStatus::Ignored;
    std::vector<InputEvent> events;
};

// Input report layout (interface 0, report ID 1, 512 bytes with the ID).
// Source: galleon-deck docs/PROTOCOL.md ("Input reports"), the same layout as
// the Stream Deck Plus in node-elgato-stream-deck and rust-elgato-streamdeck:
//   keys   01 00 <count u16le = 12> <12 bytes, 1 = pressed, 0 = released>
//   dials  01 03 <count u16le = 3>  <kind u8> <dial 0> <dial 1>
//          kind 0 = button states (1 = pressed), kind 1 = rotation (signed
//          steps since the last report)
// The order of the 12 key bytes (0 = top left, row by row) and the dial
// numbering are the sources' statement, verified on the hardware.
//
// Input comes from a device and is not trusted: the length and the declared
// counts must fit, key and button bytes must be 0 or 1, and a rotation of more
// than MaxDialSteps in one report is not plausible. Anything else is Invalid
// and changes nothing, `state` included.
inline constexpr int MaxDialSteps = 32;

InputResult decodeInputReport(std::span<const std::uint8_t> report, InputState &state);

// Same, for callers that keep no state: events relative to "nothing pressed".
// Malformed or unknown reports yield no event; never reads out of bounds.
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
