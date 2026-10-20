// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "KeyInjector.h"
#include "PlatformBackends.h"

#include <Windows.h>

namespace galleond {
namespace {

bool sendWindowsKey(KeyCode key, bool down)
{
    // Virtual-key constants and MapVirtualKeyW are documented in Winuser.h.
    // Use scan codes for physical keypad semantics, including Num Lock state.
    UINT virtualKey = 0;
    switch (key) {
    case KeyCode::Kp0: virtualKey = VK_NUMPAD0; break;
    case KeyCode::Kp1: virtualKey = VK_NUMPAD1; break;
    case KeyCode::Kp2: virtualKey = VK_NUMPAD2; break;
    case KeyCode::Kp3: virtualKey = VK_NUMPAD3; break;
    case KeyCode::Kp4: virtualKey = VK_NUMPAD4; break;
    case KeyCode::Kp5: virtualKey = VK_NUMPAD5; break;
    case KeyCode::Kp6: virtualKey = VK_NUMPAD6; break;
    case KeyCode::Kp7: virtualKey = VK_NUMPAD7; break;
    case KeyCode::Kp8: virtualKey = VK_NUMPAD8; break;
    case KeyCode::Kp9: virtualKey = VK_NUMPAD9; break;
    case KeyCode::KpDot: virtualKey = VK_DECIMAL; break;
    case KeyCode::KpEnter: virtualKey = VK_RETURN; break;
    case KeyCode::KpPlus: virtualKey = VK_ADD; break;
    case KeyCode::KpMinus: virtualKey = VK_SUBTRACT; break;
    case KeyCode::KpAsterisk: virtualKey = VK_MULTIPLY; break;
    case KeyCode::KpSlash: virtualKey = VK_DIVIDE; break;
    case KeyCode::NumLock: virtualKey = VK_NUMLOCK; break;
    case KeyCode::Backspace: virtualKey = VK_BACK; break;
    }
    const UINT scan = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC_EX);
    if (virtualKey == 0 || scan == 0)
        return false;
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = static_cast<WORD>(scan & 0xffU);
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0U : KEYEVENTF_KEYUP);
    if ((scan & 0xff00U) == 0xe000U || key == KeyCode::KpEnter)
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    return SendInput(1, &input, static_cast<int>(sizeof(input))) == 1;
}
}

std::unique_ptr<KeyInjector> createKeyInjector()
{
    return std::make_unique<TrackedKeyboard>(sendWindowsKey);
}
} // namespace galleond
