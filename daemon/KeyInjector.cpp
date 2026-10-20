// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "KeyInjector.h"

#include <QStringList>
#include <QDebug>
#include <stdexcept>
#include <utility>

namespace galleond {
namespace {
constexpr std::array<const char *, AllowedKeyCount> Names{
    "kp0", "kp1", "kp2", "kp3", "kp4", "kp5", "kp6", "kp7", "kp8", "kp9",
    "kpdot", "kpenter", "kpplus", "kpminus", "kpasterisk", "kpslash", "numlock", "backspace",
};
static_assert(static_cast<int>(KeyCode::Backspace) == AllowedKeyCount);
}

bool isAllowedKeyCode(int code)
{
    return code >= static_cast<int>(KeyCode::Kp0) && code <= static_cast<int>(KeyCode::Backspace);
}

std::optional<int> keyCodeForName(const QString &name)
{
    for (std::size_t i = 0; i < Names.size(); ++i) {
        if (name == QLatin1String(Names.at(i)))
            return static_cast<int>(i) + 1;
    }
    return std::nullopt;
}

QString allowedKeyNames()
{
    QStringList names;
    for (const char *name : Names)
        names << QLatin1String(name);
    return names.join(QStringLiteral(", "));
}

TrackedKeyboard::TrackedKeyboard(Sender sender) : m_send(std::move(sender))
{
    if (!m_send)
        throw std::invalid_argument("A keyboard sender is required");
}

TrackedKeyboard::~TrackedKeyboard()
{
    for (std::size_t i = 0; i < m_held.size(); ++i) {
        if (m_held.at(i)) {
            try {
                (void)m_send(static_cast<KeyCode>(i + 1), false);
            } catch (...) {
                // Continue releasing the other keys even if an injected sender throws.
                qWarning() << "galleond: keyboard release failed during shutdown";
            }
        }
    }
}

bool TrackedKeyboard::key(int code, bool down)
{
    if (!isAllowedKeyCode(code))
        return false;
    bool &held = m_held.at(static_cast<std::size_t>(code - 1));
    if (held == down)
        return true;
    if (!m_send(static_cast<KeyCode>(code), down))
        return false;
    held = down;
    return true;
}
} // namespace galleond
