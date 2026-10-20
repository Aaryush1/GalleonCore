# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
"""Release source integrity: each package must build the same source bytes."""
import hashlib
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class DistributionSources(unittest.TestCase):
    def test_sources_are_repeatable_and_aur_checksums_match(self):
        # A minimal checkout also works in tools/check.py's clean copy, without .git.
        with tempfile.TemporaryDirectory() as tmp:
            checkout = Path(tmp)
            for name in ('.gitignore', 'CMakeLists.txt', 'lib/src/Protocol.cpp',
                         'tools/packaging/prepare.py', 'tools/packaging/build-deb.sh',
                         'tools/packaging/debian/rules', 'tools/packaging/aur/PKGBUILD.in'):
                target = checkout / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / name, target)
            ignored = checkout / 'build-deb/private-file'
            ignored.parent.mkdir()
            ignored.write_text('Must never be shipped')
            subprocess.run(['git', 'init', '-q', str(checkout)], check=True)
            outputs = [checkout / 'build-deb' / name for name in ('first', 'second')]
            for output in outputs:
                subprocess.run(['python3', '-B', str(checkout / 'tools/packaging/prepare.py'),
                                str(output)], check=True, capture_output=True)
            first = next(outputs[0].glob('*.tar.gz'))
            second = next(outputs[1].glob('*.tar.gz'))
            self.assertEqual(first.read_bytes(), second.read_bytes())
            digest = hashlib.sha256(first.read_bytes()).hexdigest()
            self.assertIn(digest, (outputs[0] / 'PKGBUILD').read_text())
            self.assertNotIn('@VERSION@', (outputs[0] / 'PKGBUILD').read_text())
            with tarfile.open(first) as archive:
                paths = [Path(member.name) for member in archive.getmembers()]
            relative = {str(Path(*path.parts[1:])) for path in paths}
            self.assertIn('tools/packaging/build-deb.sh', relative)
            self.assertIn('lib/src/Protocol.cpp', relative)
            self.assertIn('tools/packaging/debian/rules', relative)
            self.assertFalse(any('.git' in path.parts for path in paths))
            self.assertFalse(any('build-deb' in path.parts for path in paths))


if __name__ == '__main__':
    unittest.main()
