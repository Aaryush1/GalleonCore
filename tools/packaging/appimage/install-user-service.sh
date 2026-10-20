#!/bin/bash
# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
# Run as the desktop user, passing the absolute path of a stable AppImage copy.
set -euo pipefail
if [ "$EUID" = 0 ]; then echo 'Run as your desktop user, without sudo.' >&2; exit 1; fi
image=$(realpath -- "${1:?Usage: install-user-service.sh /absolute/path/GalleonCore.AppImage}")
[ -f "$image" ] && [ -x "$image" ]
# Escape paths for systemd ExecStart, including specifier/environment expansion.
escaped=${image//\\/\\\\}
escaped=${escaped//\"/\\\"}
escaped=${escaped//%/%%}
escaped=${escaped//\$/\$\$}
config=${XDG_CONFIG_HOME:-$HOME/.config}
mkdir -p "$config/systemd/user"
cat > "$config/systemd/user/galleond.service" <<UNIT
[Unit]
Description=GalleonCore daemon (AppImage)
PartOf=graphical-session.target
After=graphical-session.target
[Service]
ExecStart="$escaped" --galleond
Environment=APPIMAGE_EXTRACT_AND_RUN=1
Restart=on-failure
RestartSec=5
[Install]
WantedBy=graphical-session.target
UNIT
systemctl --user daemon-reload
printf 'Service installed. To enable: systemctl --user enable --now galleond.service\n'
