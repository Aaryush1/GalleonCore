// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Builds the OpenRGB description of the fake device. Only this file and
// GalleonPlugin know OpenRGB types; the rest stays free of them.

#include "RGBControllerInterface.h"

namespace galleon::openrgb {

// `owner` is handed back to the callbacks as their first argument.
// The callbacks are left null: the caller sets the ones it needs.
[[nodiscard]] RGBController_Setup makeFakeSetup(void *owner);

} // namespace galleon::openrgb
