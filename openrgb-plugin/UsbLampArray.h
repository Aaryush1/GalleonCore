// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Reads a Galleon LampArray interface with libusb control transfers.
//
// Why not hidraw: on Linux, usbhid refuses these interfaces (no interrupt
// endpoint), so no /dev/hidraw node exists (docs/protocol/README.md).
//
// READ-ONLY with respect to lighting: the only things sent are standard GET
// requests and, per lamp, the LampAttributesRequest that merely selects which
// lamp the next answer describes. No lamp update, range update or control
// report is ever sent.

#include "LampArray.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace galleon::openrgb {

struct LampArrayData
{
    ReportIds ids;
    Attributes attributes;
    std::vector<Lamp> lamps; // lamps[i].id == i
};

struct ReadResult
{
    std::optional<LampArrayData> data;
    std::string error; // set when data is empty
};

[[nodiscard]] ReadResult readLampArray(std::uint16_t vendorId, std::uint16_t productId, int interfaceNumber);

} // namespace galleon::openrgb
