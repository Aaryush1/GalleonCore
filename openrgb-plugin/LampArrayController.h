// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Builds the OpenRGB description of a LampArray device read from the hardware.
// Only this file and GalleonPlugin know OpenRGB types.

#include "RGBControllerInterface.h"
#include "UsbLampArray.h"

#include <string>

namespace galleon::openrgb {

// `owner` is handed back to the callbacks as their first argument. The
// callbacks are left null: the caller sets the ones it needs.
[[nodiscard]] RGBController_Setup makeLampArraySetup(const LampArrayData &data,
                                                      const std::string &name,
                                                      const std::string &location,
                                                      void *owner);

} // namespace galleon::openrgb
