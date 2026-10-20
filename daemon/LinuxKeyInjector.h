// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "KeyInjector.h"

namespace galleond {

// A virtual keyboard made with /dev/uinput. It needs write access to that file
// (see tools/udev/71-galleon-uinput.rules). Without it, key actions do nothing
// and the reason is logged once.
class UinputKeyboard : public KeyInjector
{
public:
    UinputKeyboard() = default;
    ~UinputKeyboard() override;
    UinputKeyboard(const UinputKeyboard &) = delete;
    UinputKeyboard &operator=(const UinputKeyboard &) = delete;

    void prepare() override;
    bool key(int code, bool down) override;

private:
    int m_fd = -1;
    bool m_failed = false; // said once
};

} // namespace galleond
