// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <span>

#include <QString>

namespace galleon {

// Raw I/O with the deck. HidTransport talks to the hardware; tests use a fake.
class Transport
{
public:
    virtual ~Transport() = default;

    virtual bool open() = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;
    [[nodiscard]] virtual QString serialNumber() const = 0;

    virtual bool sendFeatureReport(std::span<const std::uint8_t> report) = 0;
    virtual bool write(std::span<const std::uint8_t> report) = 0;

    // Non-blocking. Returns the number of bytes read, 0 if nothing is
    // pending, or -1 if the device is gone.
    virtual int read(std::span<std::uint8_t> buffer) = 0;
};

} // namespace galleon
