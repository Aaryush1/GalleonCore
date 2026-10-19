// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <QString>

namespace galleond {

// The keys a pages file may press, by name. A fixed list: the numeric keypad,
// Num Lock and Backspace. A name outside it is refused when the file is read,
// and the virtual keyboard is only ever given these codes, so the pages file
// cannot make galleond type anything else.
//
// Names: kp0..kp9, kpdot, kpenter, kpplus, kpminus, kpasterisk, kpslash,
// numlock, backspace.
[[nodiscard]] std::optional<int> keyCodeForName(const QString &name);
[[nodiscard]] QString allowedKeyNames();

// Presses and releases keys as if a keyboard did. The real one is a virtual
// keyboard (uinput); the tests replace it.
class KeyInjector
{
public:
    virtual ~KeyInjector() = default;
    // Called when a pages file with key actions is applied: opens the device
    // early, so that the first press is not lost while the desktop notices it.
    virtual void prepare() {}
    // `code` is a Linux key code from keyCodeForName(). False if it was not sent.
    virtual bool key(int code, bool down) = 0;
};

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
