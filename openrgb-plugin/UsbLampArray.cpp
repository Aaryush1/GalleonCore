// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UsbLampArray.h"

#include <libusb-1.0/libusb.h>

#include <array>
#include <chrono>
#include <memory>
#include <thread>

namespace galleon::openrgb {

namespace {

constexpr unsigned int TimeoutMs = 1000;
constexpr auto PacingBetweenLamps = std::chrono::milliseconds(5);

// bmRequestType / bRequest values from the USB HID 1.11 specification.
constexpr std::uint8_t DeviceToHostClassInterface = 0xA1;
constexpr std::uint8_t HostToDeviceClassInterface = 0x21;
constexpr std::uint8_t DeviceToHostStandardInterface = 0x81;
constexpr std::uint8_t RequestGetReport = 0x01;
constexpr std::uint8_t RequestSetReport = 0x09;
constexpr std::uint8_t RequestGetDescriptor = 0x06;
constexpr std::uint16_t ReportTypeFeature = 3;
constexpr std::uint16_t DescriptorTypeReport = 0x22;

struct ContextDeleter
{
    void operator()(libusb_context *context) const { libusb_exit(context); }
};

struct HandleDeleter
{
    void operator()(libusb_device_handle *handle) const { libusb_close(handle); }
};

// Releases the interface when it goes out of scope.
class InterfaceClaim
{
public:
    InterfaceClaim(libusb_device_handle *handle, int number)
        : m_handle(handle)
        , m_number(number)
        , m_result(libusb_claim_interface(handle, number))
    {
    }
    ~InterfaceClaim()
    {
        if (m_result == 0)
            libusb_release_interface(m_handle, m_number);
    }
    InterfaceClaim(const InterfaceClaim &) = delete;
    InterfaceClaim &operator=(const InterfaceClaim &) = delete;

    [[nodiscard]] int result() const { return m_result; }

private:
    libusb_device_handle *m_handle;
    int m_number;
    int m_result;
};

std::string usbError(const char *what, int code)
{
    return std::string(what) + ": " + libusb_error_name(code);
}

// Returns the number of bytes read, or a negative libusb error.
int getFeature(libusb_device_handle *handle, int interfaceNumber, std::uint8_t id, std::span<std::uint8_t> out)
{
    return libusb_control_transfer(handle,
                                   DeviceToHostClassInterface,
                                   RequestGetReport,
                                   static_cast<std::uint16_t>((ReportTypeFeature << 8) | id),
                                   static_cast<std::uint16_t>(interfaceNumber),
                                   out.data(),
                                   static_cast<std::uint16_t>(out.size()),
                                   TimeoutMs);
}

int setFeature(libusb_device_handle *handle, int interfaceNumber, std::uint8_t id, std::span<const std::uint8_t> in)
{
    // libusb wants a non-const buffer, even for an OUT transfer.
    std::array<std::uint8_t, LampRequestReportSize> copy{};
    if (in.size() > copy.size())
        return LIBUSB_ERROR_INVALID_PARAM;
    std::copy(in.begin(), in.end(), copy.begin());
    return libusb_control_transfer(handle,
                                   HostToDeviceClassInterface,
                                   RequestSetReport,
                                   static_cast<std::uint16_t>((ReportTypeFeature << 8) | id),
                                   static_cast<std::uint16_t>(interfaceNumber),
                                   copy.data(),
                                   static_cast<std::uint16_t>(in.size()),
                                   TimeoutMs);
}

ReadResult fail(std::string message)
{
    return {std::nullopt, std::move(message)};
}

} // namespace

ReadResult readLampArray(std::uint16_t vendorId, std::uint16_t productId, int interfaceNumber)
{
    libusb_context *rawContext = nullptr;
    if (const int r = libusb_init(&rawContext); r != 0)
        return fail(usbError("libusb_init", r));
    const std::unique_ptr<libusb_context, ContextDeleter> context(rawContext);

    const std::unique_ptr<libusb_device_handle, HandleDeleter> handle(
        libusb_open_device_with_vid_pid(context.get(), vendorId, productId));
    if (!handle)
        return fail("device not found or not accessible (udev rule installed?)");

    // Never detach a kernel driver: if one is bound, something else owns the interface.
    if (libusb_kernel_driver_active(handle.get(), interfaceNumber) == 1)
        return fail("a kernel driver owns the interface");
    const InterfaceClaim claim(handle.get(), interfaceNumber);
    if (claim.result() != 0)
        return fail(usbError("claim interface", claim.result()));

    std::array<std::uint8_t, MaxDescriptorSize> descriptor{};
    const int descriptorSize = libusb_control_transfer(handle.get(),
                                                       DeviceToHostStandardInterface,
                                                       RequestGetDescriptor,
                                                       static_cast<std::uint16_t>(DescriptorTypeReport << 8),
                                                       static_cast<std::uint16_t>(interfaceNumber),
                                                       descriptor.data(),
                                                       static_cast<std::uint16_t>(descriptor.size()),
                                                       TimeoutMs);
    if (descriptorSize <= 0)
        return fail(usbError("read report descriptor", descriptorSize));

    LampArrayData data;
    const auto ids = findReportIds(std::span<const std::uint8_t>(descriptor).first(static_cast<std::size_t>(descriptorSize)));
    if (!ids)
        return fail("no LampArray reports in the descriptor");
    data.ids = *ids;

    std::array<std::uint8_t, AttributesReportSize> attributesBuffer{};
    const int attributesRead = getFeature(handle.get(), interfaceNumber, data.ids.attributes, attributesBuffer);
    if (attributesRead < 0)
        return fail(usbError("get attributes", attributesRead));
    const auto attributes
        = parseAttributes(std::span<const std::uint8_t>(attributesBuffer).first(static_cast<std::size_t>(attributesRead)));
    if (!attributes)
        return fail("invalid attributes report");
    data.attributes = *attributes;

    data.lamps.reserve(data.attributes.lampCount);
    for (unsigned int lampId = 0; lampId < data.attributes.lampCount; ++lampId) {
        const auto request = makeLampRequest(data.ids.request, lampId);
        if (const int r = setFeature(handle.get(), interfaceNumber, data.ids.request, request); r < 0)
            return fail(usbError("select lamp", r));

        std::array<std::uint8_t, LampResponseReportSize> responseBuffer{};
        const int responseRead = getFeature(handle.get(), interfaceNumber, data.ids.response, responseBuffer);
        if (responseRead < 0)
            return fail(usbError("get lamp", responseRead));
        const auto lamp
            = parseLampResponse(std::span<const std::uint8_t>(responseBuffer).first(static_cast<std::size_t>(responseRead)),
                                lampId);
        if (!lamp)
            return fail("invalid answer for lamp " + std::to_string(lampId));
        data.lamps.push_back(*lamp);
        std::this_thread::sleep_for(PacingBetweenLamps);
    }

    return {std::move(data), {}};
}

} // namespace galleon::openrgb
