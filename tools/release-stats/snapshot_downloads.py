#!/usr/bin/env python3
"""Snapshot per-asset download counts for objeck-lang GitHub Releases.

Pages through the GitHub Releases API and appends one row per release asset
(snapshot_utc, release_tag, asset_name, download_count) to downloads.csv,
building download history over time from GitHub's own counters. CI runs this
weekly via .github/workflows/release-stats.yml; it can also be run manually
with no dependencies beyond the Python 3 standard library.
"""

import argparse
import csv
import json
import sys
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

API_URL = "https://api.github.com/repos/objeck/objeck-lang/releases"
CSV_COLUMNS = ["snapshot_utc", "release_tag", "asset_name", "download_count"]


def fetch_releases(token=None):
    """Return all releases, following pagination until an empty page."""
    releases = []
    page = 1
    while True:
        url = f"{API_URL}?per_page=100&page={page}"
        request = urllib.request.Request(url)
        request.add_header("Accept", "application/vnd.github+json")
        request.add_header("User-Agent", "objeck-release-stats")
        if token:
            request.add_header("Authorization", f"Bearer {token}")
        with urllib.request.urlopen(request) as response:
            batch = json.load(response)
        if not batch:
            break
        releases.extend(batch)
        page += 1
    return releases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--token", default=None,
                        help="GitHub API token (optional; anonymous access works)")
    parser.add_argument("--csv", default=str(Path(__file__).parent / "downloads.csv"),
                        help="CSV file to append to (default: downloads.csv beside this script)")
    args = parser.parse_args()

    try:
        releases = fetch_releases(args.token)
    except (urllib.error.URLError, urllib.error.HTTPError) as error:
        print(f"error: failed to fetch releases: {error}", file=sys.stderr)
        return 1

    snapshot_utc = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    rows = []
    for release in releases:
        for asset in release.get("assets", []):
            rows.append([snapshot_utc, release["tag_name"],
                         asset["name"], asset["download_count"]])
    rows.sort(key=lambda row: (row[1], row[2]))

    csv_path = Path(args.csv)
    write_header = not csv_path.exists()
    with csv_path.open("a", newline="", encoding="utf-8") as csv_file:
        writer = csv.writer(csv_file)
        if write_header:
            writer.writerow(CSV_COLUMNS)
        writer.writerows(rows)

    print(f"Appended {len(rows)} rows to {csv_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
