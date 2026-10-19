// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "KeyInjector.h"

#include <QDebug>
#include <QStringList>

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
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
    {"kp0", KEY_KP0}, {"kp1", KEY_KP1}, {"kp2", KEY_KP2}, {"kp3", KEY_KP3}, {"kp4", KEY_KP4}, {"kp5", KEY_KP5},
    {"kp6", KEY_KP6}, {"kp7", KEY_KP7}, {"kp8", KEY_KP8}, {"kp9", KEY_KP9}, {"kpdot", KEY_KPDOT},
    {"kpenter", KEY_KPENTER}, {"kpplus", KEY_KPPLUS}, {"kpminus", KEY_KPMINUS}, {"kpasterisk", KEY_KPASTERISK},
    {"kpslash", KEY_KPSLASH}, {"numlock", KEY_NUMLOCK}, {"backspace", KEY_BACKSPACE},
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
    std::strncpy(setup.name, "GalleonCore virtual keypad", UINPUT_MAX_NAME_SIZE - 1);
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
