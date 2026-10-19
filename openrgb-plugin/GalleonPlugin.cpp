// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GalleonPlugin.h"

#include "LampArrayController.h"
#include "UsbLampArray.h"

#include <QLabel>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

namespace galleon::openrgb {

namespace {

// Values of LL_ERROR and LL_INFO in OpenRGB's LogManager.h.
constexpr unsigned int LogError = 1;
constexpr unsigned int LogInfo = 3;

struct PartInfo
{
    std::uint16_t productId;
    int interfaceNumber;
    const char *name;
    const char *location;
};

// USB parts and their LampArray interface (docs/protocol/README.md).
constexpr PartInfo Parts[] = {
    {0x2b0c, 4, "Galleon 100 SD keyboard", "galleoncore:usb:1b1c:2b0c:4"},
    {0x2b18, 3, "Galleon 100 SD dials", "galleoncore:usb:1b1c:2b18:3"},
};
constexpr std::uint16_t CorsairVendorId = 0x1b1c;

// The host hands the owner pointer back as a bare void*: check it before use.
void updateLedsCallback(void *owner)
{
    if (!owner)
        return;
    auto *part = static_cast<GalleonPlugin::Part *>(owner);
    if (part->plugin)
        part->plugin->onUpdateLeds(*part);
}

bool writesRequested()
{
    const char *value = std::getenv("GALLEONCORE_WRITE");
    return value && std::string(value) == "1";
}

} // namespace

OpenRGBPluginInfo GalleonPlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;
    info.Name = "GalleonCore";
    info.Description = "Corsair Galleon 100 SD LEDs (read-only prototype)";
    info.Version = "0.1.0";
    info.Commit = "unknown";
    info.URL = "https://github.com/Paullux/GalleonCore";
    info.Location = OPENRGB_PLUGIN_LOCATION_TOP;
    info.Label = "Galleon";
    info.ProtocolVersion = 0;
    return info;
}

unsigned int GalleonPlugin::GetPluginAPIVersion()
{
    return OPENRGB_PLUGIN_API_VERSION;
}

void GalleonPlugin::Load(OpenRGBPluginAPIInterface *api)
{
    if (!api || !m_parts.empty())
        return;
    m_api = api;
    m_writesEnabled = writesRequested();
    log(LogInfo,
        m_writesEnabled ? "WRITES ENABLED (GALLEONCORE_WRITE=1): colours are sent to the hardware"
                        : "read-only: set GALLEONCORE_WRITE=1 to send colours to the hardware");

    for (const PartInfo &info : Parts) {
        std::string error;
        auto device = LampArrayDevice::open(CorsairVendorId, info.productId, info.interfaceNumber, error);
        LampArrayData data;
        if (device) {
            data.ids = device->ids();
            data.attributes = device->attributes();
            if (!device->readLamps(data.lamps, error))
                device.reset();
        }
        if (!device) {
            log(LogError, (std::string(info.name) + ": " + error).c_str());
            continue;
        }

        auto part = std::make_unique<Part>();
        part->plugin = this;
        part->name = info.name;
        part->device = std::move(device);

        RGBController_Setup setup = makeLampArraySetup(data, info.name, info.location, part.get());
        setup.DeviceUpdateLEDs = updateLedsCallback;

        part->controller = m_api->CreateVirtualRGBController(&setup);
        if (!part->controller) {
            log(LogError, (std::string(info.name) + ": cannot create the controller").c_str());
            continue;
        }
        // Registering from the UI thread could deadlock, hence the thread variant.
        m_api->RegisterVirtualRGBControllerInThread(part->controller);
        log(LogInfo, (std::string(info.name) + ": " + std::to_string(data.lamps.size()) + " lamps registered").c_str());
        m_parts.push_back(std::move(part));
    }
}

QWidget *GalleonPlugin::GetWidget()
{
    // Ownership passes to the host window, which reparents the widget.
    return new QLabel(m_writesEnabled ? QStringLiteral("GalleonCore prototype: writes enabled") : QStringLiteral("GalleonCore prototype: read-only (set GALLEONCORE_WRITE=1 to enable writes)"));
}

QMenu *GalleonPlugin::GetTrayMenu()
{
    return nullptr;
}

void GalleonPlugin::Unload()
{
    if (!m_api)
        return;
    for (auto &part : m_parts) {
        RGBControllerInterface *controller = nullptr;
        {
            // Give the lighting back first, then drop the device: from now on
            // the callbacks find no device and do nothing.
            const std::lock_guard lock(part->mutex);
            if (part->device && part->hostControl) {
                std::string error;
                if (!part->device->setAutonomous(true, error))
                    log(LogError, (std::string(part->name) + ": " + error).c_str());
            }
            part->hostControl = false;
            part->device.reset();
            controller = std::exchange(part->controller, nullptr);
        }
        // Unregister only. The controller is deliberately NOT deleted: the
        // host's device pages still point to it until the UI has processed the
        // device-list update, and deleting it crashed OpenRGB on exit (segfault
        // in OpenRGBDevicePage's destructor, observed 2026-10-18). The cost is
        // one small object left behind per part and per Unload.
        if (controller)
            m_api->UnregisterVirtualRGBController(controller);
    }
    log(LogInfo, "controllers removed");
}

void GalleonPlugin::OnProfileAboutToLoad() {}

void GalleonPlugin::OnProfileLoad(nlohmann::json) {}

nlohmann::json GalleonPlugin::OnProfileSave()
{
    return nlohmann::json();
}

unsigned char *GalleonPlugin::OnSDKCommand(unsigned int, unsigned char *, unsigned int *)
{
    return nullptr;
}

void GalleonPlugin::ProfileManagerUpdated(unsigned int) {}

void GalleonPlugin::ResourceManagerUpdated(unsigned int) {}

void GalleonPlugin::SettingsManagerUpdated(unsigned int) {}

void GalleonPlugin::onUpdateLeds(Part &part)
{
    if (!m_writesEnabled) {
        if (!m_loggedReadOnly.exchange(true)) {
            log(LogInfo, "colours received but NOT sent: read-only (GALLEONCORE_WRITE=1 enables writes)");
        }
        return;
    }

    const std::lock_guard lock(part.mutex);
    if (!part.device || !part.controller || part.failed)
        return;
    const RGBColor *colors = part.controller->GetColorsPointer();
    const unsigned int count = std::min(part.controller->GetLEDCount(), part.device->attributes().lampCount);
    if (!colors || count == 0)
        return;

    // LED i is lamp i. Only the lamps whose colour changed are sent.
    if (!part.lastSentValid)
        part.lastSent.assign(count, LampColor{});
    std::vector<LampColor> changed;
    for (unsigned int i = 0; i < count; ++i) {
        const LampColor now{i, RGBGetRValue(colors[i]), RGBGetGValue(colors[i]), RGBGetBValue(colors[i])};
        const LampColor &before = part.lastSent[i];
        if (!part.lastSentValid || now.red != before.red || now.green != before.green || now.blue != before.blue)
            changed.push_back(now);
    }
    if (changed.empty())
        return;

    std::string error;
    if (!part.hostControl) {
        if (!part.device->setAutonomous(false, error)) {
            part.failed = true;
            log(LogError, (std::string(part.name) + ": " + error + " (writes stopped)").c_str());
            return;
        }
        part.hostControl = true;
        log(LogInfo, (std::string(part.name) + ": host control on").c_str());
    }
    if (!part.device->writeColors(changed, error)) {
        part.failed = true;
        log(LogError, (std::string(part.name) + ": " + error + " (writes stopped)").c_str());
        std::string ignored;
        if (part.device->setAutonomous(true, ignored))
            part.hostControl = false;
        return;
    }
    for (const LampColor &lamp : changed)
        part.lastSent[lamp.id] = lamp;
    part.lastSentValid = true;
}

void GalleonPlugin::log(unsigned int level, const char *message)
{
    if (m_api)
        m_api->LogEntry(__FILE__, __LINE__, level, "[GalleonCore] %s", message);
}

} // namespace galleon::openrgb
