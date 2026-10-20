// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <cstdint>
#include <array>
#include <functional>
#include <QString>

namespace galleond {

// Internal identifiers, independent of Linux input codes or Windows scan codes.
// Zero is reserved for "no held key" in the page controller.
enum class KeyCode : std::uint8_t {
    Kp0 = 1, Kp1, Kp2, Kp3, Kp4, Kp5, Kp6, Kp7, Kp8, Kp9,
    KpDot, KpEnter, KpPlus, KpMinus, KpAsterisk, KpSlash, NumLock, Backspace,
};
inline constexpr int AllowedKeyCount = 18;
[[nodiscard]] bool isAllowedKeyCode(int code);

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
// keyboard (uinput on Linux, SendInput on Windows); the tests replace it.
class KeyInjector
{
public:
    virtual ~KeyInjector() = default;
    // Called when a pages file with key actions is applied: opens the device
    // early, so that the first press is not lost while the desktop notices it.
    virtual void prepare() {}
    // `code` is an internal KeyCode from keyCodeForName(). False if it was not sent.
    virtual bool key(int code, bool down) = 0;
};

// Tracks the fixed whitelist and releases held keys on shutdown. The sender is
// injectable so the Windows lifetime policy is tested without typing real keys.
class TrackedKeyboard final : public KeyInjector
{
public:
    using Sender = std::function<bool(KeyCode, bool)>;
    explicit TrackedKeyboard(Sender sender);
    ~TrackedKeyboard() override;
    TrackedKeyboard(const TrackedKeyboard &) = delete;
    TrackedKeyboard &operator=(const TrackedKeyboard &) = delete;
    bool key(int code, bool down) override;
private:
    Sender m_send;
    std::array<bool, AllowedKeyCount> m_held{};
};

} // namespace galleond
