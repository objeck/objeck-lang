---
name: release
description: Fully automate an Objeck release — reads version.h, auto-bumps if needed, pre-flight gates, LSP sync, docs update, tag push, GitHub Actions build/sign/publish monitoring, release body, and DigitalOcean playground deploy
allowed-tools: Read Edit Write Bash Grep Glob Skill
argument-hint: "(no arguments — reads version directly from core/shared/version.h)"
---

Fully automate an Objeck release end-to-end. This skill is an **orchestrator**: it delegates heavy lifting to `bump-version` and `update-docs`, then drives the existing GitHub Actions pipeline (`release-build.yml` → `release-publish.yml`) and wraps up by deploying the DigitalOcean-hosted playground over SSH.

## Overview

Objeck releases are triggered by pushing a git tag `v<YYYY.M.R>`. GitHub Actions does parallel builds for Windows x64/ARM64 (MSI + ZIP), Linux x64/ARM64 (TGZ), macOS ARM64 (.pkg, signed + notarized), plus the LSP package. `release-publish.yml` auto-runs on success and handles GitHub Release creation + upload, Sourceforge upload, and API docs deployment to objeck.org. It does NOT sign: the certificate is on a SafeNet eToken, so signing is a local post-publish step (`tools\cicd\sign_release.cmd`).

This skill's job is pure **orchestration** — it runs on any machine (Windows, macOS, Linux) with git, `gh`, and SSH. It does zero compiling and zero platform-specific work. Builds, API doc generation and artifact packaging happen in the cloud; SIGNING does not — it needs the physical token and is run afterwards.

**Invocation**: `/release` takes no arguments. It reads the version from `core/shared/version.h` and checks whether that version's tag already exists:

- **Tag doesn't exist** (user pre-bumped) → use version.h as-is, skip straight to pre-flight gates. No local builds.
- **Tag already exists** (user forgot to bump, or wants a hands-off release) → auto-increment the release counter (e.g. `2026.4.2` → `2026.4.3`), invoke `bump-version` to update all files + rebuild + run regression, commit + push the bump, wait for CI green, then proceed.

Either way, the release summary + changelog bullets are auto-derived from `git log` since the previous tag (step 1b). Release binaries and artifact packaging happen in GitHub Actions; signing is the one step that must happen locally.

**Refuse to run if**: working tree is dirty, not on `master`, tag already exists, or latest master CI is red. These gates are non-negotiable — **do not offer to skip them**.

**Never attempt mid-release CI fixes.** If a build failure is discovered after the tag is pushed, stop and tell the user. Two releases (v2026.5.0 and v2026.5.1) were broken by attempting on-the-fly CI patches. The correct recovery is: fix on master, delete the bad tag, re-tag.

## Steps

### 1a. Resolve the target version

Read the current version string from `core/shared/version.h`:

```bash
VERSION=$(grep VERSION_STRING core/shared/version.h | sed 's/.*"\([0-9.]*\)".*/\1/')
```

**Sanity check** — parse `$VERSION` as `YEAR.MONTH.RELEASE`. If parsing fails or any segment is missing/non-numeric, abort. Also verify `VER_NUM` is consistent (e.g. `VERSION_STRING="2026.4.3"` ⇒ `VER_NUM=202643`). If they disagree, abort.

**`update_version.ps1` must agree with `version.h`** — `deploy_windows.cmd` re-runs `update_version.ps1`, which REGENERATES `version.h` from its own hardcoded version. If the two disagree, Windows silently builds a different version from Linux and macOS, and its binaries then reject the committed `.obl` set on `VER_NUM` — every program fails, on the user's machine rather than in CI:

```bash
# sed, not 'grep -P': the release machine's Git Bash reports
# "grep: -P supports only unibyte and UTF-8 locales" and yields empty strings,
# which would make this gate silently compare "" against "" and pass.
PS1=core/release/update_version.ps1
PS1_VER="$(sed -n 's/^\$year_end = "\([0-9]*\)".*/\1/p' $PS1).$(sed -n 's/^\$month_end = "\([0-9]*\)".*/\1/p' $PS1).$(sed -n 's/^\$version = "\([0-9]*\)".*/\1/p' $PS1)"
[ "$PS1_VER" = "$VERSION" ] || { echo "update_version.ps1 says $PS1_VER, version.h says $VERSION"; exit 1; }
```

`release-build.yml` also rewrites the `.ps1` from the tag as a second line of defence, but fix it here so the repository is not left inconsistent.

**Tag collision check** — if `git tag -l "v$VERSION"` is non-empty, the version in `version.h` is already released. Auto-increment the release counter until a free tag is found:

```bash
YEAR=$(echo "$VERSION" | cut -d. -f1)
MONTH=$(echo "$VERSION" | cut -d. -f2)
REL=$(echo "$VERSION" | cut -d. -f3)

while [ -n "$(git tag -l "v${YEAR}.${MONTH}.${REL}")" ]; do
  REL=$((REL + 1))
done
VERSION="${YEAR}.${MONTH}.${REL}"
```

If the version was incremented, **invoke `bump-version $VERSION`** (via the Skill tool) to update all version files, rebuild locally for verification, and run regression. `bump-version` handles `update_version.ps1`, `deploy_windows.cmd`, `update_version.sh`, and the regression suite. If it fails, abort — do not continue.

After `bump-version` completes, commit and push the bump:

```bash
git add core/shared/version.h core/release/update_version.ps1 \
        core/release/code_doc64.cmd \
        programs/deploy/util/readme/readme.json \
        programs/web-playground/backend/app/config.py \
        README.md \
        core/compiler/vs/objeck.rc core/vm/vs/objeck.rc \
        core/debugger/vs/objeck.rc core/repl/vs/objeck.rc \
        core/utils/launcher/vs/builder/objeck.rc \
        core/lib/*.obl docs/api.zip
git commit -m "Bump version to $VERSION

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
git push origin master
```

Then wait for `ci-build.yml` to complete on the bump commit before proceeding to the pre-flight gates (poll `gh run list` until `completed/success`).

If the version was **not** incremented (tag didn't exist, user pre-bumped), skip `bump-version` entirely — just use version.h as-is.

Print the resolved version:

```
Release target:
  version.h: v<ORIGINAL>
  Releasing: v<VERSION>  (auto-bumped: yes/no)
```

### 1. Pre-flight gates (all must pass)

Run the following and **abort** on the first failure:

```bash
# Clean working tree
git status --porcelain    # must be empty

# On master, up to date with origin
git rev-parse --abbrev-ref HEAD         # must be "master"
git fetch origin master
git rev-list --count HEAD..origin/master  # must be 0 (no unpulled commits)
git rev-list --count origin/master..HEAD  # must be 0 (no unpushed commits)

# Tag must not already exist (should be guaranteed by 1a, but double-check)
git tag -l "v$VERSION"                         # must be empty
git ls-remote --tags origin "refs/tags/v$VERSION"  # must be empty

# Latest master CI is green (including the bump commit if auto-bumped)
gh run list --workflow=ci-build.yml --branch=master --limit=1 \
  --json conclusion,status --jq '.[0] | .status + "/" + .conclusion'
# must return "completed/success"

# Committed docs/api.zip is complete (NOT a stale/broken 3-file zip). A broken one
# (generated against stale .obl during the bump — see bump-version step 4) fails
# release-build on every non-windows-x64 target AFTER the tag is pushed. This broke
# v2026.6.4. Catch it here, before tagging.
#
# WHERE it actually breaks (verified 2026-08-19 — the "Generate API Docs" job is NOT
# the consumer; it contains no unzip and builds docs/api fresh with mkdir -p, since
# docs/api is untracked). The committed zip is consumed by the DEPLOY SCRIPTS:
#   deploy_posix.sh:154         -> linux-x64, linux-arm64
#   deploy_macos_arm64.sh:141   -> macos-arm64
#   deploy_windows.cmd:774      -> windows-ARM64 only (cross-compiled, cannot run
#                                  ARM64 code_doc on an x64 host)
# windows-x64 alone regenerates its own docs via code_doc64.cmd. That is precisely
# why a bad committed zip takes out "every non-windows-x64 target" — look in the
# BUILD jobs, not the doc job.
#
# It also means the API docs shipped INSIDE every archive except windows-x64 are the
# committed zip, so a wrong version stamp is visible in the user's install, not just
# in the repo.
unzip -l docs/api.zip | grep -c '\.html$'   # must be >= 50 (healthy ~435)
unzip -l docs/api.zip | head                # paths must be 'api/...', not 'api\...'

# CONTENT, not just structure: every generated page stamps the version it was built
# for in its footer. The two checks above are structural and pass on a perfectly
# well-formed zip built against the WRONG version -- which is how the committed
# api.zip stamped v2026.8.0 while shipping as the docs for v2026.8.1 AND v2026.8.2.
# Nobody noticed for three releases because 439 HTML files with 'api/' paths is
# exactly what a healthy zip looks like.
#
# Pick the page by LISTING the zip, never by guessing a filename. There is no
# api/api.system.html: naming a file that is not there makes unzip -p print
# nothing, grep match nothing, and an empty-vs-empty comparison PASS on any
# version -- the same silent-pass this gate exists to catch.
PAGE=$(unzip -l docs/api.zip | awk '/\.html$/{print $4; exit}')
STAMP=$(unzip -p docs/api.zip "$PAGE" | grep -o 'class="version">v[0-9.]*' | head -1 | sed 's/.*>//')
[ -n "$STAMP" ] || { echo "api.zip: NO version stamp found in $PAGE -- treat as FAILED"; exit 1; }
[ "$STAMP" = "v$VERSION" ] || { echo "api.zip stamps $STAMP, expected v$VERSION"; exit 1; }
echo "api.zip stamp OK: $STAMP"
```

If any gate fails, print the specific failure and stop immediately. Do not propose workarounds.
If `docs/api.zip` is broken **or stamped with the wrong version**, regenerate it
(bump-version step 4 note), commit, and restart. Regenerating locally is
`code_doc64.cmd x64 deploy` from `core/release` against a deploy tree whose `.obl` are
already at the new version, then zip with `7z`/`zip` (forward-slash paths), never
PowerShell `Compress-Archive`.

Note the cloud build *does* pass the release version to `code_doc` and commits the
result back as `chore: update api.zip for v<VERSION> [skip ci]` -- so a wrong stamp in
the committed file is not simply "CI never refreshes it".

**Unresolved, with the evidence recorded so it is not rediscovered.** Three consecutive
CI commits each committed a zip stamped **v2026.8.0**:

```
a757830453  "chore: update api.zip for v2026.8.2"  -> zip stamps v2026.8.0
930dd19836  "chore: update api.zip for v2026.8.1"  -> zip stamps v2026.8.0
7dba61d324  "chore: update api.zip for v2026.8.1"  -> zip stamps v2026.8.0
```

So either the version handed to `code_doc` was wrong on those runs, or the commit step
(which stashes the built zip to `/tmp` and copies it back around a git operation)
restored a stale file. Not diagnosed. Until it is, **verify the stamp every release**
rather than assuming the cloud produced a correct one.

### 1b. Derive the release summary

Do **not** ask the user for a summary — synthesize it from the commit history between the previous release tag and `HEAD`:

```bash
# Find the previous release tag (highest semver-sorted v*)
PREV_TAG=$(git tag -l 'v*' | sort -V | tail -1)

# Get commit subjects since that tag
git log --no-merges --pretty=format:'%s' "${PREV_TAG}..HEAD"
```

Read those commit subjects and compose a **single-line summary** (≤ 80 chars) that captures the 2–4 most impactful changes as a comma-separated list. Think about the reader: a user scanning the GitHub release list. Lead with the headline feature; include bug-fix themes only if no features were added.

**Examples of good auto-derived summaries**:
- `"DAP debugger hover, release automation, LSP setup polish"`
- `"JIT register cache (~3x), AI library refresh, S2F JIT fix, editor support"`
- `"DTLS support, LTO optimization, MSVC warning cleanup"`

**Examples of bad ones** (do not produce these):
- `"Various bug fixes and improvements"` — vague, say what specifically
- `"Release v2026.5.0"` — restating the version is not a summary
- `"Fix debugger.cpp segfault in EvaluateForDap when cur_frame is null"` — too low-level, not user-facing

If the commit log between tags is empty (nothing merged since last release), abort — there is nothing to release. If the commits are purely internal refactors with no user-visible change, abort and tell the user the release has no customer value.

Store the derived summary as `$SUMMARY` for use in steps 3, 5, 10, and 12. **Show the summary to the user once it's derived**, so they can see what's going out — but do not pause for confirmation (if they wanted to override it, they would have started from `update-docs` manually).

### 2. LSP pre-flight (in-repo — nothing is built locally)

The LSP lives in this repository at `tools/lsp`. The standalone `objeck-lsp`
sibling repo is **DEPRECATED** — do not build, commit or push it. A stale clone
may still exist on the release machine; ignore it.

`release-build.yml` builds the whole LSP in the cloud from `tools/lsp`: it runs
`build_server.sh`, `npm install`, packages the `.vsix`, and **sets the extension
version from the tag** (`jq '.version = $v' package.json`). So there is no local
rebuild, no version edit, and no second repo to push.

Only one thing needs a human before tagging, because the release ships it as
committed and never regenerates it:

**2a. Is `objk_apis.json` current?** It backs LSP hover and completion. Two
copies are committed and BOTH must be regenerated together:

- `tools/lsp/server/objk_apis.json`
- `tools/lsp/clients/vscode/server/objk_apis.json` — the one packaged into the
  `.vsix`, so this is what users actually get

Check staleness by comparing against the library sources it documents:

```bash
git log -1 --format='%h %ci' -- tools/lsp/server/objk_apis.json
git log -1 --format='%h %ci' -- core/compiler/lib_src/
diff -q tools/lsp/server/objk_apis.json tools/lsp/clients/vscode/server/objk_apis.json
```

If `lib_src` is newer, or the two copies differ, regenerate and commit both:

```bash
cd tools/lsp/server/doc_json && ./gen_json.sh "$VERSION"     # gen_json.cmd on Windows
```

Both failure modes have shipped before: a new public class missing entirely
(`Web.HTTP.HeaderCheck`), and the two copies drifting so the `.vsix` shipped an
older index than the server (the unsigned `Int` methods were absent from the
VS Code copy while present in the server copy). Neither breaks a build — the
docs are just silently wrong for the release's own new APIs.

**2b. Sanity-check the generator's source list.** `gen_json.cmd`/`gen_json.sh`
hard-code the `lib_src` files they document, so a newly added library is
invisible until listed. Confirm the counts match:

```bash
ls core/compiler/lib_src/*.obs | wc -l    # must equal the count in gen_json.cmd
```

**2c. Run the LSP tests** (they also gate in the `tools` CI job):

```bash
python tools/lsp/tests/run_lsp_tests.py
```

Abort if any test fails.

### 2d. Re-check `readme.json.in` against everything that landed after the bump

`readme.json.in` carries the release notes that ship **inside** the archive, and its
`title` + `features` block is hand-written. `bump-version` writes that block, so it
describes the tree **as of the bump commit** -- every fix merged between the bump and
the tag is silently missing from it. `update_version.ps1` only substitutes
`@VERSION@`/`@YEAR@`, so nothing downstream notices.

This is not hypothetical: at v2026.8.3 the bump landed mid-release and six later
commits (two compiler null dereferences, the REPL brace hang, two stream-format leaks,
the wrapped entry points, the GC atomic) were absent from the shipped README while
present in every other notes file.

```bash
BUMP=$(git log --format='%H' -1 --grep="Bump version to $VERSION")
[ -n "$BUMP" ] && git log --no-merges --oneline "$BUMP..HEAD"   # must all be represented
```

If anything is missing, add it to the `v<VERSION>` block in `readme.json.in`, refresh
`title` if the release's headline changed, then **regenerate `readme.json`** (the two
must never drift) and validate it parses:

```bash
python -c "import io,json; src=io.open('programs/deploy/util/readme/readme.json.in',encoding='utf-8',newline='').read(); out=src.replace('@VERSION@','$VERSION').replace('@YEAR@','${VERSION%%.*}'); io.open('programs/deploy/util/readme/readme.json','w',encoding='utf-8',newline='').write(out); json.loads(out)"
```

Both files are committed in step 5.

### 3. Delegate to `update-docs`

Invoke the `update-docs` skill with the `$VERSION` from step 1a and the `$SUMMARY` derived in step 1b:

```
Skill: update-docs
Args: <VERSION> "<$SUMMARY>"
```

In addition to the one-line summary, `update-docs` also needs per-entry bullet points for the changelog. Derive these the same way you derived `$SUMMARY` — group the `git log --no-merges "${PREV_TAG}..HEAD"` subjects into thematic bullets (one per major area: JIT, LSP/DAP, libraries, bug fixes). Pass the full bullet list to `update-docs` as additional context so it can populate `README.md`, `docs/readme.html`, `docs/readme.txt`, and `CHANGELOG.md` without prompting the user.

Confirm all four files have the new version entry at the top before proceeding.

### 4. Sync `docs/web/` to the new release

The `docs/web/` directory contains the website content served from objeck.org. It is similar to but not identical to the top-level `docs/` files — headers, navigation links, and meta tags are different, but the changelog content should match.

**`update-docs` does NOT touch `docs/web/`** — this step is the release skill's responsibility.

**4a. Sync `docs/web/readme.html`** — copy the `<main>` changelog body from the freshly updated `docs/readme.html` into `docs/web/readme.html`, preserving `docs/web/readme.html`'s own `<head>`, `<nav>`, and absolute `objeck.org` URLs. Specifically:
  - **Do NOT replace the first `<p>` in `<main>`.** Unlike `docs/readme.html`, that
    paragraph in `docs/web/readme.html` is the site's standing intro line ("AI,
    networking, and vision — built in…"), not a per-release summary. Overwriting it
    with the release summary silently destroys site copy. This file gets the changelog
    block and the download button only.
  - Insert the new `<h3><u>v<VERSION></u></h3>` changelog block.
  - Strip `<u>` from the previously-latest entry.
  - Update the `Download v<VERSION>` button — `docs/web/readme.html` has a hardcoded
    `<a class="btn btn-primary" ...>Download v<OLD></a>`; bump its text to the new version.
    **This is a gate, not a nicety** — it had been left on `v2026.8.1` through the whole of
    v2026.8.2 and was still wrong when v2026.8.3 started, so the site's own download button
    named a superseded release. Verify with a grep that must come back empty:

    ```bash
    grep -o 'Download v2026\.[0-9]*\.[0-9]*' docs/web/readme.html | grep -v "Download v$VERSION"
    ```
  - Keep `docs/web/readme.html`'s existing nav bar (`<a href="index.html">Home</a>` etc.).

**4b. Update `docs/web/index.html`** — grep for any hardcoded `v202x.x.x` version strings (not `/releases/latest` or `/api/latest/` links). Update only those hits. Also update the Download button text if it contains a version number. Most releases need minimal changes here.

### 5. Commit docs changes to master

Before committing, pull to incorporate any commits that landed while you were working (CI's api.zip `[skip ci]` commit is the most common):

```bash
cd /c/Users/objec/Documents/Code/objeck-lang
git fetch origin master
git stash   # stash any unstaged edits
git pull --rebase origin master
git stash pop
```

If `git pull --rebase` reports a conflict on `docs/api.zip` (binary file), accept the remote version — it was committed by CI and is authoritative:

```bash
git checkout --theirs docs/api.zip
git add docs/api.zip
git rebase --continue
```

Then stage and commit — enumerate paths explicitly, never `git add -A`:

```bash
git add README.md CHANGELOG.md docs/readme.html docs/readme.txt \
        docs/web/readme.html docs/web/index.html \
        programs/deploy/util/readme/readme.json programs/deploy/util/readme/readme.json.in \
        docs/api.zip
git status    # verify what's staged; omit unchanged files
git commit -m "Release v<VERSION> — <SUMMARY>

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
git push origin master
```

### 6. Tag and push

```bash
git tag "v$VERSION"
git push origin "v$VERSION"
```

This is the point of no return — the tag push triggers `release-build.yml` on GitHub Actions.

### 7-8. Monitor build and publish: ONE command

```bash
tools/cicd/watch_release.sh <VERSION>
```

Watches `release-build.yml` through `release-publish.yml`, then confirms the
per-platform binary gates actually RAN (skipped is not failed -- both are guarded by
`if: runner.os ...`, so each platform job runs one and skips the other; matching only
the matrix jobs keeps it quiet on a healthy run).

**Read its exit code, do not eyeball the text:**

| exit | meaning |
|---|---|
| 0 | finished green |
| 1 | genuinely failed -- a conclusion other than success |
| 2 | **still running when the budget ran out. NOT a failure.** |

That distinction is the whole point. During v2026.8.3 an ad-hoc watcher printed
`RELEASE-BUILD FAILED` for a completely healthy run because its 65-minute poll budget
expired while the `windows-arm64` leg took its normal ~70 minutes. A false red invites
exactly the panic response this process forbids -- deleting a pushed tag or patching CI
mid-release, which is what broke v2026.5.0 and v2026.5.1.

Note it polls `status` explicitly rather than using `gh run watch`, which exits 0 on a
network error. An empty query result is a failed query, not a finished run.

Expected: build ~45-70 min (the arm64 leg dominates; vcpkg rebuilds
`opencv4:arm64-windows` from source on a cache miss), publish ~15 min.

**CI pushes to master while this runs.** The "Generate API Docs" job commits
`chore: update api.zip for v<VERSION> [skip ci]`, so any push you make during or after
the build is rejected as non-fast-forward. Rebase onto it; the tag is unaffected.

### 9. Write the release body

`release-publish.yml` sets a generic body with no changelog. Replace it. Combine the
curated bullets for this version from `docs/readme.txt` with the `release-drafter` draft:

```bash
DRAFTER_BODY=$(gh api repos/:owner/:repo/releases --jq   '.[] | select(.draft == true and .tag_name == "v'"$VERSION"'") | .body')
```

Structure: `## What's New in v<VERSION>` + summary + curated bullets, then the merged-PR
list, then Installation / Verification / Downloads. Pass the file to `post_release.sh
--body`, which publishes it and then gates it (gate 5). Do not hand-run `gh release edit`
and skip the gate.

State the signature status only from gate 3's output. Reporting "signed installers
verified" on the strength of the MSI existing is exactly how three consecutive release
reports were wrong.

### 10-11b. Post-publish: ONE command

Everything after `release-publish.yml` goes green is a single pipeline, and it runs as
one script. It used to be four prose sections here, which meant the commands were
retyped by hand every release -- and re-broken the same ways every release.

```bash
tools/cicd/post_release.sh <VERSION> --sign --body /tmp/release_body.md --playground
```

Exit 0 only when all six gates pass. Read-only unless you pass the flags.

| Gate | What it proves | Why it exists |
|---|---|---|
| 1 assets | not a draft; both MSIs, the `.pkg` and `SHA256SUMS` present | — |
| 2 binaries | `obc obr obd obi obb obu` in **every** POSIX archive | v2026.5.0/5.1 shipped with no `obr`; v2026.8.0 shipped with no `obu` on any platform while the notes made it the headline feature |
| 3 signatures | real `Get-AuthenticodeSignature` on the **published** files | v2026.4.0-v2026.8.2 all shipped unsigned while the notes claimed otherwise |
| 4 `SHA256SUMS` | manifest matches the published bytes; regenerates + re-proves if signing invalidated it | signing rewrites the MSIs, so v2026.8.3's manifest described pre-signing bytes and every user's verification would have FAILED |
| 5 body | every advertised filename is a real asset; no `$VARS`, no `compare/v...`, no `[x]()` | the published body named `objeck-lsp-X.zip` (hyphen) when the asset is `objeck-lsp_X.zip`, and linked `compare/v...vX` |
| 6 playground | `/api/health` reports the tag | the version is a tracked constant the deploy does not auto-bump |

`--sign` needs the SafeNet eToken plugged in and prompts for its password (once per MSI
unless single-logon is on). Signing cannot happen in CI -- the key is non-exportable and a
hosted runner has no USB port -- so this is a required human step, not optional polish.
Write the body to a file first (step 9 above) and pass it with `--body`.

**If the script reports a failure, read it before believing it.** Three of today's
"failures" were the harness, not the release: a watcher calling a slow build FAILED after
exhausting its poll budget, `tar` reading a Windows `C:/...` path as a remote host and
declaring every binary missing, and an empty-href grep written across two lines so it
matched `)` in ordinary prose. Each is now fixed *in the script* with a comment naming the
incident. A gate whose failure mode is indistinguishable from the defect it hunts is worse
than no gate.

### 12. Final report

Print a single consolidated summary:

- **Released**: `v<OLD_VERSION>` → `v<VERSION>`
- **Summary**: `<SUMMARY>`
- **GitHub release**: https://github.com/objeck/objeck-lang/releases/tag/v<VERSION>
- **Release build**: run #<RUN_ID>, duration, conclusion
- **Release publish**: run #<PUB_ID>, duration, conclusion
- **Installer signatures**: report the real `Get-AuthenticodeSignature` / `pkgutil` status per installer — never "verified" on the basis of the file existing
- **Shipped binaries verified**: `obc obr obd obi obb obu` present in every POSIX archive ✓
- **LSP**: built in-cloud from `tools/lsp`; `.vsix` version set from the tag; `objk_apis.json` current ✓
- **Playground**: deployed to $PLAYGROUND_HOST, health=OK, version=v<VERSION>
- **Sourceforge upload**: status from release-publish.yml
- **API docs**: deployed to objeck.org/api/latest

## Failure handling

At every stage, **fail fast** and report exactly which step failed with the raw tool output. Do not attempt to undo partial state — recovery is the user's call. Specifically:

- If the pre-flight gates fail, no changes have been made; tell the user what's blocking and stop.
- If `bump-version` fails during auto-bump (step 1a), the working tree has uncommitted local file changes. Tell the user, do not revert.
- If CI doesn't go green after auto-bump, the bump commit is on master but the release stops. Tell the user to investigate.
- If LSP tests fail, the LSP repo has uncommitted changes. Report and stop.
- If `git push origin master` fails (another commit landed), tell the user to pull-rebase and restart. Do not auto-rebase.
- If `git push origin v<VERSION>` fails, the local tag exists but is not pushed. Tell the user; do not auto-delete the local tag.
- If `release-build.yml` fails, the tag is already pushed. Tell the user the options: (a) fix on master, delete the tag, re-push; (b) re-run via `gh run rerun`. Do **not** attempt to patch CI mid-release.
- If `release-publish.yml` fails, build artifacts still exist. Suggest `gh workflow run release-publish.yml -f version=<VERSION> -f run_id=<RUN_ID>`. Do not act automatically.
- If the binary check fails (`post_release.sh` gate 2), the release is broken. Tell the user to fix the CI dependency, delete the tag, and re-release. Do not mark the release as complete.
- If the playground SSH deploy fails, the GitHub release is already live. Tell the user the release is public but playground is stale; suggest re-running the SSH step manually.

## Things this skill deliberately does NOT do

- **Auto-bumps only when needed.** If `version.h` points at an already-released tag, the skill auto-increments and invokes `bump-version`. If `version.h` is already at an unreleased version (user pre-bumped), no bump happens.
- **Does not compile, link, or build anything.** No `MSBuild`, no `make`, no `deploy_windows.cmd`, no `update_version.sh`, no `obc`, no `obr`. All builds happen in GitHub Actions.
- **Signs locally, and ONLY locally.** This previously read "does not sign anything locally; all
  signing happens in GitHub Actions using repo secrets" — flatly contradicting the Overview, and
  wrong. The certificate lives on a SafeNet eToken: the private key is non-exportable and a hosted
  runner has no USB port, so CI signing is **impossible, not merely unconfigured**. Believing
  otherwise is exactly why every Windows MSI from v2026.4.0 through v2026.8.1 shipped unsigned
  while `signtool` ran and failed on every artifact. `post_release.sh --sign` is a required human
  step. Do not add signing to any workflow.
- **Does not generate API docs locally** — but the committed `docs/api.zip` is an INPUT, not only
  an output. Only `windows-x64` regenerates its own docs; `deploy_posix.sh`,
  `deploy_macos_arm64.sh` and `deploy_windows.cmd`'s ARM64 branch all unzip the committed file
  straight into the shipped `doc/` tree. A stale one therefore ships to four of five platforms,
  which is how v2026.8.0's docs went out inside v2026.8.1 and v2026.8.2. Hence the stamp gate in
  step 1.
- **Does not use `git add -A`.** Explicit file lists only.
- **Does not amend commits or force-push.** Every commit is new.
- **Does not delete tags.** If recovery requires tag deletion, the user drives that step.
- **Does not skip any step with `--no-verify` / `skip_sourceforge` / `skip_docs_deploy`.** If a feature is required per user policy, its failure is a release blocker.
- **Does not attempt mid-release CI fixes.** If a build fails after the tag is pushed, stop and report. Two releases were broken by on-the-fly CI patches.
