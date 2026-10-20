#!/bin/sh
# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
# Explicit administrator action, separate from running the AppImage.
set -eu
[ "$(id -u)" = 0 ] || { echo 'Run with sudo to install hardware rules.' >&2; exit 1; }
here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
install -d /usr/lib/udev/rules.d /usr/lib/modules-load.d
install -m644 "$here/hardware/70-galleon.rules" /usr/lib/udev/rules.d/
install -m644 "$here/hardware/71-galleon-uinput.rules" /usr/lib/udev/rules.d/
printf 'uinput\n' > /usr/lib/modules-load.d/galleoncore.conf
udevadm control --reload-rules
modprobe uinput
udevadm trigger --subsystem-match=hidraw
udevadm trigger --subsystem-match=usb --attr-match=idVendor=1b1c
udevadm trigger --subsystem-match=misc --sysname-match=uinput
