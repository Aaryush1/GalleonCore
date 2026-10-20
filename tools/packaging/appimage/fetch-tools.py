#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
"""Download official tools, refusing any change to the pinned SHA-256."""
import hashlib
import json
import sys
import urllib.request
from pathlib import Path

out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
for name, tool in json.loads(Path(__file__).with_name("tools.json").read_text()).items():
    data = urllib.request.urlopen(tool["url"], timeout=120).read()
    if hashlib.sha256(data).hexdigest() != tool["sha256"]:
        raise SystemExit(f"SHA-256 mismatch: {name}; review upstream before updating tools.json")
    path = out / name
    path.write_bytes(data)
    path.chmod(0o755)
