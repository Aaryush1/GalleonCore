// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PlatformBackends.h"
#include "LinuxKeyInjector.h"
#include "MprisController.h"

namespace galleond {

std::unique_ptr<MediaController> createMediaController()
{
    return std::make_unique<MprisController>();
}

std::unique_ptr<KeyInjector> createKeyInjector()
{
    return std::make_unique<UinputKeyboard>();
}

} // namespace galleond
