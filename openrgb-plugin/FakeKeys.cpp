// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FakeKeys.h"

namespace galleon::openrgb {

bool isValidLedIndex(unsigned int index)
{
    return index < FakeLedCount;
}

std::string fakeLedName(unsigned int index)
{
    if (!isValidLedIndex(index))
        return {};
    return "Key " + std::to_string(index + 1);
}

} // namespace galleon::openrgb
