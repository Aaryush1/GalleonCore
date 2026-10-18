// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GalleonPlugin.h"

#include "LampArrayController.h"
#include "UsbLampArray.h"

#include <QLabel>

#include <string>

namespace galleon::openrgb {

namespace {

// Values of LL_ERROR and LL_INFO in OpenRGB's LogManager.h.
constexpr unsigned int LogError = 1;
constexpr unsigned int LogInfo = 3;

struct Part
{
    std::uint16_t productId;
    int interfaceNumber;
    const char *name;
    const char *location;
};

// USB parts and their LampArray interface (docs/protocol/README.md).
constexpr Part Parts[] = {
    {0x2b0c, 4, "Galleon 100 SD keyboard", "galleoncore:usb:1b1c:2b0c:4"},
    {0x2b18, 3, "Galleon 100 SD dials", "galleoncore:usb:1b1c:2b18:3"},
};
constexpr std::uint16_t CorsairVendorId = 0x1b1c;

// The host hands the owner pointer back as a bare void*: check it before use.
void updateLedsCallback(void *owner)
{
    if (!owner)
        return;
    static_cast<GalleonPlugin *>(owner)->onUpdateLeds();
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
    if (!api || !m_controllers.empty())
        return;
    m_api = api;

    for (const Part &part : Parts) {
        const ReadResult result = readLampArray(CorsairVendorId, part.productId, part.interfaceNumber);
        if (!result.data) {
            log(LogError, (std::string(part.name) + ": " + result.error).c_str());
            continue;
        }

        RGBController_Setup setup = makeLampArraySetup(*result.data, part.name, part.location, this);
        setup.DeviceUpdateLEDs = updateLedsCallback;

        RGBControllerInterface *controller = m_api->CreateVirtualRGBController(&setup);
        if (!controller) {
            log(LogError, (std::string(part.name) + ": cannot create the controller").c_str());
            continue;
        }
        // Registering from the UI thread could deadlock, hence the thread variant.
        m_api->RegisterVirtualRGBControllerInThread(controller);
        m_controllers.push_back(controller);
        log(LogInfo, (std::string(part.name) + ": " + std::to_string(result.data->lamps.size()) + " lamps registered").c_str());
    }
}

QWidget *GalleonPlugin::GetWidget()
{
    // Ownership passes to the host window, which reparents the widget.
    return new QLabel(QStringLiteral("GalleonCore prototype: read-only, colours are not sent to the keyboard"));
}

QMenu *GalleonPlugin::GetTrayMenu()
{
    return nullptr;
}

void GalleonPlugin::Unload()
{
    if (!m_api)
        return;
    for (RGBControllerInterface *controller : m_controllers) {
        m_api->UnregisterVirtualRGBController(controller);
        m_api->DeleteVirtualRGBController(controller);
    }
    m_controllers.clear();
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

void GalleonPlugin::onUpdateLeds()
{
    // Read-only: nothing is sent. Say so once, not at every effect frame.
    if (m_loggedFirstUpdate)
        return;
    m_loggedFirstUpdate = true;
    log(LogInfo, "colours received but NOT sent to the hardware (read-only prototype)");
}

void GalleonPlugin::log(unsigned int level, const char *message)
{
    if (m_api)
        m_api->LogEntry(__FILE__, __LINE__, level, "[GalleonCore] %s", message);
}

} // namespace galleon::openrgb
