# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
FROM galleoncore-packaging:24.04
RUN apt-get update -qq && apt-get install -y --no-install-recommends \
    build-essential debhelper devscripts qt6-wayland qt6-svg-dev libxkbcommon-x11-0 patchelf \
    curl zsync desktop-file-utils gnupg dput openssh-client python3-yaml shellcheck \
    && rm -rf /var/lib/apt/lists/*
