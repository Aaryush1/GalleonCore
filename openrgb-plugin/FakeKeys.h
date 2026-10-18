// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Pure description of the fake 12-LED device used to prove that a plugin can
// register a controller with OpenRGB. No OpenRGB, no Qt: unit-tested alone.
//
// The layout of the real keys is not measured yet, so the fake device is a
// plain linear zone: nothing here claims a grid shape.

#include <string>

namespace galleon::openrgb {

inline constexpr unsigned int FakeLedCount = 12;

[[nodiscard]] bool isValidLedIndex(unsigned int index);

// "Key 1" .. "Key 12". An out-of-range index gives an empty string.
[[nodiscard]] std::string fakeLedName(unsigned int index);

} // namespace galleon::openrgb
