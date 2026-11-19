# Release download stats

`downloads.csv` is a running history of download counts for Objeck release
assets, with one row per asset per snapshot: `snapshot_utc`, `release_tag`,
`asset_name`, `download_count`. The source of truth is GitHub's own per-asset
`download_count` from the Releases API — no extra infrastructure or telemetry
is involved. Counts are cumulative, so per-week deltas come from diffing
successive snapshots.

The `.github/workflows/release-stats.yml` workflow runs the snapshot weekly
and commits the updated CSV. To run it manually:

```
python3 tools/release-stats/snapshot_downloads.py
```

## Repository traffic (private)

GitHub keeps only 14 days of repository traffic (views, clones, referrers, popular
pages), and then it is gone. `snapshot_traffic.py` merges what the API returns into two
files that keep growing, in a **private folder outside the repository**
(`$OBJECK_STATS_DIR`, or `~/.objeck-stats` when that is unset). Unlike `downloads.csv`,
they are never committed, so referrer names and page paths stay private.

- `traffic.csv`: `date,views,view_uniques,clones,clone_uniques`, one row per day. A day
  already in the file is replaced, because the newest day in a response is partial.
- `referrers.csv`: `snapshot_utc,kind,name,count,uniques`, the ten busiest referrers and
  pages of the 14 days ending at each snapshot.

**The traffic endpoints need push access**, which the built-in `GITHUB_TOKEN` of a
workflow does not have, so this is not a repository workflow (a workflow would need a
secret token and would have to publish or upload the history). Run it as yourself, with
the GitHub CLI logged in to an account with push access:

```
python3 tools/release-stats/snapshot_traffic.py --gh
python3 tools/release-stats/test_snapshot_traffic.py   # offline tests
```

Run it at least every two weeks: each response covers 14 days, so weekly runs see every
day twice and one missed run loses nothing. To schedule it weekly on Windows (an
administrator is not needed):

```
schtasks /Create /SC WEEKLY /D MON /ST 09:00 /TN ObjeckTraffic /TR "python <repo>\tools\release-stats\snapshot_traffic.py --gh"
```

and with cron on Linux or macOS: `17 9 * * 1  python3 <repo>/tools/release-stats/snapshot_traffic.py --gh`.

## Release downloads, continued

An optional `--token <github-token>` raises the API rate limit (CI passes the
built-in `GITHUB_TOKEN`); anonymous access works fine for occasional runs.
