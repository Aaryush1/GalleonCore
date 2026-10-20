#!/bin/bash
# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
# Run inside the packaging container with /src read-only and /out writable.
set -euo pipefail
jobs=${JOBS:-4}
arch=$(dpkg --print-architecture)
triplet=$(dpkg-architecture -qDEB_HOST_MULTIARCH)
version=$(sed -n 's/project(GalleonCore VERSION \([^ ]*\).*/\1/p' /src/CMakeLists.txt)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
stage=$work/stage
cmake -S /src -B "$work/release" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR="lib/$triplet" \
    -DGALLEON_SOURCE_TREE_PATHS=OFF -DBUILD_TESTING=OFF -DGALLEON_WERROR=ON
cmake --build "$work/release" -j "$jobs"
DESTDIR="$stage" cmake --install "$work/release"
mkdir -p "$stage/DEBIAN" "$stage/usr/share/doc/galleoncore" "$stage/usr/lib/modules-load.d"
printf 'uinput\n' > "$stage/usr/lib/modules-load.d/galleoncore.conf"
cp /src/tools/packaging/README.Debian "$stage/usr/share/doc/galleoncore/"
cp -r /src/LICENSES "$stage/usr/share/doc/galleoncore/"
(cd /src && reuse spdx) > "$stage/usr/share/doc/galleoncore/copyright.spdx"
cat > "$stage/usr/share/doc/galleoncore/copyright" <<'COPYRIGHT'
Copyright 2026 Paul Woisard
Application, widgets, integration and packaging: GPL-3.0-or-later.
libgalleon (including installed headers): LGPL-3.0-or-later.
Wallpapers: CC-BY-SA-4.0; provenance accompanies each wallpaper directory.
Full license texts: LICENSES/ in this directory.
Per-file attribution: copyright.spdx in this directory.
COPYRIGHT
# Resolve library dependencies against the target distribution, including our bundled lib.
mkdir -p "$work/debian"
printf 'Source: galleoncore\nSection: utils\nPriority: optional\nMaintainer: Paul Woisard <paul@localhost>\n\nPackage: galleoncore\nArchitecture: any\nDescription: GalleonCore\n' > "$work/debian/control"
strip --strip-unneeded "$stage/usr/bin/galleond" "$stage/usr/bin/galleon-bosun" "$stage/usr/lib/$triplet/libgalleon.so.$version"
deps=$(cd "$work" && dpkg-shlibdeps -O --ignore-missing-info \
    -l"$stage/usr/lib/$triplet" -e"$stage/usr/bin/galleond" \
    -e"$stage/usr/bin/galleon-bosun" -e"$stage/usr/lib/$triplet/libgalleon.so.$version" | sed 's/^shlibs:Depends=//')
cat > "$stage/DEBIAN/control" <<CONTROL
Package: galleoncore
Version: $version-1
Section: utils
Priority: optional
Architecture: $arch
Maintainer: Paul Woisard <paul@localhost>
Depends: $deps, qml6-module-qtquick (>= 6.4), qml6-module-qtquick-window (>= 6.4), qml6-module-qtqml (>= 6.4), qml6-module-qtqml-workerscript (>= 6.4), libglib2.0-bin, xdg-utils, systemd, udev
Recommends: ffmpeg
Suggests: wireplumber, steam, openrgb
Installed-Size: $(du -sk "$stage/usr" | cut -f1)
Description: Corsair Galleon Stream Deck support and settings
 GalleonCore daemon, Bosun settings, libgalleon, QML widgets and wallpapers.
 Includes udev access rules and an opt-in systemd user service.
CONTROL
printf 'libgalleon 0 galleoncore (>= %s-1)\n' "$version" > "$stage/DEBIAN/shlibs"
printf 'activate-noawait ldconfig\n' > "$stage/DEBIAN/triggers"
cp /src/tools/packaging/postinst /src/tools/packaging/postrm "$stage/DEBIAN/"
chmod 755 "$stage/DEBIAN/postinst" "$stage/DEBIAN/postrm"
(cd "$stage" && find usr -type f -print0 | sort -z | xargs -0 md5sum > DEBIAN/md5sums)
dpkg-deb --root-owner-group --build "$stage" "/out/galleoncore_${version}-1_${arch}.deb"
# Separate build: tests retain their source fixtures, release binaries do not.
cmake -S /src -B "$work/tests" -DCMAKE_BUILD_TYPE=Release \
    -DGALLEON_SOURCE_TREE_PATHS=OFF -DBUILD_TESTING=ON -DGALLEON_WERROR=ON
cmake --build "$work/tests" -j "$jobs"
ctest --test-dir "$work/tests" --output-on-failure -j "$jobs"
(cd /src && reuse lint)
LD_LIBRARY_PATH="$stage/usr/lib/$triplet" QT_QPA_PLATFORM=offscreen "$stage/usr/bin/galleond" --help
LD_LIBRARY_PATH="$stage/usr/lib/$triplet" QT_QPA_PLATFORM=offscreen "$stage/usr/bin/galleon-bosun" --help
if strings "$stage/usr/bin/galleond" "$stage/usr/bin/galleon-bosun" | grep -F '/src/'; then
    echo 'Source path found in release binaries' >&2
    exit 1
fi
