// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LinuxKeyInjector.h"

#include <QDebug>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <span>
#include <string_view>
#include <unistd.h>

namespace galleond {

namespace {

struct KeyName
{
    const char *name;
    int code;
};

// Codes from linux/input-event-codes.h.
constexpr std::array<KeyName, 18> Names{{
    {.name="kp0", .code=KEY_KP0}, {.name="kp1", .code=KEY_KP1}, {.name="kp2", .code=KEY_KP2}, {.name="kp3", .code=KEY_KP3}, {.name="kp4", .code=KEY_KP4}, {.name="kp5", .code=KEY_KP5},
    {.name="kp6", .code=KEY_KP6}, {.name="kp7", .code=KEY_KP7}, {.name="kp8", .code=KEY_KP8}, {.name="kp9", .code=KEY_KP9}, {.name="kpdot", .code=KEY_KPDOT},
    {.name="kpenter", .code=KEY_KPENTER}, {.name="kpplus", .code=KEY_KPPLUS}, {.name="kpminus", .code=KEY_KPMINUS}, {.name="kpasterisk", .code=KEY_KPASTERISK},
    {.name="kpslash", .code=KEY_KPSLASH}, {.name="numlock", .code=KEY_NUMLOCK}, {.name="backspace", .code=KEY_BACKSPACE},
}};

} // namespace

std::optional<int> keyCodeForName(const QString &name)
{
    for (const KeyName &entry : Names) {
        if (name == QLatin1String(entry.name))
            return entry.code;
    }
    return std::nullopt;
}

QString allowedKeyNames()
{
    QStringList names;
    for (const KeyName &entry : Names)
        names << QLatin1String(entry.name);
    return names.join(QStringLiteral(", "));
}

UinputKeyboard::~UinputKeyboard()
{
    if (m_fd >= 0) {
        ioctl(m_fd, UI_DEV_DESTROY);
        close(m_fd);
    }
}

void UinputKeyboard::prepare()
{
    if (m_fd >= 0 || m_failed)
        return;
    const int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        m_failed = true;
        qWarning() << "galleond: cannot open /dev/uinput (" << std::strerror(errno)
                   << "): key actions do nothing. See tools/udev/71-galleon-uinput.rules";
        return;
    }
    bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0;
    for (const KeyName &entry : Names)
        ok = ioctl(fd, UI_SET_KEYBIT, entry.code) == 0 && ok;
    uinput_setup setup{};
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x1b1c; // the Galleon's maker, so that the device is recognisable
    setup.id.product = 0x2b18;
    setup.id.version = 1;
    constexpr std::string_view DeviceName = "GalleonCore virtual keypad"; // the rest of setup.name stays zero
    static_assert(DeviceName.size() < UINPUT_MAX_NAME_SIZE);
    std::ranges::copy(DeviceName, std::span<char>(setup.name).begin());
    ok = ok && ioctl(fd, UI_DEV_SETUP, &setup) == 0 && ioctl(fd, UI_DEV_CREATE) == 0;
    if (!ok) {
        m_failed = true;
        qWarning() << "galleond: cannot create the virtual keyboard (" << std::strerror(errno) << ")";
        close(fd);
        return;
    }
    m_fd = fd;
    qInfo() << "galleond: virtual keypad created";
}

bool UinputKeyboard::key(int code, bool down)
{
    prepare();
    if (m_fd < 0)
        return false;
    // Only the keys of the fixed list: the device was not given any other.
    bool allowed = false;
    for (const KeyName &entry : Names)
        allowed = allowed || entry.code == code;
    if (!allowed)
        return false;

    std::array<input_event, 2> events{};
    events[0].type = EV_KEY;
    events[0].code = static_cast<unsigned short>(code);
    events[0].value = down ? 1 : 0;
    events[1].type = EV_SYN;
    events[1].code = SYN_REPORT;
    events[1].value = 0;
    return write(m_fd, events.data(), sizeof events) == static_cast<ssize_t>(sizeof events);
}

} // namespace galleond
