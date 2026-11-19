#!/usr/bin/env python3
"""Keep the repository's traffic history, which GitHub only retains for 14 days.

GitHub's traffic API answers with one entry per day for the last 14 days (views
and clones, each with a total and a unique count) and a top-ten list of
referrers and of pages for the same window. Nothing older can be asked for, so
this script merges what the API returns into two CSV files that grow, in a PRIVATE
folder outside the repository ($OBJECK_STATS_DIR, or ~/.objeck-stats when that is
unset), so referrer names and page paths are not published:

  traffic.csv   date,views,view_uniques,clones,clone_uniques
                 one row per day; a day that is already there is replaced, because
                 the newest day in a response is partial and later fills in
  referrers.csv  snapshot_utc,kind,name,count,uniques
                 kind is "referrer" or "path"; one block per snapshot, each
                 covering the 14 days that end at the snapshot

Run weekly, every day appears in two snapshots, so one missed run loses nothing.

The traffic endpoints need push access to the repository, which a workflow's built-in
GITHUB_TOKEN does not have. Run it as someone who has it: with the GitHub CLI logged in
(--gh), or pass a token (a fine-grained token with read access to "Administration" on
this repository, or a classic token with the "repo" scope) through --token. Standard
library only.
"""

import argparse
import csv
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

REPO = "objeck/objeck-lang"
API = "https://api.github.com/repos/" + REPO + "/traffic/"
HERE = Path(os.environ.get("OBJECK_STATS_DIR") or (Path.home() / ".objeck-stats"))
DAILY_COLUMNS = ["date", "views", "view_uniques", "clones", "clone_uniques"]
TOP_COLUMNS = ["snapshot_utc", "kind", "name", "count", "uniques"]
TOP_LIMIT = 10


def fetch(endpoint, token=None, use_gh=False):
    """The parsed JSON of one traffic endpoint, or raises RuntimeError with the reason."""
    if use_gh:
        done = subprocess.run(["gh", "api", "repos/%s/traffic/%s" % (REPO, endpoint)],
                              capture_output=True, text=True)
        if done.returncode != 0:
            raise RuntimeError("gh api %s failed: %s" % (endpoint, done.stderr.strip() or done.stdout.strip()))
        return json.loads(done.stdout)

    request = urllib.request.Request(API + endpoint)
    request.add_header("Accept", "application/vnd.github+json")
    request.add_header("User-Agent", "objeck-traffic-stats")
    if token:
        request.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(request) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        if error.code in (401, 403, 404):
            raise RuntimeError("%s answered HTTP %d: the traffic endpoints need a token with push access "
                               "(Administration read, or the classic repo scope); the workflow's own "
                               "GITHUB_TOKEN cannot read them" % (endpoint, error.code))
        raise RuntimeError("%s answered HTTP %d" % (endpoint, error.code))
    except urllib.error.URLError as error:
        raise RuntimeError("%s could not be fetched: %s" % (endpoint, error.reason))


def day_of(entry):
    """YYYY-MM-DD from an entry's timestamp (2026-11-05T00:00:00Z)."""
    return str(entry["timestamp"])[:10]


def daily_rows(views_json, clones_json):
    """{date: [views, view_uniques, clones, clone_uniques]} from the two daily responses."""
    rows = {}
    for entry in (views_json or {}).get("views", []):
        row = rows.setdefault(day_of(entry), [0, 0, 0, 0])
        row[0], row[1] = int(entry.get("count", 0)), int(entry.get("uniques", 0))
    for entry in (clones_json or {}).get("clones", []):
        row = rows.setdefault(day_of(entry), [0, 0, 0, 0])
        row[2], row[3] = int(entry.get("count", 0)), int(entry.get("uniques", 0))
    return rows


def read_daily(path):
    rows = {}
    if Path(path).exists():
        with open(path, newline="", encoding="utf-8") as handle:
            for record in csv.DictReader(handle):
                rows[record["date"]] = [int(record["views"]), int(record["view_uniques"]),
                                        int(record["clones"]), int(record["clone_uniques"])]
    return rows


def merge_daily(existing, new):
    """Existing days kept, a day in the new response replaces its old row."""
    merged = dict(existing)
    merged.update(new)
    return merged


def write_daily(path, rows):
    with open(path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(DAILY_COLUMNS)
        for date in sorted(rows):
            writer.writerow([date] + rows[date])


def top_rows(snapshot_utc, referrers_json, paths_json):
    """Rows for referrers.csv: the busiest referrers and pages of this snapshot's window."""
    out = []
    for kind, items, key in (("referrer", referrers_json, "referrer"), ("path", paths_json, "path")):
        ranked = sorted(items or [], key=lambda item: (-int(item.get("count", 0)), str(item.get(key, ""))))
        for item in ranked[:TOP_LIMIT]:
            out.append([snapshot_utc, kind, item.get(key, ""), int(item.get("count", 0)), int(item.get("uniques", 0))])
    return out


def append_top(path, rows):
    new_file = not Path(path).exists()
    with open(path, "a", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        if new_file:
            writer.writerow(TOP_COLUMNS)
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--token", default=None, help="GitHub token with push access to the repository")
    parser.add_argument("--gh", action="store_true", help="use the GitHub CLI (gh api) as the logged-in user")
    parser.add_argument("--daily", default=str(HERE / "traffic.csv"))
    parser.add_argument("--top", default=str(HERE / "referrers.csv"))
    args = parser.parse_args()

    if not args.token and not args.gh:
        print("error: pass --token <token with push access> or --gh", file=sys.stderr)
        return 2

    try:
        views = fetch("views", args.token, args.gh)
        clones = fetch("clones", args.token, args.gh)
        referrers = fetch("popular/referrers", args.token, args.gh)
        paths = fetch("popular/paths", args.token, args.gh)
    except RuntimeError as error:
        print("error: " + str(error), file=sys.stderr)
        return 1

    for target in (args.daily, args.top):
        Path(target).parent.mkdir(parents=True, exist_ok=True)

    new = daily_rows(views, clones)
    merged = merge_daily(read_daily(args.daily), new)
    write_daily(args.daily, merged)

    snapshot_utc = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    rows = top_rows(snapshot_utc, referrers, paths)
    append_top(args.top, rows)

    print("traffic.csv: %d day(s) in this response, %d in the file; referrers.csv: %d row(s) appended"
          % (len(new), len(merged), len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
