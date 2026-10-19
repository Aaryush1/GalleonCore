// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// A Galleon LampArray interface, driven with libusb control transfers.
//
// Why not hidraw: on Linux, usbhid refuses these interfaces (no interrupt
// endpoint), so no /dev/hidraw node exists (docs/protocol/README.md).
//
// Reading is harmless: GET requests, plus per lamp the LampAttributesRequest
// that only selects which lamp the next answer describes. Writing is separate
// and explicit (setAutonomous, writeColors): standard LampArray reports,
// volatile, nothing is saved to flash.

#include "LampArray.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace galleon::openrgb {

struct LampArrayData
{
    ReportIds ids;
    Attributes attributes;
    std::vector<Lamp> lamps; // lamps[i].id == i
};

class LampArrayDevice
{
public:
    // Opens the device, claims the interface (never detaches a kernel driver)
    // and reads the report IDs and the attributes. Null on failure, with the
    // reason in `error`.
    [[nodiscard]] static std::unique_ptr<LampArrayDevice> open(std::uint16_t vendorId,
                                                                std::uint16_t productId,
                                                                int interfaceNumber,
                                                                std::string &error);
    ~LampArrayDevice();
    LampArrayDevice(const LampArrayDevice &) = delete;
    LampArrayDevice &operator=(const LampArrayDevice &) = delete;

    [[nodiscard]] const ReportIds &ids() const;
    [[nodiscard]] const Attributes &attributes() const;

    // Reads every lamp's attributes.
    [[nodiscard]] bool readLamps(std::vector<Lamp> &lamps, std::string &error);

    // false = the host drives the lamps; true = the keyboard's own lighting.
    [[nodiscard]] bool setAutonomous(bool autonomous, std::string &error);

    // Sends the colours in batches of MaxLampsPerUpdate, the last batch
    // flagged "complete". Waits for the device's minimum update interval
    // between reports. setAutonomous(false) must have been called before.
    [[nodiscard]] bool writeColors(std::span<const LampColor> colors, std::string &error);

private:
    struct Impl;
    explicit LampArrayDevice(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace galleon::openrgb
