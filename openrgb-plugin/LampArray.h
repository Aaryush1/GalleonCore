// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Pure parsing of the standard HID LampArray reports (usage page 0x59) of the
// Galleon 100 SD. No libusb, no Qt, no OpenRGB: bytes in, values out.
//
// Layouts come from the devices' own report descriptors, read on 2026-10-18
// (see docs/protocol/README.md). All input is untrusted: every function checks
// lengths and values and returns an error instead of reading further.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace galleon::openrgb {

// A lamp array has at most this many lamps for us (the keyboard has 108).
inline constexpr unsigned int MaxLamps = 1024;
inline constexpr std::size_t MaxDescriptorSize = 4096;

// Feature report sizes, report ID byte included.
inline constexpr std::size_t AttributesReportSize = 23;
inline constexpr std::size_t LampRequestReportSize = 3;
inline constexpr std::size_t LampResponseReportSize = 29;
inline constexpr std::size_t ControlReportSize = 2;
inline constexpr std::size_t MultiUpdateReportSize = 51;
inline constexpr std::size_t MaxLampsPerUpdate = 8;

struct ReportIds
{
    std::uint8_t attributes = 0; // LampArrayAttributesReport
    std::uint8_t request = 0;    // LampAttributesRequestReport (selects a lamp)
    std::uint8_t response = 0;   // LampAttributesResponseReport
    std::uint8_t multiUpdate = 0; // LampMultiUpdateReport (up to 8 lamps)
    std::uint8_t control = 0;    // LampArrayControlReport (autonomous mode)
};

struct Attributes
{
    unsigned int lampCount = 0;
    std::uint32_t widthUm = 0;
    std::uint32_t heightUm = 0;
    std::uint32_t depthUm = 0;
    std::uint32_t kind = 0;
    std::uint32_t minUpdateIntervalUs = 0;
};

struct Lamp
{
    unsigned int id = 0;
    std::uint32_t xUm = 0;
    std::uint32_t yUm = 0;
    std::uint32_t zUm = 0;
    std::uint32_t latencyUs = 0;
    std::uint32_t purposes = 0;
    std::uint8_t redLevels = 0;
    std::uint8_t greenLevels = 0;
    std::uint8_t blueLevels = 0;
    std::uint8_t intensityLevels = 0;
    bool programmable = false;
    std::uint8_t inputBinding = 0;
};

// Finds the report IDs of the five reports we use by scanning the descriptor
// for `Report ID (n)  Usage (u)  Collection (Logical)`, with u = 0x02
// (attributes), 0x20 (request), 0x22 (response), 0x50 (multi-update) and
// 0x70 (control). Empty if any is missing,
// zero or duplicated, or if the descriptor is empty or too large.
[[nodiscard]] std::optional<ReportIds> findReportIds(std::span<const std::uint8_t> descriptor);

// Parses the answer to a GET_REPORT of the attributes report.
//
// The ID byte of the answer is NOT checked: the keyboard part answers with
// another ID (04 or 06 when asked for 1) and a valid payload, measured on
// firmware v1.4.73 and v2.3.88 and recorded in docs/protocol/README.md. The length is checked
// exactly instead, and the values must be plausible.
[[nodiscard]] std::optional<Attributes> parseAttributes(std::span<const std::uint8_t> report);

// The SET_REPORT payload that selects which lamp the next response describes.
[[nodiscard]] std::array<std::uint8_t, LampRequestReportSize> makeLampRequest(std::uint8_t requestId,
                                                                              unsigned int lampId);

// Parses a lamp response. Fails if the length is wrong or if the answer is for
// another lamp than `expectedId` (the sequence must stay in step). The ID byte
// is not checked, for the same reason as above.
[[nodiscard]] std::optional<Lamp> parseLampResponse(std::span<const std::uint8_t> report, unsigned int expectedId);

struct LampColor
{
    unsigned int id = 0;
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

// LampArrayControlReport: autonomous = true gives the lighting back to the
// keyboard; false makes the host the only one to drive the lamps.
[[nodiscard]] std::array<std::uint8_t, ControlReportSize> makeControlReport(std::uint8_t controlId, bool autonomous);

// LampMultiUpdateReport for 1 to MaxLampsPerUpdate lamps (more or none: empty
// result). `complete` sets the LampUpdateComplete flag, which tells the device
// to apply the batch: set it on the last report of a frame only. Lamp IDs above
// MaxLamps are refused. The intensity channel is always 255.
[[nodiscard]] std::optional<std::array<std::uint8_t, MultiUpdateReportSize>>
makeMultiUpdateReport(std::uint8_t multiUpdateId, std::span<const LampColor> lamps, bool complete);

} // namespace galleon::openrgb
