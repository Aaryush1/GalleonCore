# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

from datetime import date, datetime, timezone
import unittest
import urllib.error

from website.download_stats import (
    build_stats, parse_copr_downloads, platform_for, previous_snapshot,
    repository_measurements, daily_refresh_needed,
)


class DownloadStatsTests(unittest.TestCase):
    def test_domain_migration_keeps_existing_baseline(self):
        calls = []
        def fetch(url, missing_ok=False):
            calls.append((url, missing_ok))
            if url == "https://galleoncore.xyz/downloads-weekly.json":
                raise urllib.error.URLError("DNS pending")
            return {"history": [{"date": "2026-10-22", "assets": {}}]}
        previous = previous_snapshot([
            "https://galleoncore.xyz/downloads-weekly.json",
            "https://paullux.github.io/GalleonCore/downloads-weekly.json",
        ], fetch)
        self.assertEqual(previous["history"][0]["date"], "2026-10-22")
        self.assertEqual(len(calls), 2)

    def test_no_baseline_if_both_urls_are_missing(self):
        self.assertEqual(previous_snapshot(["new", "old"], lambda *_args, **_kwargs: {}), {})

    def test_malformed_previous_snapshot_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "not an object"):
            previous_snapshot(["new", "old"], lambda *_args, **_kwargs: [])

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
                             previous, datetime(2026, 10, 22, tzinfo=timezone.utc),
                             repository_weekly={"ppa": 2, "copr": 5})
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
                             previous, datetime(2026, 10, 22, tzinfo=timezone.utc),
                             repository_weekly={"ppa": 2, "copr": None})
        self.assertIsNone(result["by_channel"]["copr"])
        self.assertIsNone(result["total"])

    def test_copr_counts_rpms_not_repo_enables(self):
        page = '<tr><td>Fedora 44</td><td>x86_64 (12)*</td>' \
               '<td>Fedora 44 (7 downloads)</td></tr>'
        self.assertEqual(parse_copr_downloads(page), 12)
        with self.assertRaises(ValueError):
            parse_copr_downloads('<title>Making sure you are not a bot</title>')

    def test_repository_counts_refresh_daily(self):
        self.assertFalse(daily_refresh_needed("2026-10-22", 12, date(2026, 10, 22)))
        self.assertTrue(daily_refresh_needed("2026-10-21", 12, date(2026, 10, 22)))
        self.assertTrue(daily_refresh_needed("2026-10-19", 12, date(2026, 10, 26)))
        self.assertTrue(daily_refresh_needed("2026-10-22", None, date(2026, 10, 22)))
        self.assertTrue(daily_refresh_needed("invalid", 12, date(2026, 10, 22)))

    def test_same_day_reuse_and_daily_refresh(self):
        previous = {
            "history": [{"date": "2026-10-19", "repositories": {"ppa": 10, "copr": 20},
                         "repository_checked_at": {"ppa": "2026-10-19", "copr": "2026-10-19"}}],
            "repository_checked_at": {"ppa": "2026-10-19", "copr": "2026-10-19",
                                      "aur": "2026-10-19"},
            "repository_weekly": {"ppa": 2, "copr": 3}, "aur_votes": 4,
        }
        def unexpected():
            self.fail("same-day repository check must not make a network request")
        totals, votes, checked, weekly, _ = repository_measurements(
            previous, date(2026, 10, 19),
            (("ppa", unexpected), ("copr", unexpected), ("aur", unexpected)))
        self.assertEqual(totals, {"ppa": 10, "copr": 20})
        self.assertEqual(votes, 4)
        self.assertEqual(weekly, {"ppa": 2, "copr": 3})
        self.assertEqual(checked["ppa"], "2026-10-19")

        totals, votes, checked, weekly, period = repository_measurements(
            previous, date(2026, 10, 26),
            (("ppa", lambda: 14), ("copr", lambda: 25), ("aur", lambda: 5)))
        self.assertEqual(totals, {"ppa": 14, "copr": 25})
        self.assertEqual(votes, 5)
        self.assertEqual(weekly, {"ppa": 4, "copr": 5})
        self.assertEqual(period["ppa"], {"from": "2026-10-19", "to": "2026-10-26"})
        self.assertEqual(checked["aur"], "2026-10-26")

    def test_unavailable_repository_retries_on_next_daily_run(self):
        previous = {"history": [{"date": "2026-10-19",
                                  "repositories": {"ppa": 10, "copr": 20}}],
                    "repository_checked_at": {"ppa": "2026-10-19", "copr": "2026-10-19"}}
        def unavailable():
            raise ValueError("temporary outage")
        totals, _, checked, weekly, period = repository_measurements(
            previous, date(2026, 10, 26),
            (("ppa", lambda: 14), ("copr", unavailable)))
        self.assertEqual(totals, {"ppa": 14, "copr": 20})
        self.assertEqual(weekly, {"ppa": None, "copr": None})
        self.assertTrue(daily_refresh_needed(checked["copr"], totals["copr"],
                                              date(2026, 10, 27)))
        self.assertEqual(checked["copr"], "2026-10-19")
        self.assertEqual(period["copr"]["to"], "2026-10-19")

    def test_daily_delta_requires_successful_observations(self):
        previous = {"history": [{"date": "2026-10-21", "assets": {},
                    "repositories": {"ppa": 10, "copr": 12},
                    "repository_checked_at": {"ppa": "2026-10-21"}}]}
        result = build_stats([], {"ppa": 15, "copr": 9}, 2, previous,
                             datetime(2026, 10, 22, tzinfo=timezone.utc),
                             {"ppa": "2026-10-22"})
        self.assertEqual(result["daily"]["ppa"], 5)
        self.assertIsNone(result["daily"]["copr"])
        self.assertIsNone(result["since"])
        stale = build_stats([], {"ppa": 10}, 2, previous,
                            datetime(2026, 10, 22, tzinfo=timezone.utc),
                            {"ppa": "2026-10-21"})
        self.assertIsNone(stale["daily"]["ppa"])

    def test_history_is_preserved_and_same_day_replaced(self):
        previous = {"history": [{"date": "2026-09-01", "assets": {}},
                                 {"date": "2026-10-22", "assets": {}}]}
        result = build_stats([], {}, None, previous,
                             datetime(2026, 10, 22, tzinfo=timezone.utc))
        self.assertEqual([item["date"] for item in result["history"]],
                         ["2026-09-01", "2026-10-22"])


if __name__ == "__main__":
    unittest.main()
