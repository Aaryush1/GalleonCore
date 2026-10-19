// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UsbLampArray.h"

#include <libusb-1.0/libusb.h>

#include <algorithm>
#include <array>
#include <chrono>
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

std::string usbError(const char *what, int code)
{
    return std::string(what) + ": " + libusb_error_name(code);
}

} // namespace

struct LampArrayDevice::Impl
{
    // Members are destroyed in reverse order: release the interface, close the
    // handle, then exit libusb.
    std::unique_ptr<libusb_context, ContextDeleter> context;
    std::unique_ptr<libusb_device_handle, HandleDeleter> handle;
    int interfaceNumber = 0;
    bool claimed = false;
    ReportIds ids;
    Attributes attributes;
    std::chrono::steady_clock::time_point lastSet{};

    ~Impl()
    {
        if (claimed && handle)
            libusb_release_interface(handle.get(), interfaceNumber);
    }

    // Returns the number of bytes read, or a negative libusb error.
    int getFeature(std::uint8_t id, std::span<std::uint8_t> out) const
    {
        return libusb_control_transfer(handle.get(),
                                       DeviceToHostClassInterface,
                                       RequestGetReport,
                                       static_cast<std::uint16_t>((ReportTypeFeature << 8) | id),
                                       static_cast<std::uint16_t>(interfaceNumber),
                                       out.data(),
                                       static_cast<std::uint16_t>(out.size()),
                                       TimeoutMs);
    }

    // Waits so that two SET requests are never closer than the device's
    // declared minimum update interval.
    void pace()
    {
        const auto interval = std::chrono::microseconds(attributes.minUpdateIntervalUs);
        const auto next = lastSet + interval;
        const auto now = std::chrono::steady_clock::now();
        if (next > now)
            std::this_thread::sleep_for(next - now);
    }

    int setFeature(std::span<const std::uint8_t> report)
    {
        // libusb wants a non-const buffer, even for an OUT transfer.
        std::array<std::uint8_t, MultiUpdateReportSize> copy{};
        if (report.empty() || report.size() > copy.size())
            return LIBUSB_ERROR_INVALID_PARAM;
        std::copy(report.begin(), report.end(), copy.begin());
        pace();
        const int r = libusb_control_transfer(handle.get(),
                                              HostToDeviceClassInterface,
                                              RequestSetReport,
                                              static_cast<std::uint16_t>((ReportTypeFeature << 8) | report[0]),
                                              static_cast<std::uint16_t>(interfaceNumber),
                                              copy.data(),
                                              static_cast<std::uint16_t>(report.size()),
                                              TimeoutMs);
        lastSet = std::chrono::steady_clock::now();
        return r;
    }
};

LampArrayDevice::LampArrayDevice(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

LampArrayDevice::~LampArrayDevice() = default;

const ReportIds &LampArrayDevice::ids() const
{
    return m_impl->ids;
}

const Attributes &LampArrayDevice::attributes() const
{
    return m_impl->attributes;
}

std::unique_ptr<LampArrayDevice> LampArrayDevice::open(std::uint16_t vendorId,
                                                        std::uint16_t productId,
                                                        int interfaceNumber,
                                                        std::string &error)
{
    auto impl = std::make_unique<Impl>();
    impl->interfaceNumber = interfaceNumber;

    libusb_context *rawContext = nullptr;
    if (const int r = libusb_init(&rawContext); r != 0) {
        error = usbError("libusb_init", r);
        return nullptr;
    }
    impl->context.reset(rawContext);

    impl->handle.reset(libusb_open_device_with_vid_pid(impl->context.get(), vendorId, productId));
    if (!impl->handle) {
        error = "device not found or not accessible (udev rule installed?)";
        return nullptr;
    }

    // Never detach a kernel driver: if one is bound, something else owns the interface.
    if (libusb_kernel_driver_active(impl->handle.get(), interfaceNumber) == 1) {
        error = "a kernel driver owns the interface";
        return nullptr;
    }
    if (const int r = libusb_claim_interface(impl->handle.get(), interfaceNumber); r != 0) {
        error = usbError("claim interface", r);
        return nullptr;
    }
    impl->claimed = true;

    std::array<std::uint8_t, MaxDescriptorSize> descriptor{};
    const int descriptorSize = libusb_control_transfer(impl->handle.get(),
                                                       DeviceToHostStandardInterface,
                                                       RequestGetDescriptor,
                                                       static_cast<std::uint16_t>(DescriptorTypeReport << 8),
                                                       static_cast<std::uint16_t>(interfaceNumber),
                                                       descriptor.data(),
                                                       static_cast<std::uint16_t>(descriptor.size()),
                                                       TimeoutMs);
    if (descriptorSize <= 0) {
        error = usbError("read report descriptor", descriptorSize);
        return nullptr;
    }

    const auto ids = findReportIds(std::span<const std::uint8_t>(descriptor).first(static_cast<std::size_t>(descriptorSize)));
    if (!ids) {
        error = "no LampArray reports in the descriptor";
        return nullptr;
    }
    impl->ids = *ids;

    std::array<std::uint8_t, AttributesReportSize> attributesBuffer{};
    const int attributesRead = impl->getFeature(impl->ids.attributes, attributesBuffer);
    if (attributesRead < 0) {
        error = usbError("get attributes", attributesRead);
        return nullptr;
    }
    const auto attributes
        = parseAttributes(std::span<const std::uint8_t>(attributesBuffer).first(static_cast<std::size_t>(attributesRead)));
    if (!attributes) {
        error = "invalid attributes report";
        return nullptr;
    }
    impl->attributes = *attributes;

    return std::unique_ptr<LampArrayDevice>(new LampArrayDevice(std::move(impl)));
}

bool LampArrayDevice::readLamps(std::vector<Lamp> &lamps, std::string &error)
{
    lamps.clear();
    lamps.reserve(m_impl->attributes.lampCount);
    for (unsigned int lampId = 0; lampId < m_impl->attributes.lampCount; ++lampId) {
        const auto request = makeLampRequest(m_impl->ids.request, lampId);
        if (const int r = m_impl->setFeature(request); r < 0) {
            error = usbError("select lamp", r);
            return false;
        }

        std::array<std::uint8_t, LampResponseReportSize> responseBuffer{};
        const int responseRead = m_impl->getFeature(m_impl->ids.response, responseBuffer);
        if (responseRead < 0) {
            error = usbError("get lamp", responseRead);
            return false;
        }
        const auto lamp
            = parseLampResponse(std::span<const std::uint8_t>(responseBuffer).first(static_cast<std::size_t>(responseRead)),
                                lampId);
        if (!lamp) {
            error = "invalid answer for lamp " + std::to_string(lampId);
            return false;
        }
        lamps.push_back(*lamp);
        std::this_thread::sleep_for(PacingBetweenLamps);
    }
    return true;
}

bool LampArrayDevice::setAutonomous(bool autonomous, std::string &error)
{
    const auto report = makeControlReport(m_impl->ids.control, autonomous);
    if (const int r = m_impl->setFeature(report); r < 0) {
        error = usbError("set autonomous mode", r);
        return false;
    }
    return true;
}

bool LampArrayDevice::writeColors(std::span<const LampColor> colors, std::string &error)
{
    for (std::size_t first = 0; first < colors.size(); first += MaxLampsPerUpdate) {
        const std::size_t count = std::min(MaxLampsPerUpdate, colors.size() - first);
        const bool last = first + count >= colors.size();
        const auto report = makeMultiUpdateReport(m_impl->ids.multiUpdate, colors.subspan(first, count), last);
        if (!report) {
            error = "cannot encode the colour update";
            return false;
        }
        if (const int r = m_impl->setFeature(*report); r < 0) {
            error = usbError("update lamps", r);
            return false;
        }
    }
    return true;
}

} // namespace galleon::openrgb
