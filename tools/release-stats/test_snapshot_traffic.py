#!/usr/bin/env python3
"""Offline tests for snapshot_traffic.py: parsing, merging and the CSV round trip.

No network and no token. The API shapes below are GitHub's documented responses
for GET /repos/{owner}/{repo}/traffic/views, /clones, /popular/referrers and
/popular/paths.
"""

import csv
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import snapshot_traffic as st  # noqa: E402

VIEWS = {"count": 9, "uniques": 5, "views": [
    {"timestamp": "2026-11-05T00:00:00Z", "count": 4, "uniques": 3},
    {"timestamp": "2026-11-06T00:00:00Z", "count": 5, "uniques": 2},
]}
CLONES = {"count": 30, "uniques": 8, "clones": [
    {"timestamp": "2026-11-06T00:00:00Z", "count": 20, "uniques": 6},
    {"timestamp": "2026-11-07T00:00:00Z", "count": 10, "uniques": 2},
]}
REFERRERS = [{"referrer": "a.org", "count": 7, "uniques": 7}, {"referrer": "b.com", "count": 7, "uniques": 1},
             {"referrer": "c.com", "count": 1, "uniques": 1}]
PATHS = [{"path": "/x", "title": "x", "count": 3, "uniques": 2}]


class DailyRows(unittest.TestCase):
    def test_views_and_clones_join_by_day(self):
        rows = st.daily_rows(VIEWS, CLONES)
        self.assertEqual(sorted(rows), ["2026-11-05", "2026-11-06", "2026-11-07"])
        self.assertEqual(rows["2026-11-05"], [4, 3, 0, 0])     # views only
        self.assertEqual(rows["2026-11-06"], [5, 2, 20, 6])    # both
        self.assertEqual(rows["2026-11-07"], [0, 0, 10, 2])    # clones only

    def test_empty_and_missing_responses(self):
        self.assertEqual(st.daily_rows({"count": 0, "uniques": 0, "views": []}, {"clones": []}), {})
        self.assertEqual(st.daily_rows(None, None), {})
        self.assertEqual(st.daily_rows({}, {}), {})

    def test_a_missing_count_is_zero(self):
        rows = st.daily_rows({"views": [{"timestamp": "2026-11-05T00:00:00Z"}]}, None)
        self.assertEqual(rows["2026-11-05"], [0, 0, 0, 0])


class Merge(unittest.TestCase):
    def test_old_days_are_kept(self):
        merged = st.merge_daily({"2026-10-01": [1, 1, 1, 1]}, {"2026-11-05": [4, 3, 0, 0]})
        self.assertEqual(sorted(merged), ["2026-10-01", "2026-11-05"])

    def test_a_repeated_day_is_replaced_not_added(self):
        # the newest day of a response is partial; the next snapshot has the whole day
        merged = st.merge_daily({"2026-11-07": [1, 1, 0, 0]}, {"2026-11-07": [6, 4, 0, 0]})
        self.assertEqual(merged["2026-11-07"], [6, 4, 0, 0])


class Files(unittest.TestCase):
    def test_round_trip_and_idempotence(self):
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / "traffic.csv")
            rows = st.daily_rows(VIEWS, CLONES)
            st.write_daily(path, st.merge_daily(st.read_daily(path), rows))
            first = Path(path).read_text(encoding="utf-8")
            st.write_daily(path, st.merge_daily(st.read_daily(path), rows))   # the same snapshot again
            self.assertEqual(Path(path).read_text(encoding="utf-8"), first)
            self.assertTrue(first.startswith("date,views,view_uniques,clones,clone_uniques\n"))
            self.assertEqual(first.count("\n"), 4)                              # header + 3 days
            self.assertEqual(st.read_daily(path), rows)

    def test_days_are_written_in_date_order(self):
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / "traffic.csv")
            st.write_daily(path, {"2026-11-07": [1, 1, 1, 1], "2026-10-01": [2, 2, 2, 2]})
            with open(path, newline="") as handle:
                dates = [record["date"] for record in csv.DictReader(handle)]
            self.assertEqual(dates, ["2026-10-01", "2026-11-07"])

    def test_a_missing_file_reads_as_empty(self):
        self.assertEqual(st.read_daily("this-file-does-not-exist.csv"), {})


class Top(unittest.TestCase):
    def test_ranked_by_count_then_name_and_limited(self):
        rows = st.top_rows("2026-11-08T05:00:00Z", REFERRERS, PATHS)
        refs = [row for row in rows if row[1] == "referrer"]
        self.assertEqual([row[2] for row in refs], ["a.org", "b.com", "c.com"])   # tie on 7 broken by name
        self.assertEqual([row[1] for row in rows if row[1] == "path"], ["path"])
        many = [{"referrer": "r%02d" % i, "count": i, "uniques": 1} for i in range(30)]
        self.assertEqual(len([r for r in st.top_rows("t", many, []) if r[1] == "referrer"]), st.TOP_LIMIT)

    def test_append_writes_the_header_once(self):
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / "referrers.csv")
            st.append_top(path, st.top_rows("2026-11-08T05:00:00Z", REFERRERS, PATHS))
            st.append_top(path, st.top_rows("2026-11-15T05:00:00Z", REFERRERS, PATHS))
            lines = Path(path).read_text(encoding="utf-8").splitlines()
            self.assertEqual(lines[0], "snapshot_utc,kind,name,count,uniques")
            self.assertEqual(sum(1 for line in lines if line.startswith("snapshot_utc")), 1)
            self.assertEqual(len(lines), 1 + 2 * 4)

    def test_empty_lists(self):
        self.assertEqual(st.top_rows("t", None, None), [])


if __name__ == "__main__":
    unittest.main(verbosity=1)
