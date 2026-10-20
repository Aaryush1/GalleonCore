// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "KeyInjector.h"
#include "MediaController.h"

#include <memory>

namespace galleond {

// Platform implementations are selected by the build, not by the page logic.
[[nodiscard]] std::unique_ptr<MediaController> createMediaController();
[[nodiscard]] std::unique_ptr<KeyInjector> createKeyInjector();

} // namespace galleond
