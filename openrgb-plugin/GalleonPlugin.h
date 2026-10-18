// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "OpenRGBPluginInterface.h"

#include <QObject>

namespace galleon::openrgb {

// Proof of concept: registers one fake 12-LED controller through the plugin
// API (RegisterVirtualRGBController) and logs the colours OpenRGB sends it.
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
    RGBControllerInterface *m_controller = nullptr;
};

} // namespace galleon::openrgb
