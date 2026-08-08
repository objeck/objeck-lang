# Auto-updater for Objeck — design

Upgrading Objeck today means downloading an installer and re-running it.
This note designs an in-place upgrade path — check, download, verify, swap —
plus download tracking, without adding server infrastructure.

## Ground truth

- Releases are GitHub Releases produced by `release-build.yml`. Assets are
  named `objeck-<platform>_<version>.<ext>`: `.msi` (windows-x64/arm64),
  `.pkg` (macos-arm64, notarized), `.tgz` (linux-x64/arm64), plus the LSP
  zip. Versions are tags like `v2026.8.0`.
- An install is a self-contained directory (`bin`, `lib`, `app`, `doc`,
  `examples`) — on Windows under `Program Files\Objeck`, on POSIX wherever
  the tarball was unpacked. Nothing outside that directory except PATH and,
  on Windows, the launcher's registry entries and Start-menu links.
- The toolchain already refuses mismatched artifacts: `obr` rejects a
  `lang.obl` built by a different toolchain, so a *partial* upgrade is
  already fatal rather than subtly wrong. The updater must be all-or-nothing.
- GitHub's API already counts asset downloads (`download_count` per release
  asset). Tracking can ride on that with zero new infrastructure.

## Shape: a small native tool, `obu`

A new `core/utils/updater` producing `obu` (`obu.exe`), shipped in `bin/`
beside the other tools. Native rather than an `.obs` program for one hard
reason: the updater must be able to replace `obr` and every `.obl` while it
runs, so it cannot itself run on the VM it is replacing.

Commands:

```
obu check              is a newer release available? (exit 0/1, prints both versions)
obu update             check + download + verify + install
obu update --channel latest|<tag>    pin or roll forward/back to a tag
obu rollback           restore the previous version (one level)
```

The REPL, launcher and `obc -v` can surface "a newer version is available"
by shelling to `obu check --quiet`; none of them grow update logic of
their own.

## Update flow

1. **Check.** `GET https://api.github.com/repos/objeck/objeck-lang/releases/latest`
   (anonymous; 60 req/hour is far above need). Compare `tag_name` against
   `core/shared/version.h`'s version compiled into `obu`. A `--channel <tag>`
   targets a specific release instead.
2. **Download** the platform's asset to a temp directory inside the install
   root (same volume, so the final move is atomic-ish and permissions are
   already proven writable).
3. **Verify before anything is touched.** Two layers:
   - `release-build.yml` gains a `SHA256SUMS` asset per release (trivial CI
     addition); `obu` verifies the digest of what it downloaded.
   - Platform signatures where they exist: the `.msi` is Authenticode-signed
     and the `.pkg` notarized — `obu` additionally runs
     `WinVerifyTrust`/`spctl --assess` rather than trusting transport alone.
   A failed check deletes the download and changes nothing. Integrity comes
   first; a compromised update path is worse than no updater.
4. **Install, all-or-nothing.**
   - **POSIX:** unpack to `<root>/.staging-<ver>`, move current content to
     `<root>/.previous`, move staging into place. Running processes keep
     their open inodes, so a live `obr` is unaffected.
   - **Windows:** files in use cannot be replaced, and `obu.exe` itself lives
     in `bin/`. Standard two-step: `obu` copies itself to `%TEMP%`, re-execs
     the copy (elevating via UAC if the install root needs it — Program
     Files does), which waits for the parent to exit, performs the same
     staged swap, and reports. The `.msi` is *not* run — the payload for
     Windows is the same file set the `.msi` carries, which the release
     workflow already produces as a plain archive for the portable zip; the
     MSI's registry/Start-menu work is redone by `obu` only when it detects
     an MSI-managed install (marker: the registry uninstall key).
   - `.previous` is kept until the next successful update — that is what
     `obu rollback` restores.
5. **Post-check.** Run `<new>/bin/obr --version` (and compile a one-liner
   with the new `obc`); on failure, automatic rollback. This is the same
   "verify by running it" rule the repo's CI applies.

The version check compares the *deployed tree*, not just `obu`'s own build:
`obc -v` output is authoritative for "what is installed", guarding against a
half-swapped tree claiming the new version.

## Download tracking

Two tiers, both optional to operate:

1. **Zero-infrastructure (now):** GitHub already meters every asset.
   `tools/release-stats/` gets a small script (CI cron, weekly) that snapshots
   `download_count` per asset per release into a committed CSV — history over
   time, per platform, for free. The updater needs nothing added: its
   downloads are ordinary asset downloads and are counted with the rest.
2. **If finer grain is ever wanted:** point `obu`'s *check* (not download) at
   a redirect endpoint (e.g. `objeck.org/api/latest` on the playground host,
   which already runs a maintained service) that logs platform + current
   version before 302-ing to the GitHub API response. Opt-out flag
   (`--no-telemetry`, env `OBJECK_UPDATE_NO_PING`), and nothing beyond
   platform + version is ever sent. This tier is deliberately deferred —
   tier 1 answers "are people downloading, and on what platforms" without
   operating anything.

## What the updater must never do

- Modify anything before signature/digest verification passes.
- Leave a tree that mixes versions (the staged swap plus post-check + auto
  rollback exists for exactly this).
- Update the running VM's libraries out from under a long-running `obr`
  (POSIX inode semantics make this safe; on Windows the swap only proceeds
  when `bin` binaries are not in use, else it schedules and reports).
- Auto-update silently. `obu` acts only when invoked; surfacing "an update
  exists" in the REPL is as far as unprompted behavior goes.

## Phasing

1. `obu check` + `SHA256SUMS` in the release workflow + the stats snapshot
   script. Small, independently shippable, immediately useful.
2. `obu update`/`rollback` for the tarball platforms (POSIX first: the swap
   is simple and testable in CI with a fake release).
3. Windows swap with the re-exec dance and MSI-marker handling.
4. Tier-2 tracking endpoint, only if the CSV proves insufficient.

## Risks, in order

- **Windows file locking and elevation.** The re-exec + wait pattern is
  well-trodden but has the most edge cases (install in use by an IDE's LSP
  server — `~/.objeck-lsp` is separate, but a PATH-resolved `obr` may be
  running). Detect-and-defer, never force.
- **Trust chain.** SHA256SUMS published by the same channel as the assets
  protects against corruption, not compromise; the platform-signature checks
  are what protect against substitution. Documenting that honestly matters
  more than pretending the sums alone are a security boundary.
- **The macOS agreement problem.** Notarization has failed before for
  account reasons (v2026.6.3); `obu` must treat "latest release has no pkg
  asset yet" as "no update available", not an error.
