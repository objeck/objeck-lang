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

## Repository traffic

GitHub keeps only 14 days of repository traffic (views, clones, referrers, popular
pages), and then it is gone. `snapshot_traffic.py` merges what the API returns into two
files that keep growing:

- `traffic.csv`: `date,views,view_uniques,clones,clone_uniques`, one row per day. A day
  already in the file is replaced, because the newest day in a response is partial.
- `referrers.csv`: `snapshot_utc,kind,name,count,uniques`, the ten busiest referrers and
  pages of the 14 days ending at each snapshot.

The same workflow runs it weekly. Each response covers 14 days, so every day is seen
twice and one missed run loses nothing. **These files are public**, like `downloads.csv`:
they are committed to this repository, so referrer names and page paths in them are
visible to everyone. Delete the workflow step if that is not what you want.

**The traffic endpoints need push access.** The workflow's built-in `GITHUB_TOKEN` cannot
read them, so the step needs a repository secret named `TRAFFIC_TOKEN`: a fine-grained
personal access token limited to this repository with read access to "Administration", or
a classic token with the `repo` scope. Add it under Settings > Secrets and variables >
Actions. Without the secret the step prints a notice and the job carries on.

To run it by hand as yourself (the GitHub CLI logged in to an account with push access):

```
python3 tools/release-stats/snapshot_traffic.py --gh
python3 tools/release-stats/test_snapshot_traffic.py   # offline tests
```

## Release downloads, continued

An optional `--token <github-token>` raises the API rate limit (CI passes the
built-in `GITHUB_TOKEN`); anonymous access works fine for occasional runs.
