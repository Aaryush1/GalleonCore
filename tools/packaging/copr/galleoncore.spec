# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
Name:           galleoncore
Version:        0.2.0
Release:        1%{?dist}
Summary:        Corsair Galleon Stream Deck support and settings
License:        GPL-3.0-or-later AND LGPL-3.0-or-later AND CC-BY-SA-4.0
URL:            https://github.com/Paullux/GalleonCore
Source0:        %{url}/releases/download/v%{version}/galleoncore-%{version}.tar.gz
BuildRequires:  gcc-c++ cmake make pkgconf-pkg-config
BuildRequires:  qt6-qtbase-devel >= 6.4
BuildRequires:  qt6-qtwebsockets-devel >= 6.4
BuildRequires:  qt6-qtdeclarative-devel >= 6.4
BuildRequires:  hidapi-devel dbus-daemon
Requires:       qt6-qtdeclarative >= 6.4
Requires:       systemd udev glib2 xdg-utils
Recommends:     ffmpeg-free
Suggests:       wireplumber openrgb

%description
GalleonCore daemon, Bosun settings, libgalleon, QML widgets and wallpapers.
Includes udev access rules and an opt-in systemd user service.

%prep
%autosetup -n galleoncore-%{version}

%build
%cmake -DGALLEON_SOURCE_TREE_PATHS=OFF -DGALLEON_PACKAGE_FORMAT=rpm -DBUILD_TESTING=ON -DGALLEON_WERROR=OFF
%cmake_build

%install
%cmake_install
sh tools/packaging/install-extras.sh . %{buildroot}

%check
QT_QPA_PLATFORM=offscreen %ctest --output-on-failure

%post
if command -v udevadm >/dev/null 2>&1; then
    udevadm control --reload-rules || :
    udevadm trigger --subsystem-match=hidraw || :
    udevadm trigger --subsystem-match=usb --attr-match=idVendor=1b1c || :
    udevadm trigger --subsystem-match=misc --sysname-match=uinput || :
fi
if command -v modprobe >/dev/null 2>&1; then modprobe uinput || :; fi

%postun
if command -v udevadm >/dev/null 2>&1; then udevadm control --reload-rules || :; fi

%files
%license LICENSES/*
%license %{_datadir}/licenses/galleoncore/AMD-SMI-MIT.txt
%license %{_datadir}/licenses/galleoncore/PresentMon-API-MIT.txt
%{_bindir}/galleond
%{_bindir}/galleon-bosun
%{_bindir}/galleon-gpu-probe
%{_bindir}/galleon-presentmon-probe
%{_libdir}/libgalleon.so*
%{_includedir}/galleon/
%{_datadir}/galleoncore/
%{_datadir}/applications/galleon-bosun.desktop
%{_datadir}/icons/hicolor/scalable/apps/galleon-bosun.svg
%{_prefix}/lib/udev/rules.d/*galleon*.rules
%{_prefix}/lib/systemd/user/galleond.service
%{_prefix}/lib/modules-load.d/galleoncore.conf
%{_docdir}/galleoncore/

%changelog
* Sat Oct 24 2026 Paul Woisard <paulwoisard@gmail.com> - 0.2.0-1
- Add home widgets, automatic Gaming mode and recording controls

* Tue Oct 20 2026 Paul Woisard <paulwoisard@gmail.com> - 0.1.0-1
- Initial Linux distribution packaging
