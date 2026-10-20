#!/bin/bash
# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
# GitHub runners have unrelated vendor repositories which can fail independently.
# Use the signed Ubuntu sources for this project's distro build dependencies.
set -euo pipefail
sources=/etc/apt/sources.list.d/ubuntu.sources
test -f "$sources"
options=(-o "Dir::Etc::sourcelist=$sources" -o 'Dir::Etc::sourceparts=-')
sudo apt-get "${options[@]}" update -qq
sudo apt-get "${options[@]}" install -y "$@"
