# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
FROM fedora:44
RUN dnf install -y git rpm-build rpmdevtools gcc-c++ cmake make pkgconf-pkg-config \
    qt6-qtbase-devel qt6-qtdeclarative-devel hidapi-devel dbus-daemon \
    && dnf clean all
