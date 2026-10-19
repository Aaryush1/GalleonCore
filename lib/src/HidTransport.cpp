// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "galleon/HidTransport.h"

#include <hidapi/hidapi.h>

#include <QDebug>

#include <array>

namespace galleon {

struct HidTransport::Device
{
    hid_device *handle = nullptr;

    // hid_exit() is never called: it would invalidate devices opened by
    // other transports. hidapi releases its state at process exit.
    Device() { hid_init(); }
    ~Device()
    {
        if (handle)
            hid_close(handle);
    }

    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;
};

HidTransport::HidTransport(std::uint16_t vendorId, std::uint16_t productId, int interfaceNumber)
    : m_vendorId(vendorId)
    , m_productId(productId)
    , m_interfaceNumber(interfaceNumber)
{
}

HidTransport::~HidTransport() = default;

bool HidTransport::open()
{
    if (isOpen())
        return true;

    auto device = std::make_unique<Device>();
    // hid_open(vid, pid) would take the first interface found, which for the
    // Stream Deck module may be the numpad or the dial keys: pick by number.
    hid_device_info *devices = hid_enumerate(m_vendorId, m_productId);
    for (const hid_device_info *info = devices; info && !device->handle; info = info->next) {
        if (m_interfaceNumber < 0 || info->interface_number == m_interfaceNumber)
            device->handle = hid_open_path(info->path);
    }
    hid_free_enumeration(devices);
    if (!device->handle) {
        qWarning() << "galleon: cannot open device"
                   << QString::fromWCharArray(hid_error(nullptr));
        return false;
    }
    hid_set_nonblocking(device->handle, 1);
    m_device = std::move(device);
    return true;
}

void HidTransport::close()
{
    m_device.reset();
}

bool HidTransport::isOpen() const
{
    return m_device && m_device->handle;
}

QString HidTransport::serialNumber() const
{
    if (!isOpen())
        return {};
    std::array<wchar_t, 128> buffer{};
    if (hid_get_serial_number_string(m_device->handle, buffer.data(), buffer.size()) != 0)
        return {};
    return QString::fromWCharArray(buffer.data());
}

bool HidTransport::sendFeatureReport(std::span<const std::uint8_t> report)
{
    // An empty report means the protocol layer does not know the bytes yet.
    if (!isOpen() || report.empty())
        return false;
    return hid_send_feature_report(m_device->handle, report.data(), report.size()) >= 0;
}

bool HidTransport::write(std::span<const std::uint8_t> report)
{
    if (!isOpen() || report.empty())
        return false;
    return hid_write(m_device->handle, report.data(), report.size()) >= 0;
}

int HidTransport::read(std::span<std::uint8_t> buffer)
{
    if (!isOpen())
        return -1;
    return hid_read(m_device->handle, buffer.data(), buffer.size());
}

} // namespace galleon
