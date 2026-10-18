// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GalleonPlugin.h"

#include "FakeController.h"
#include "FakeKeys.h"

#include <QLabel>

namespace galleon::openrgb {

namespace {

// Values of LL_ERROR and LL_INFO in OpenRGB's LogManager.h.
constexpr unsigned int LogError = 1;
constexpr unsigned int LogInfo = 3;

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
    info.Description = "Corsair Galleon 100 SD support (prototype: fake 12-LED device)";
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
    if (!api || m_controller)
        return;
    m_api = api;

    RGBController_Setup setup = makeFakeSetup(this);
    setup.DeviceUpdateLEDs = updateLedsCallback;

    m_controller = m_api->CreateVirtualRGBController(&setup);
    if (!m_controller) {
        log(LogError, "cannot create the fake controller");
        return;
    }
    // Registering from the UI thread could deadlock, hence the thread variant.
    m_api->RegisterVirtualRGBControllerInThread(m_controller);
    log(LogInfo, "fake Galleon controller registered");
}

QWidget *GalleonPlugin::GetWidget()
{
    // Ownership passes to the host window, which reparents the widget.
    return new QLabel(QStringLiteral("GalleonCore prototype: fake 12-LED device"));
}

QMenu *GalleonPlugin::GetTrayMenu()
{
    return nullptr;
}

void GalleonPlugin::Unload()
{
    if (!m_api || !m_controller)
        return;
    m_api->UnregisterVirtualRGBController(m_controller);
    m_api->DeleteVirtualRGBController(m_controller);
    m_controller = nullptr;
    log(LogInfo, "fake Galleon controller removed");
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
    if (!m_controller)
        return;
    const RGBColor *colors = m_controller->GetColorsPointer();
    if (!colors)
        return;

    // Index 0..FakeLedCount-1 only: the buffer holds exactly that many colours.
    const RGBColor first = colors[0];
    const RGBColor last = colors[FakeLedCount - 1];
    const QString line = QString::asprintf("LEDs updated: key 1 = #%02x%02x%02x, key 12 = #%02x%02x%02x",
                                           RGBGetRValue(first), RGBGetGValue(first), RGBGetBValue(first),
                                           RGBGetRValue(last), RGBGetGValue(last), RGBGetBValue(last));
    log(LogInfo, line.toUtf8().constData());
}

void GalleonPlugin::log(unsigned int level, const char *message)
{
    if (m_api)
        m_api->LogEntry(__FILE__, __LINE__, level, "[GalleonCore] %s", message);
}

} // namespace galleon::openrgb
