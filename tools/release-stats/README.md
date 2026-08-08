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

An optional `--token <github-token>` raises the API rate limit (CI passes the
built-in `GITHUB_TOKEN`); anonymous access works fine for occasional runs.
