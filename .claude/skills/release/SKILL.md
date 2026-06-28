---
name: release
description: Fully automate an Objeck release — reads version.h, auto-bumps if needed, pre-flight gates, LSP sync, docs update, tag push, GitHub Actions build/sign/publish monitoring, release body, and DigitalOcean playground deploy
allowed-tools: Read Edit Write Bash Grep Glob Skill
argument-hint: "(no arguments — reads version directly from core/shared/version.h)"
---

Fully automate an Objeck release end-to-end. This skill is an **orchestrator**: it delegates heavy lifting to `bump-version` and `update-docs`, then drives the existing GitHub Actions pipeline (`release-build.yml` → `release-publish.yml`) and wraps up by deploying the DigitalOcean-hosted playground over SSH.

## Overview

Objeck releases are triggered by pushing a git tag `v<YYYY.M.R>`. GitHub Actions does parallel builds for Windows x64/ARM64 (MSI + ZIP), Linux x64/ARM64 (TGZ), macOS ARM64 (.pkg, signed + notarized), plus the LSP package. `release-publish.yml` auto-runs on success and handles Windows MSI signing, GitHub Release creation + upload, Sourceforge upload, and API docs deployment to objeck.org.

This skill's job is pure **orchestration** — it runs on any machine (Windows, macOS, Linux) with git, `gh`, and SSH. It does zero compiling, zero signing, zero platform-specific work. All builds, API doc generation, signing, and artifact packaging happen in the cloud via GitHub Actions.

**Invocation**: `/release` takes no arguments. It reads the version from `core/shared/version.h` and checks whether that version's tag already exists:

- **Tag doesn't exist** (user pre-bumped) → use version.h as-is, skip straight to pre-flight gates. No local builds.
- **Tag already exists** (user forgot to bump, or wants a hands-off release) → auto-increment the release counter (e.g. `2026.4.2` → `2026.4.3`), invoke `bump-version` to update all files + rebuild + run regression, commit + push the bump, wait for CI green, then proceed.

Either way, the release summary + changelog bullets are auto-derived from `git log` since the previous tag (step 1b). All release binaries, signing, and artifact packaging happen in GitHub Actions — never locally.

**Refuse to run if**: working tree is dirty, not on `master`, tag already exists, or latest master CI is red. These gates are non-negotiable — **do not offer to skip them**.

**Never attempt mid-release CI fixes.** If a build failure is discovered after the tag is pushed, stop and tell the user. Two releases (v2026.5.0 and v2026.5.1) were broken by attempting on-the-fly CI patches. The correct recovery is: fix on master, delete the bad tag, re-tag.

## Steps

### 1a. Resolve the target version

Read the current version string from `core/shared/version.h`:

```bash
VERSION=$(grep VERSION_STRING core/shared/version.h | sed 's/.*"\([0-9.]*\)".*/\1/')
```

**Sanity check** — parse `$VERSION` as `YEAR.MONTH.RELEASE`. If parsing fails or any segment is missing/non-numeric, abort. Also verify `VER_NUM` is consistent (e.g. `VERSION_STRING="2026.4.3"` ⇒ `VER_NUM=202643`). If they disagree, abort.

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
        core/release/code_doc64.cmd core/release/cov_scan.sh \
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

# Committed docs/api.zip is complete (NOT a stale/broken 3-file zip). release-build.yml's
# "Generate API Docs" job unzips this committed api.zip as the doc base; a broken one
# (generated against stale .obl during the bump — see bump-version step 4) yields 0 HTML
# and fails release-build on every non-windows-x64 target AFTER the tag is pushed. This
# broke v2026.6.4. Catch it here, before tagging:
unzip -l docs/api.zip | grep -c '\.html$'   # must be >= 50 (healthy ~435)
unzip -l docs/api.zip | head                # paths must be 'api/...', not 'api\...'
```

If any gate fails, print the specific failure and stop immediately. Do not propose workarounds.
If `docs/api.zip` is broken, regenerate it (bump-version step 4 note), commit, and restart.

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

### 2. LSP repo update (`../objeck-lsp`)

The LSP repo is a sibling directory: `C:\Users\objec\Documents\Code\objeck-lsp` on this machine. It must be rebuilt against the just-bumped Objeck runtime so the LSP and compiler stay version-compatible. If the sibling directory is missing, abort with instructions to clone it.

**IMPORTANT — LSP repo uses `main` branch, not `master`.** Every git push to the LSP repo must target `main`:
```bash
git push origin HEAD   # or: git push origin main
```
Never `git push origin master` in the LSP repo — it will push to a different (or nonexistent) branch.

**2a. Sync version** — edit `clients/vscode/package.json` so the `"version"` field matches `<VERSION>`. Also grep for any other hardcoded version strings in `server/` and `clients/` and update to match. Check `README.txt` and `docs/install_guide.html` as well.

**2b. Rebuild LSP server + VSIX**:

```bash
cd /c/Users/objec/Documents/Code/objeck-lsp
bash deploy_lsp.sh deploy
```

This runs `build_server.sh`, regenerates `server/objk_apis.json`, and packages the VS Code extension. The script calls `zip` to create `objeck-lsp-<VERSION>.zip`. On Windows, `zip` may not be available in Bash — if `deploy_lsp.sh` fails at the packaging step, create the zip with PowerShell instead:

```powershell
Compress-Archive -Path "C:\Users\objec\Documents\Code\objeck-lsp\*" `
  -DestinationPath "C:\Users\objec\Documents\Code\objeck-lsp\objeck-lsp-<VERSION>.zip" `
  -Force
```

Verify `objeck-lsp-<VERSION>.zip` exists before proceeding.

**2c. Run LSP tests** — On Windows, `.obe` files are Windows PE executables and cannot run in POSIX Bash. Run tests via PowerShell using `obr.exe`:

```powershell
$obr = "C:\Users\objec\Documents\Code\objeck-lang\core\release\deploy-x64\bin\obr.exe"
$lsp_server = "C:\Users\objec\Documents\Code\objeck-lsp\server\objeck_lsp.obe"
$apis_json  = "C:\Users\objec\Documents\Code\objeck-lsp\server\objk_apis.json"

Get-ChildItem "C:\Users\objec\Documents\Code\objeck-lsp\tests" -Filter "test_*.obs" | ForEach-Object {
  $result = & $obr $lsp_server $apis_json diag $_.FullName 2>&1
  if ($LASTEXITCODE -ne 0) { Write-Error "LSP test FAILED: $($_.Name)"; exit 1 }
}
```

If the repo has a dedicated test script (check `scripts/run_tests.*` or `tests/run.*`), prefer that. Abort if any test fails.

**2d. Commit LSP repo**:

```bash
cd /c/Users/objec/Documents/Code/objeck-lsp
git add -A
git commit -m "Release v<VERSION> — rebuild against objeck-lang v<VERSION>

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
git push origin HEAD   # LSP repo uses 'main', not 'master'
```

Do not tag the LSP repo — the main `objeck-lang` tag push handles that via GitHub Actions.

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
  - Replace the `<p>` summary at the top of `<main>` with the new summary.
  - Insert the new `<h3><u>v<VERSION></u></h3>` changelog block.
  - Strip `<u>` from the previously-latest entry.
  - Update the `Download v<VERSION>` button — `docs/web/readme.html` has a hardcoded `<a class="btn btn-primary" ...>Download v<OLD></a>`; bump its text to the new version.
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
        docs/web/readme.html docs/web/index.html
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

### 7. Monitor `release-build.yml`

```bash
# Grab the triggered run ID (wait up to 30 seconds for it to appear)
for i in 1 2 3 4 5 6; do
  RUN_ID=$(gh run list --workflow=release-build.yml --branch="v$VERSION" \
           --limit=1 --json databaseId --jq '.[0].databaseId')
  [ -n "$RUN_ID" ] && break
  sleep 5
done
[ -z "$RUN_ID" ] && { echo "No release-build.yml run triggered — aborting"; exit 1; }

# Stream progress
gh run watch "$RUN_ID" --exit-status
```

`--exit-status` makes `gh run watch` return non-zero if the workflow fails. Abort on failure — do **not** re-trigger without the user's explicit direction (deleting and re-pushing a tag is destructive and risks confusing the Sourceforge uploader).

Expected duration: ~45 minutes. Report milestones as they complete.

### 8. Monitor `release-publish.yml`

`release-publish.yml` auto-triggers on `release-build.yml` success. Find its run and watch it the same way:

```bash
for i in 1 2 3 4 5 6; do
  PUB_ID=$(gh run list --workflow=release-publish.yml --limit=1 \
           --json databaseId,createdAt --jq '.[0].databaseId')
  [ -n "$PUB_ID" ] && break
  sleep 10
done
gh run watch "$PUB_ID" --exit-status
```

Expected duration: ~15 minutes.

### 9. Verify published artifacts

List the release assets and confirm all expected files are present:

```bash
gh release view "v$VERSION" --json assets --jq '.assets[].name'
```

**Signed installers** (required — release is blocked without these):
- `objeck-windows-x64_<VERSION>.msi`
- `objeck-windows-arm64_<VERSION>.msi`
- `objeck-macos-arm64_<VERSION>.pkg`

If Windows MSIs are missing, the workflow secret `WINDOWS_CERT_BASE64`/`WINDOWS_CERT_PASSWORD` is not set — tell the user and abort.
If macOS `.pkg` is missing, the Apple signing secrets are not set — tell the user and abort.

**VM binary sanity** — prior releases (v2026.5.0, v2026.5.1) shipped without the `obr` VM executable because a CI dependency was missing. Spot-check the Linux x64 archive to confirm `obr` is present:

```bash
gh release download "v$VERSION" --pattern "objeck-linux-x64_*.tgz" -D /tmp/check
tar -tzf /tmp/check/objeck-linux-x64_*.tgz | grep '/bin/obr$'
# must print a match; if empty, the VM is missing — abort and investigate CI
```

If `obr` is absent from the archive, the release is broken. Tell the user; do not publish. The correct fix is to identify the missing CI dependency, add it to `release-build.yml` on master, delete the tag, and re-release.

### 10. Update the GitHub release body

The `release-drafter.yml` workflow accumulates PR-title based changelog entries as PRs merge into master. Combine that draft with the curated bullets from `docs/readme.txt` for this version:

```bash
# Pull the release-drafter draft body
DRAFTER_BODY=$(gh api repos/:owner/:repo/releases --jq \
  '.[] | select(.draft == true and .tag_name == "v'"$VERSION"'") | .body')

# Extract the curated section for v<VERSION> from docs/readme.txt
# (lines from "v<VERSION>" header up to the next "v..." header)

# Construct final body:
#   ## What's New in v<VERSION>
#   <summary paragraph>
#
#   <curated bullets from docs/readme.txt>
#
#   ## Changes (auto-generated from PRs)
#   <release-drafter body>
#
#   ## Downloads
#   See the Assets section below for installers and archives.

gh release edit "v$VERSION" --notes-file /tmp/release_body.md --draft=false
```

If no draft exists yet (release-publish already published), use `gh release edit` to replace the body. Do not delete the assets attached by release-publish.

### 11. Deploy the DigitalOcean playground

> `$PLAYGROUND_HOST` is the playground deploy target — held out-of-band (not
> committed). Export it before running, e.g. `export PLAYGROUND_HOST=playground.objeck.org`
> (or the VPS IP if DNS is down). Deploy uses root SSH and assumes your key is
> already authorized on the host.

SSH to the playground VPS and run its update script:

```bash
ssh -o StrictHostKeyChecking=accept-new root@$PLAYGROUND_HOST \
  'bash /opt/playground/repo/programs/web-playground/deploy/update.sh'
```

The script does: `git pull origin master`, update Python venv, rebuild the sandbox Docker image, restart the systemd `playground` service, and run a `curl -sf http://localhost:8000/api/health` check.

**Known playground git issues** — the server can accumulate local modifications (`.obl` files regenerated in-place, `config.py` touched) and untracked files (Windows DLLs or other artifacts that were later committed to master). If `git pull` fails, recover with:

```bash
ssh root@$PLAYGROUND_HOST 'cd /opt/playground/repo && \
  chmod -R u+w . && \
  git stash && \
  git clean -fd && \
  git pull origin master && \
  git stash pop'
```

Then re-run `update.sh`. If it still fails, report the SSH output verbatim and stop.

**Version sanity check** — after the remote update, confirm the version string matches:

```bash
curl -fsS https://playground.objeck.org/api/health | jq -r '.version'
# must equal v<VERSION>  (note: health endpoint returns "v<VERSION>" with the 'v' prefix)
```

If `playground.objeck.org` is unreachable, try `https://$PLAYGROUND_HOST/api/health` as a fallback.

### 12. Final report

Print a single consolidated summary:

- **Released**: `v<OLD_VERSION>` → `v<VERSION>`
- **Summary**: `<SUMMARY>`
- **GitHub release**: https://github.com/objeck/objeck-lang/releases/tag/v<VERSION>
- **Release build**: run #<RUN_ID>, duration, conclusion
- **Release publish**: run #<PUB_ID>, duration, conclusion
- **Signed installers verified**: Windows x64 MSI ✓, Windows ARM64 MSI ✓, macOS ARM64 .pkg ✓
- **VM binary verified**: `obr` present in Linux x64 archive ✓
- **LSP repo**: commit SHA pushed to objeck-lsp `main`
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
- If the VM binary check fails (step 9), the release is broken. Tell the user to fix the CI dependency, delete the tag, and re-release. Do not mark the release as complete.
- If the playground SSH deploy fails, the GitHub release is already live. Tell the user the release is public but playground is stale; suggest re-running the SSH step manually.

## Things this skill deliberately does NOT do

- **Auto-bumps only when needed.** If `version.h` points at an already-released tag, the skill auto-increments and invokes `bump-version`. If `version.h` is already at an unreleased version (user pre-bumped), no bump happens.
- **Does not compile, link, or build anything.** No `MSBuild`, no `make`, no `deploy_windows.cmd`, no `update_version.sh`, no `obc`, no `obr`. All builds happen in GitHub Actions.
- **Does not sign anything locally.** All signing happens in GitHub Actions using repo secrets.
- **Does not generate API docs locally.** GitHub Actions generates `api.zip` and deploys to objeck.org.
- **Does not use `git add -A`.** Explicit file lists only.
- **Does not amend commits or force-push.** Every commit is new.
- **Does not delete tags.** If recovery requires tag deletion, the user drives that step.
- **Does not skip any step with `--no-verify` / `skip_sourceforge` / `skip_docs_deploy`.** If a feature is required per user policy, its failure is a release blocker.
- **Does not attempt mid-release CI fixes.** If a build fails after the tag is pushed, stop and report. Two releases were broken by on-the-fly CI patches.
