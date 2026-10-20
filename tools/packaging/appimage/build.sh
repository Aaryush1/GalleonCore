#!/bin/bash
# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=$(realpath -m "${1:-$root/build-deb/appimage}")
mkdir -p "$out"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 "$root/tools/packaging/appimage/fetch-tools.py" "$work/tools"
cmake -S "$root" -B "$work/build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib \
    -DGALLEON_SOURCE_TREE_PATHS=OFF -DBUILD_TESTING=OFF -DGALLEON_WERROR=ON
cmake --build "$work/build" -j "${JOBS:-4}"
DESTDIR="$work/AppDir" cmake --install "$work/build"
sh "$root/tools/packaging/install-extras.sh" "$root" "$work/AppDir"
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE=/usr/bin/qmake6
mkdir -p "$work/qml"
cp -r "$root/widgets" "$root/bosun/qml" "$work/qml/"
cp -r "$root/daemon/qml" "$work/qml/daemon"
export QML_SOURCES_PATHS="$work/qml"
export EXTRA_PLATFORM_PLUGINS='libqwayland-egl.so;libqwayland-generic.so;libqoffscreen.so'
export LD_LIBRARY_PATH="$work/AppDir/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$work/tools:$PATH"
"$work/tools/linuxdeploy-x86_64.AppImage" --appdir "$work/AppDir" \
    --executable "$work/AppDir/usr/bin/galleond" \
    --executable "$work/AppDir/usr/bin/galleon-bosun" \
    --desktop-file "$work/AppDir/usr/share/applications/galleon-bosun.desktop" \
    --icon-file "$work/AppDir/usr/share/icons/hicolor/scalable/apps/galleon-bosun.svg" --plugin qt
install -m755 "$root/tools/packaging/appimage/AppRun" "$work/AppDir/AppRun"
# Host integration is supplied separately; an AppImage mount is temporary.
cp "$root/tools/packaging/appimage/install-user-service.sh" "$out/"
cp "$root/tools/packaging/appimage/install-hardware-rules.sh" "$out/"
mkdir -p "$out/hardware"
cp "$root/tools/udev/"*.rules "$out/hardware/"
version=$(sed -n 's/project(GalleonCore VERSION \([^ ]*\).*/\1/p' "$root/CMakeLists.txt")
export ARCH=x86_64
"$work/tools/appimagetool-x86_64.AppImage" "$work/AppDir" "$out/GalleonCore-$version-x86_64.AppImage"
(cd "$out" && sha256sum ./*.AppImage > AppImage-SHA256SUMS)
# Exercise both entry points without hardware, D-Bus or a FUSE mount.
QT_QPA_PLATFORM=offscreen "$out/GalleonCore-$version-x86_64.AppImage" --help
QT_QPA_PLATFORM=offscreen "$out/GalleonCore-$version-x86_64.AppImage" --galleond --help
