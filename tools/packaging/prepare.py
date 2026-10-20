#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
"""Create deterministic release sources and native packaging from this checkout."""
import argparse
import gzip
import hashlib
import io
import re
import subprocess
import tarfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("output", type=Path)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
version = re.search(r"project\(GalleonCore VERSION ([0-9]+\.[0-9]+\.[0-9]+)",
                    (root / "CMakeLists.txt").read_text()).group(1)
# Include new, non-ignored packaging files during local validation too.
files = subprocess.check_output(["git", "ls-files", "-z", "--cached", "--others",
                                 "--exclude-standard"], cwd=root).split(b"\0")
archive = out / f"galleoncore-{version}.tar.gz"
with archive.open("wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as zipped:
    with tarfile.open(fileobj=zipped, mode="w", format=tarfile.PAX_FORMAT) as tar:
        for filename in sorted(set(files)):
            if not filename:
                continue
            path = root / filename.decode()
            if not path.is_file() or path.is_relative_to(out):
                continue
            info = tarfile.TarInfo(f"galleoncore-{version}/{filename.decode()}")
            info.size = path.stat().st_size
            info.mode = 0o755 if path.stat().st_mode & 0o111 else 0o644
            tar.addfile(info, io.BytesIO(path.read_bytes()))
sha = hashlib.sha256(archive.read_bytes()).hexdigest()
recipe = (root / "tools/packaging/aur/PKGBUILD.in").read_text()
(out / "PKGBUILD").write_text(recipe.replace("@VERSION@", version).replace("@SHA256@", sha))
(out / "SHA256SUMS").write_text(f"{sha}  {archive.name}\n")
(out / "version").write_text(version + "\n")
print(archive)
