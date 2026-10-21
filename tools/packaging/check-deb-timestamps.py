#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reject Debian packages containing pre-2000 payload timestamps."""
import subprocess
import sys
import tarfile

MINIMUM_MTIME = 946684800


def check(package):
    with subprocess.Popen(["dpkg-deb", "--fsys-tarfile", package],
                          stdout=subprocess.PIPE) as process:
        with tarfile.open(fileobj=process.stdout, mode="r|*") as archive:
            old = [member.name for member in archive
                   if member.isfile() and member.mtime < MINIMUM_MTIME]
        if process.wait() != 0:
            raise RuntimeError(f"Cannot inspect {package}")
    if old:
        raise RuntimeError(f"{package}: {len(old)} old payload timestamps, e.g. {old[0]}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("Usage: check-deb-timestamps.py PACKAGE.deb [...]")
    for name in sys.argv[1:]:
        check(name)
        print(f"Valid payload timestamps: {name}")
