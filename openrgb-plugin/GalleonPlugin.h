// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "OpenRGBPluginInterface.h"

#include "LampArray.h"
#include "UsbLampArray.h"

#include <QObject>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace galleon::openrgb {

// Reads the two LampArray interfaces of the Galleon 100 SD with libusb and
// registers one controller per part through the plugin API
// (RegisterVirtualRGBController).
//
// Writing colours to the hardware is OFF by default. Start OpenRGB with
// GALLEONCORE_WRITE=1 to enable it; the lighting is given back to the keyboard
// when the plugin unloads.
class GalleonPlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID FILE "galleon-plugin.json")
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    OpenRGBPluginInfo GetPluginInfo() override;
    unsigned int GetPluginAPIVersion() override;

    void Load(OpenRGBPluginAPIInterface *api) override;
    QWidget *GetWidget() override;
    QMenu *GetTrayMenu() override;
    void Unload() override;

    void OnProfileAboutToLoad() override;
    void OnProfileLoad(nlohmann::json profile) override;
    nlohmann::json OnProfileSave() override;
    unsigned char *OnSDKCommand(unsigned int id, unsigned char *data, unsigned int *size) override;

    void ProfileManagerUpdated(unsigned int reason) override;
    void ResourceManagerUpdated(unsigned int reason) override;
    void SettingsManagerUpdated(unsigned int reason) override;

    // State of one USB part. The controller callback gets a pointer to it.
    struct Part
    {
        GalleonPlugin *plugin = nullptr;
        std::mutex mutex; // the device thread and the UI thread both touch it
        std::unique_ptr<LampArrayDevice> device;
        RGBControllerInterface *controller = nullptr;
        std::vector<LampColor> lastSent;
        bool lastSentValid = false;
        bool hostControl = false;
        bool failed = false;
        const char *name = "";
    };

    // Called by the controller callback with the part it was set up with.
    void onUpdateLeds(Part &part);

private:
    void log(unsigned int level, const char *message);

    OpenRGBPluginAPIInterface *m_api = nullptr;
    // Kept until the plugin object dies: callbacks hold raw pointers to them.
    std::vector<std::unique_ptr<Part>> m_parts;
    bool m_writesEnabled = false;
    std::atomic<bool> m_loggedReadOnly = false;
};

} // namespace galleon::openrgb
