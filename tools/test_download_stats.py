# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

from datetime import datetime, timezone
import unittest

from website.download_stats import build_stats, parse_copr_downloads, platform_for


class DownloadStatsTests(unittest.TestCase):
    def test_only_installable_assets_are_counted(self):
        self.assertEqual(platform_for("GalleonCore-0.1.0-Windows-x86_64-Setup.exe"), "windows")
        self.assertEqual(platform_for("galleoncore_0.1.0-2.noble_amd64.deb"), "ubuntu")
        self.assertEqual(platform_for("galleoncore-0.1.0-1.fc44.x86_64.rpm"), "fedora")
        self.assertEqual(platform_for("galleoncore-0.1.0-1-x86_64.pkg.tar.zst"), "arch")
        self.assertEqual(platform_for("GalleonCore-0.1.0-x86_64.AppImage"), "appimage")
        self.assertIsNone(platform_for("galleoncore-debuginfo-0.1.0-1.fc44.x86_64.rpm"))
        self.assertIsNone(platform_for("galleoncore-0.1.0-1.fc44.src.rpm"))
        self.assertIsNone(platform_for("SHA256SUMS"))

    def test_first_snapshot_has_no_weekly_claim(self):
        releases = [{"assets": [{"id": 10, "name": "a.deb", "download_count": 20}]}]
        result = build_stats(releases, {"ppa": 2, "copr": 3}, 1,
                             {}, datetime(2026, 10, 22, tzinfo=timezone.utc))
        self.assertIsNone(result["total"])
        self.assertIsNone(result["since"])

    def test_deltas_use_asset_ids_and_ignore_count_resets(self):
        previous = {"history": [{"date": "2026-10-15", "repositories": {
            "ppa": 10, "copr": 20,
        }, "assets": {
            "10": {"count": 20, "platform": "ubuntu"},
            "11": {"count": 9, "platform": "windows"},
        }}]}
        releases = [{"assets": [
            {"id": 10, "name": "a.deb", "download_count": 27},
            {"id": 11, "name": "b-Windows-x86_64.zip", "download_count": 2},
            {"id": 12, "name": "c.AppImage", "download_count": 4},
        ]}]
        result = build_stats(releases, {"ppa": 12, "copr": 25}, 3,
                             previous, datetime(2026, 10, 22, tzinfo=timezone.utc))
        self.assertEqual(result["total"], 18)
        self.assertEqual(result["by_channel"]["ubuntu"], 7)
        self.assertEqual(result["by_channel"]["windows"], 0)
        self.assertEqual(result["by_channel"]["appimage"], 4)
        self.assertEqual(result["by_channel"]["ppa"], 2)
        self.assertEqual(result["by_channel"]["copr"], 5)
        self.assertEqual(result["aur_votes"], 3)
        self.assertEqual(result["since"], "2026-10-15")

    def test_unavailable_repository_does_not_become_zero(self):
        previous = {"history": [{"date": "2026-10-15", "assets": {},
                                 "repositories": {"ppa": 5, "copr": 9}}]}
        result = build_stats([], {"ppa": 7, "copr": None}, None,
                             previous, datetime(2026, 10, 22, tzinfo=timezone.utc))
        self.assertIsNone(result["by_channel"]["copr"])
        self.assertIsNone(result["total"])

    def test_copr_counts_rpms_not_repo_enables(self):
        page = '<tr><td>Fedora 44</td><td>x86_64 (12)*</td>' \
               '<td>Fedora 44 (7 downloads)</td></tr>'
        self.assertEqual(parse_copr_downloads(page), 12)
        with self.assertRaises(ValueError):
            parse_copr_downloads('<title>Making sure you are not a bot</title>')


if __name__ == "__main__":
    unittest.main()
