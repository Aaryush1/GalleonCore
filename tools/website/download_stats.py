# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
"""Collect daily download measurements without triggering application CI."""

import argparse
from datetime import date, datetime, timedelta, timezone
import html
import json
import os
from pathlib import Path
import re
import urllib.error
import urllib.parse
import urllib.request


PLATFORMS = ("windows", "ubuntu", "fedora", "arch", "appimage")
REPOSITORIES = ("ppa", "copr")
MAX_HISTORY = 366


def platform_for(name):
    lower = name.lower()
    if lower.endswith(("-setup.exe", "-windows-x86_64.zip")):
        return "windows"
    if lower.endswith(".deb") and not lower.endswith(".ddeb"):
        return "ubuntu"
    if lower.endswith(".rpm") and ".src." not in lower and not any(
        part in lower for part in ("-debuginfo-", "-debugsource-")
    ):
        return "fedora"
    if lower.endswith(".pkg.tar.zst") and "-debug-" not in lower:
        return "arch"
    if lower.endswith(".appimage"):
        return "appimage"
    return None


def snapshot(releases, date):
    assets = {}
    for release in releases:
        if release.get("draft") or release.get("prerelease"):
            continue
        for asset in release.get("assets", []):
            category = platform_for(asset.get("name", ""))
            asset_id, count = asset.get("id"), asset.get("download_count")
            if category and type(asset_id) is int and type(count) is int and count >= 0:
                assets[str(asset_id)] = {"count": count, "platform": category}
    return {"date": date, "assets": assets}


def daily_refresh_needed(last_checked, previous_value, today):
    if type(previous_value) is not int or previous_value < 0:
        return True
    try:
        checked = date.fromisoformat(last_checked)
    except (TypeError, ValueError):
        return True
    return checked != today


def build_stats(releases, repository_totals, aur_votes, previous, now,
                repository_checked_at=None, repository_weekly=None,
                repository_period=None):
    today = now.date().isoformat()
    current = snapshot(releases, today)
    current["repositories"] = repository_totals
    cutoff = (now.date() - timedelta(days=365)).isoformat()
    history = []
    for item in previous.get("history", []):
        if not isinstance(item, dict) or not isinstance(item.get("assets"), dict):
            continue
        date = item.get("date")
        if isinstance(date, str) and cutoff <= date < today:
            history.append(item)
    history = sorted(history, key=lambda item: item["date"])[-MAX_HISTORY:]
    week_start = (now.date() - timedelta(days=7)).isoformat()
    baseline = next((item for item in history if item["date"] == week_start), None)
    yesterday = (now.date() - timedelta(days=1)).isoformat()
    daily_baseline = next((item for item in history if item["date"] == yesterday), None)
    current["repository_checked_at"] = dict(repository_checked_at or {})
    # A first snapshot cannot reveal downloads from the preceding seven days.
    by_channel = None
    if baseline:
        by_channel = {name: 0 for name in PLATFORMS}
        for asset_id, item in current["assets"].items():
            old = baseline["assets"].get(asset_id, {})
            old_count = old.get("count", 0)
            if type(old_count) is not int or old_count < 0:
                old_count = 0
            by_channel[item["platform"]] += max(0, item["count"] - old_count)
        for name in REPOSITORIES:
            by_channel[name] = (repository_weekly or {}).get(name)
    history.append(current)
    total = None
    if by_channel and all(type(value) is int for value in by_channel.values()):
        total = sum(by_channel.values())
    return {
        "generated_at": now.isoformat().replace("+00:00", "Z"),
        "since": baseline["date"] if baseline else None,
        "daily": interval_deltas(current, daily_baseline),
        "measurement_timezone": "UTC",
        "by_channel": by_channel,
        "total": total,
        "aur_votes": aur_votes,
        "repository_checked_at": repository_checked_at or {},
        "repository_weekly": repository_weekly or {},
        "repository_period": repository_period or {},
        "history": history,
    }


def interval_deltas(current, baseline):
    """Changes between daily observations; not calendar-day activity."""
    if not baseline:
        return None
    result = {name: 0 for name in PLATFORMS}
    for asset_id, item in current["assets"].items():
        old = baseline["assets"].get(asset_id, {}).get("count", 0)
        result[item["platform"]] += max(0, item["count"] - old)
    for name in REPOSITORIES:
        value = current.get("repositories", {}).get(name)
        old = baseline.get("repositories", {}).get(name)
        fresh = current.get("repository_checked_at", {}).get(name) == current["date"]
        old_fresh = baseline.get("repository_checked_at", {}).get(name) == baseline["date"]
        result[name] = (max(0, value - old) if fresh and old_fresh
                        and type(value) is int and type(old) is int else None)
    return result


def get_json(url, token=None, missing_ok=False):
    headers = {"Accept": "application/vnd.github+json", "User-Agent": "GalleonCore-pages"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        if missing_ok and error.code == 404:
            return {}
        raise


def previous_snapshot(urls, fetch=get_json):
    """Keep the baseline while the site moves to its custom domain."""
    last_error = None
    for url in urls:
        try:
            previous = fetch(url, missing_ok=True)
        except urllib.error.URLError as error:
            last_error = error
            continue
        if not isinstance(previous, dict):
            raise ValueError(f"Previous snapshot at {url} is not an object")
        if previous:
            return previous
        last_error = None
    if last_error is not None:
        raise last_error
    return {}


def ppa_downloads():
    base = "https://api.launchpad.net/1.0/~paulwoisard/+archive/ubuntu/galleoncore"
    links = set()
    for status in ("Published", "Superseded"):
        query = urllib.parse.urlencode({
            "ws.op": "getPublishedBinaries", "binary_name": "galleoncore",
            "exact_match": "true", "status": status,
        })
        url = f"{base}?{query}"
        while url:
            page = get_json(url)
            links.update(entry["self_link"] for entry in page["entries"])
            url = page.get("next_collection_link")
    if not links:
        raise ValueError("Launchpad returned no GalleonCore binaries")
    total = 0
    for link in links:
        count = get_json(f"{link}?ws.op=getDownloadCount")
        if type(count) is not int or count < 0:
            raise ValueError("Invalid Launchpad download count")
        total += count
    return total


def parse_copr_downloads(page):
    counts = {}
    for row in re.findall(r"<tr\b.*?</tr>", page, re.S):
        text = " ".join(html.unescape(re.sub(r"<[^>]+>", " ", row)).split())
        release = re.search(r"Fedora [\w.]+", text)
        repo = re.search(r"\((\d+) downloads?\)", text)
        if not (release and repo):
            continue
        rpms = re.findall(r"\((\d+)\)\s*\*", text)
        if not rpms:
            raise ValueError(f"No RPM count for {release.group()}")
        counts[release.group()] = sum(map(int, rpms))
    if not counts:
        raise ValueError("COPR download table unavailable")
    return sum(counts.values())


def copr_downloads():
    url = "https://copr.fedorainfracloud.org/coprs/paullux/GalleonCore/"
    request = urllib.request.Request(url, headers={"User-Agent": "GalleonCore-pages/1.0"})
    with urllib.request.urlopen(request, timeout=20) as response:
        return parse_copr_downloads(response.read().decode("utf-8"))


def aur_votes():
    url = "https://aur.archlinux.org/rpc/v5/info?arg[]=galleoncore"
    results = get_json(url).get("results", [])
    if len(results) != 1 or type(results[0].get("NumVotes")) is not int:
        raise ValueError("AUR package information unavailable")
    return results[0]["NumVotes"]


def repository_measurements(previous, today, fetchers):
    previous_history = previous.get("history", [])
    latest = previous_history[-1] if isinstance(previous_history, list) and previous_history else {}
    if not isinstance(latest, dict):
        latest = {}
    prior_repositories = latest.get("repositories", {})
    prior_checked = previous.get("repository_checked_at", {})
    prior_weekly = previous.get("repository_weekly", {})
    prior_period = previous.get("repository_period", {})
    if not isinstance(prior_repositories, dict):
        prior_repositories = {}
    if not isinstance(prior_checked, dict):
        prior_checked = {}
    if not isinstance(prior_weekly, dict):
        prior_weekly = {}
    if not isinstance(prior_period, dict):
        prior_period = {}
    repository_totals = {}
    checked_at = dict(prior_checked)
    weekly = dict(prior_weekly)
    period = dict(prior_period)
    votes = None
    for name, fetch in fetchers:
        prior_value = previous.get("aur_votes") if name == "aur" else prior_repositories.get(name)
        if not daily_refresh_needed(prior_checked.get(name), prior_value, today):
            value = prior_value
        else:
            try:
                value = fetch()
                if type(value) is not int or value < 0:
                    raise ValueError("Invalid download measurement")
                checked_at[name] = today.isoformat()
            except (ValueError, OSError) as error:
                print(f"{name}: {error}")
                value = prior_value
            if name in REPOSITORIES:
                baseline_date = (today - timedelta(days=7)).isoformat()
                baseline = next((item for item in previous_history
                                 if item.get("date") == baseline_date), {})
                old = baseline.get("repositories", {}).get(name)
                old_checked = baseline.get("repository_checked_at", {}).get(name)
                fresh = checked_at.get(name) == today.isoformat()
                # Both providers expose cumulative package download counts.
                weekly[name] = (max(0, value - old) if fresh and old_checked == baseline_date
                                and type(old) is int and type(value) is int else None)
                period[name] = {"from": baseline_date, "to": checked_at.get(name)}
        if name == "aur":
            votes = value
        else:
            repository_totals[name] = value
    return repository_totals, votes, checked_at, weekly, period


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--previous-url", required=True, action="append")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    token = os.environ.get("GITHUB_TOKEN")
    if not token:
        raise ValueError("GITHUB_TOKEN is required")
    releases = []
    for page in range(1, 11):
        url = f"https://api.github.com/repos/Paullux/GalleonCore/releases?per_page=100&page={page}"
        batch = get_json(url, token=token)
        if not isinstance(batch, list):
            raise ValueError("GitHub did not return a release list")
        releases.extend(batch)
        if len(batch) < 100:
            break
    else:
        raise ValueError("More than 1,000 releases; refusing incomplete statistics")
    previous = previous_snapshot(args.previous_url)
    now = datetime.now(timezone.utc)
    repository_totals, votes, checked_at, weekly, period = repository_measurements(
        previous, now.date(), (("ppa", ppa_downloads), ("copr", copr_downloads),
                               ("aur", aur_votes)))
    result = build_stats(releases, repository_totals, votes, previous, now,
                         checked_at, weekly, period)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, separators=(",", ":")) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
