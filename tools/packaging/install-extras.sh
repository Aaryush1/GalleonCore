#!/bin/sh
# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
# Called by native package builders after cmake --install.
root=$1
stage=$2
doc="$stage/usr/share/doc/galleoncore"
install -d "$doc" "$stage/usr/lib/modules-load.d"
cp -r "$root/LICENSES" "$doc/"
install -m644 "$root/tools/packaging/README.Debian" "$doc/README.integration"
install -m644 "$root/tools/packaging/debian/copyright" "$doc/copyright"
printf 'uinput\n' > "$stage/usr/lib/modules-load.d/galleoncore.conf"
