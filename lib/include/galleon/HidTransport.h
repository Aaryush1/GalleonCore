// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "galleon/Transport.h"

#include <memory>

namespace galleon {

// Transport over hidapi (hidraw on Linux, native HID on Windows).
//
// A USB part exposes several HID interfaces (the Stream Deck module has four);
// `interfaceNumber` selects the one to open, or -1 for the first one found.
class HidTransport final : public Transport
{
public:
    HidTransport(std::uint16_t vendorId, std::uint16_t productId, int interfaceNumber = -1);
    ~HidTransport() override;

    HidTransport(const HidTransport &) = delete;
    HidTransport &operator=(const HidTransport &) = delete;

    bool open() override;
    void close() override;
    [[nodiscard]] bool isOpen() const override;
    [[nodiscard]] QString serialNumber() const override;

    bool sendFeatureReport(std::span<const std::uint8_t> report) override;
    bool write(std::span<const std::uint8_t> report) override;
    int read(std::span<std::uint8_t> buffer) override;

private:
    struct Device;
    std::uint16_t m_vendorId;
    std::uint16_t m_productId;
    int m_interfaceNumber;
    std::unique_ptr<Device> m_device;
};

} // namespace galleon
