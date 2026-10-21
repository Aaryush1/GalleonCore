#!/bin/bash
# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
archive=$(realpath "${1:?source archive}")
out=$(realpath -m "${2:?output directory}")
series=${3:-noble}
mode=${4:-binary}
revision=${5:-1}
case "$series" in noble|resolute) ;; *) echo 'Unsupported Ubuntu series' >&2; exit 1;; esac
case "$mode" in binary|source) ;; *) exit 1;; esac
[[ "$revision" =~ ^[1-9][0-9]*$ ]] || { echo 'Invalid Debian revision' >&2; exit 1; }
packaging_dir=$(dirname "$(realpath "$0")")
version=$(basename "$archive" .tar.gz)
version=${version#galleoncore-}
mkdir -p "$out"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$archive" "$work/galleoncore_${version}.orig.tar.gz"
tar -xzf "$archive" -C "$work"
cd "$work/galleoncore-$version"
if [ "$revision" = 1 ]; then
    cp -r tools/packaging/debian .
else
    # Keep the released orig tarball byte-for-byte; only Debian packaging changes.
    cp -r "$packaging_dir/debian" .
fi
# Version and series come from the same tested source artifact for every target.
export DEBFULLNAME='Paul Woisard' DEBEMAIL='paulwoisard@gmail.com'
dch -b --newversion "$version-$revision~$series" --distribution "$series" --force-distribution 'Linux distribution release.'
if [ "$mode" = source ]; then
    dpkg-buildpackage -S -sa -us -uc -d
else
    dpkg-buildpackage -b -us -uc -j"${JOBS:-4}"
fi
find "$work" -maxdepth 1 -type f ! -name '*.tar.gz' -exec cp -t "$out" {} +
cp "$work/galleoncore_${version}.orig.tar.gz" "$out/"
