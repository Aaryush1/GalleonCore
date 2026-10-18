// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "OpenRGBPluginInterface.h"

#include <QObject>

#include <vector>

namespace galleon::openrgb {

// Reads the two LampArray interfaces of the Galleon 100 SD with libusb and
// registers one controller per part through the plugin API
// (RegisterVirtualRGBController). READ-ONLY for now: the colours OpenRGB sets
// are logged, never sent to the hardware.
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

    // Called by the controller callback, with the owner pointer given at setup.
    void onUpdateLeds();

private:
    void log(unsigned int level, const char *message);

    OpenRGBPluginAPIInterface *m_api = nullptr;
    std::vector<RGBControllerInterface *> m_controllers;
    bool m_loggedFirstUpdate = false;
};

} // namespace galleon::openrgb
