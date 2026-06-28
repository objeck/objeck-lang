---
name: bump-version
description: Bump the Objeck release version across all platform files, rebuild on Windows and Linux, and run regression tests
allowed-tools: Read Edit Write Bash Grep Glob
argument-hint: "[YYYY.M.R] e.g. 2026.5.0"
---

Bump the Objeck version to `$ARGUMENTS` across all files and do a full cross-platform rebuild.

## Version Format

Objeck uses `YEAR.MONTH.RELEASE` (e.g. `2026.4.0`, `2026.5.1`). The last number is the release count within that month (starting at 0).

If no argument is provided, read the current version from `core/shared/version.h` (the `VERSION_STRING` line) and ask the user what the new version should be.

## Steps

### 1. Validate the version

Parse `$ARGUMENTS` as `YEAR.MONTH.RELEASE`. All three parts must be present. Confirm with the user before proceeding.

### 2. Update `update_version.ps1`

Edit `core/release/update_version.ps1` — change the three variables at the top:
```powershell
$year_end = "YEAR"
$month_end = "MONTH"  
$version = "RELEASE"
```

> **CRITICAL — bump `$version` here every release.** `update_version.ps1` is the
> **only** script that regenerates `core/shared/version.h` (from `version.in`);
> Linux's `update_version.sh` does **not** touch `version.h`. If `$version` is
> stale, the Windows build stamps `obc` with the old `VER_NUM` and it then
> **cannot load the newly-committed `.obl` libraries** ("different version of the
> tool chain" — Windows CI fails, and `version.h` keeps reverting locally). This
> exact mismatch (a `202660` obc against `202661` libs) bit the 2026.6.1 release.

### 3. Run `update_version.ps1` (generates version.h + .rc files)

```bash
cd core/release && powershell -ExecutionPolicy Bypass -File update_version.ps1
```

This generates from `.in` templates:
- `core/shared/version.h` (from `version.in`)
- `core/release/code_doc64.cmd` (from `code_doc64.in`)
- `core/release/cov_scan.sh` (from `cov_scan.in`)
- `programs/deploy/util/readme/readme.json` (from `readme.json.in`)
- Windows `.rc` resource files for compiler, VM, debugger, REPL, launcher

> **Curate `readme.json.in` by hand.** Only `@VERSION@`/`@YEAR@` are templated —
> the `title` and the `features` array are hand-maintained. Before generating,
> add a new `{ "version": "v<VERSION>", "items": [...] }` block at the top of
> `features` and set `title` to a one-line summary of the release. If you skip
> this, the generated `readme.json` ships the new version number over the
> previous release's feature list (a visible glitch in the deploy README).
>
> Likewise, if a library was added/removed, sync the obc list in `code_doc64.in`
> (and the lib-build lists in `update_version.sh` / `update_version_arm.sh` and
> the CI `Rebuild libraries (Windows)` step) so docs and builds stay in step.

> **Also bump the web-playground version constant by hand.**
> `programs/web-playground/backend/app/config.py` hard-codes
> `objeck_version: str = "v<VERSION>"` (the string the playground header and
> `/api/health` report). It is **not** templated from `version.in`, so a release
> that forgets it ships a playground reporting the previous version even after a
> successful deploy. This bit 2026.6.1 — the playground served `v2026.6.0` until
> the constant was bumped and re-deployed.

> **Also bump the README "Latest Release" badge by hand.** `README.md` (line ~15)
> uses a **static** shields.io badge:
> `https://img.shields.io/badge/release-v<VERSION>-blue`. It was switched from the
> dynamic `github/v/release` endpoint because that endpoint intermittently
> rendered `unable to select next github token from pool` (a shields.io
> server-side error when its GitHub API token pool is exhausted). The static badge
> never calls the API, but must be bumped to `v<VERSION>` every release or it
> shows the previous version.

### 4. Full Windows build via `deploy_windows.cmd`

This must run from a VS Developer Command Prompt or have `VCINSTALLDIR` set. Run:
```bash
cd core/release && cmd.exe //c "deploy_windows.cmd x64"
```

This rebuilds the entire MSVC solution (compiler, VM, debugger, REPL, launcher), all native libraries (crypto, lame, diags, ODBC, ONNX, OpenCV), generates API docs, and creates the deploy directory.

> **CRITICAL — `docs/api.zip` must be generated AFTER the `.obl` libraries are at the
> new version.** `deploy_windows.cmd` regenerates `docs/api.zip` by running `code_doc`
> against the `.obl` already in `deploy-x64/lib` — it does **NOT** rebuild the `.obl`
> itself. If those libs are still the *previous* version (e.g. the bump is split — Windows
> binaries bumped here but the `.obl` rebuilt later/on Linux per steps 5–6, or on another
> machine), `code_doc` hits *"compiled with a different version of the tool chain"*,
> writes **zero HTML**, and produces a **broken `docs/api.zip` containing only the 3 style
> files**. A broken api.zip is silent locally but **fails the release** — the cloud
> `release-build.yml` *"Generate API Docs"* step unzips the committed `docs/api.zip` as the
> doc base (it relies on it; the Linux path does not copy fresh `code_doc` output into
> `docs/api`), so 0 HTML → *"API docs incomplete: only 0 HTML files (expected 50+)"* →
> cascading `Verify required binaries` failures on every non-windows-x64 target. This
> broke v2026.6.4. **So: if the `.obl` are rebuilt separately/after, REGENERATE `docs/api.zip`
> once the new `.obl` are in `deploy-x64/lib`** (re-run `deploy_windows.cmd`, or run the
> `code_doc` doc-gen + `7z a -tzip api.zip api/*` from `deploy-x64/doc`). Build the zip with
> `7z`/`zip` (forward-slash paths), **not** PowerShell `Compress-Archive` (writes backslash
> entries that Linux `unzip` mangles). **Always verify before committing** (see step 8):
> `unzip -l docs/api.zip | grep -c '\.html$'` must be ≥ 50.

**If `deploy_windows.cmd` cannot run** (no VS environment), fall back to rebuilding just compiler and VM:
```bash
cd core/compiler/vs && MSBuild compiler.vcxproj -p:Configuration=Release -p:Platform=x64 -t:Rebuild
cd core/vm/vs && MSBuild vm.vcxproj -p:Configuration=Release -p:Platform=x64 -t:Rebuild
```

### 5. Full Linux build via `update_version.sh`

Run on Linux or WSL. This rebuilds sys_obc, lang.obl, obc, and all standard
libraries (including `web_server.obl` — added 2026.6.1; keep the lib list here in
sync with `update_version.sh` / `update_version_arm.sh`). Note it does **not**
regenerate `version.h` (see step 2). From the repo root:
```bash
wsl -d Ubuntu -- bash -c 'cd "$REPO"/core/compiler && bash update_version.sh'   # $REPO = /mnt/c/.../objeck-lang
```

**Important:** Use single quotes around the bash -c argument to avoid `\r` line ending issues.

If WSL is not available, note that the user needs to run `update_version.sh` on Linux/macOS.

### 6. Copy rebuilt libraries to deploy directory

After the Linux build, the `.obl` files in `core/lib/` are the authoritative rebuilt copies:
```bash
cp core/lib/*.obl core/release/deploy-x64/lib/
```

### 7. Run regression tests

**Windows:**
```bash
cmd.exe //c "cd /d <repo>\programs\regression & <repo>\programs\regression\run_regression.cmd x64"
```

**Linux (WSL):**
Copy Linux obc/obr to deploy-x64/bin temporarily, then:
```bash
wsl -- bash -c 'cd <repo>/programs/regression && bash run_regression.sh x64'
```
Restore Windows obc.exe/obr.exe afterward.

The `core_opencv` test may fail on WSL (missing native .so) — that's expected.

### 8. Verify

- Read `core/shared/version.h` and confirm `VER_NUM` and `VERSION_STRING` match
- Check regression test results: all should pass (except opencv on WSL)
- Check `update_version.ps1` variables match the new version
- **Verify `docs/api.zip` is complete** (a broken one silently fails the release — see step 4):
  `unzip -l docs/api.zip | grep -c '\.html$'` must be **≥ 50** (a healthy build is ~435).
  If it's ~0–3, the api.zip was generated against stale `.obl`; rebuild it after the new
  `.obl` are in `deploy-x64/lib` and re-verify. Also confirm forward-slash paths
  (`unzip -l docs/api.zip | head` shows `api/...`, not `api\...`).

### 9. Report

Show the user:
- Old version → New version
- Windows build result (warnings/errors)
- Linux build result (warnings/errors)
- Regression test results (pass/fail counts)
- List of files modified
- Suggest: `git add` and `git commit -m "Bump version to X.Y.Z and rebuild all libraries"` (but do NOT commit automatically)
