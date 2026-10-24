# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

# The environment of the GitHub CI (ubuntu-24.04: GCC 13, Clang 18, Qt 6.4, clang-tidy 18),
# to run its checks on a machine that has other versions. Used by tools/check.py, which
# rebuilds the image when this file changes. The source is mounted read only at /src, the
# build trees live in the volume /build and the caches in /cache, so nothing is lost
# between two runs.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
# Same packages as .github/workflows/ci.yml, plus what the checks themselves need
# (git and file for reuse, python3 for tools/check.py, dbus-run-session for ctest).
RUN apt-get update -qq \
 && apt-get install -y -qq --no-install-recommends \
      ca-certificates cmake make g++ clang libclang-rt-18-dev clang-tidy pkg-config \
      qt6-base-dev qt6-declarative-dev qt6-websockets-dev libhidapi-dev dbus \
      qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtqml qml6-module-qtqml-workerscript \
      git file python3 pipx \
 && rm -rf /var/lib/apt/lists/* \
 && PIPX_HOME=/opt/pipx PIPX_BIN_DIR=/usr/local/bin pipx install reuse \
 && git config --system --add safe.directory '*'

VOLUME ["/build", "/cache"]
WORKDIR /src
