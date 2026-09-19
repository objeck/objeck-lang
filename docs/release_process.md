# Objeck Language Release Process

**Version:** 2.2
**Last Updated:** 2026-09-19

This document is an overview of how an Objeck release is built and published with GitHub Actions, and of the steps that still happen by hand. The authoritative step-by-step procedure, with every gate, is the release skill: [`.claude/skills/release/SKILL.md`](../.claude/skills/release/SKILL.md).

## 📋 Table of Contents

1. [Overview](#overview)
2. [Prerequisites](#prerequisites)
3. [Quick Release Guide](#quick-release-guide)
4. [Detailed Workflow](#detailed-workflow)
5. [Manual Steps](#manual-steps)
6. [Troubleshooting](#troubleshooting)
7. [Manual Fallback](#manual-fallback)

---

## Overview

Building, packaging and publishing are automated with GitHub Actions: pushing a `vYYYY.M.P` tag starts them, and the GitHub Release is published about 25 minutes later with warm caches, or up to about 80 when the windows-arm64 leg has to rebuild OpenCV from source. Some steps are not automated: verifying every platform before the tag, Windows code signing, the playground deploy and the objeck.org API docs upload. See [Manual Steps](#manual-steps).

### What's Automated

✅ **Build Process**
- Windows x64/ARM64 (MSI + ZIP)
- Linux x64/ARM64 (TGZ)
- macOS ARM64 (`.pkg` installer and notarized `.zip`; a `.tgz` is still published so that older copies of `obu` can self-update)
- LSP package (ZIP, including the VS Code `.vsix`)

✅ **Code Signing (macOS only)**
- Developer ID signing of the macOS binaries and `.pkg`
- Notarization of the `.pkg` and `.zip`

✅ **Distribution**
- GitHub Releases (automatic upload, with a `SHA256SUMS` manifest)
- Sourceforge (mirrors the GitHub release through a GitHub webhook)

✅ **Documentation**
- API documentation generation (a tag build commits it to master as `docs/api.zip`)

❌ **Not Automated** (see [Manual Steps](#manual-steps))
- Windows MSI signing (the certificate is on a hardware token)
- Playground deploy
- API documentation upload to objeck.org

---

## Prerequisites

### One-Time Setup (Already Done)

The release workflows use these GitHub Secrets (repository settings):

| Secret Name | Purpose | Required |
|-------------|---------|----------|
| `APPLE_CERTIFICATE_BASE64`, `APPLE_CERTIFICATE_PASSWORD` | macOS Developer ID Application certificate | ✅ Yes |
| `APPLE_INSTALLER_CERT_BASE64`, `APPLE_INSTALLER_CERT_PASSWORD` | macOS Developer ID Installer certificate | ✅ Yes |
| `KEYCHAIN_PASSWORD` | Temporary CI keychain | ✅ Yes |
| `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD` | macOS notarization | ✅ Yes |
| `SOURCEFORGE_SSH_KEY`, `SOURCEFORGE_USERNAME` | Sourceforge SFTP upload | No — declared manual |
| `PLAYGROUND_HOST`, `PLAYGROUND_SSH_KEY` | Playground deploy | No — declared manual |
| `OBJECK_ORG_SSH_KEY`, `OBJECK_ORG_USER` | objeck.org API docs deploy | No — declared manual |
| `NTFY_TOPIC` | Push notification when a build fails | Optional |

Windows signing has no secret: it is done locally with a hardware token (see [SIGNING.md](../SIGNING.md)).

The `RELEASE_MANUAL_STEPS` repository **variable** (currently `sourceforge,playground,docs`) names the publish steps handled outside CI. `release-publish.yml` runs each of those jobs when its secrets are set, skips it when the step is listed in the variable, and fails it otherwise. `tools/cicd/check_release_config.sh` checks all of this before a tag.

**Note:** `GITHUB_TOKEN` is automatically provided by GitHub Actions.

### Developer Requirements

- Git command line tools and an authenticated GitHub CLI (`gh`)
- Write access to the repository
- Ability to create and push tags
- For signing: the Windows release machine, with the SafeNet eToken and the Windows SDK's `signtool`
- For the manual publish steps: SSH access to the playground host and upload access to objeck.org, both held outside the repository

---

## Quick Release Guide

### Pre-Release Checklist

Before tagging, ensure the following are updated (the `bump-version` and `update-docs` skills do most of it):

1. **Version string** in `core/shared/version.h` (`VER_NUM` and `VERSION_STRING`), and the same version in `core/release/update_version.ps1`, which `deploy_windows.cmd` regenerates `version.h` from
2. **Release notes** in `CHANGELOG.md`, `README.md`, `docs/readme.txt`, `docs/readme.html`, `docs/web/readme.html` and `programs/deploy/util/readme/readme.json.in` (regenerate `readme.json` from it)
3. **Download URLs** in `README.md` Quick Start section
4. **Web playground** version in `programs/web-playground/backend/app/config.py` (`objeck_version`). NOT `frontend/index.html` -- its version tag is an empty span filled at runtime from `/api/health`, so editing it does nothing
5. **LSP** (in this repository, under `tools/lsp`) — if `core/compiler/lib_src` changed, regenerate both copies of `objk_apis.json` (`tools/lsp/server/objk_apis.json` and `tools/lsp/clients/vscode/server/objk_apis.json`) with `tools/lsp/server/doc_json/gen_json.sh`; the release build does the rest
6. **Clean working tree** — no uncommitted changes, `.gitignore` up to date

### Creating a New Release

The release skill runs these steps with their gates; this is the outline.

1. **Pass the release train:** at the exact commit to be tagged, every target passes `tools/cicd/verify_platform.sh` / `tools\cicd\verify_platform.cmd`, a `release-build.yml` dispatch from a branch (a dry run) is green, and `tools/cicd/check_release_config.sh` and `tools/cicd/check_open_issues.sh <VERSION>` pass.

2. **Ensure your local repository is up to date:**
   ```bash
   git checkout master
   git pull origin master
   ```

3. **Create and push a version tag:**
   ```bash
   # Format: vYYYY.M.P (e.g., v2026.2.1)
   git tag v2026.2.1
   git push origin v2026.2.1
   ```

4. **Wait for automation:**
   - GitHub Actions automatically triggers both build and publish
   - Monitor progress at: https://github.com/objeck/objeck-lang/actions, or with `tools/cicd/watch_release.sh <VERSION>` (exit 0 = green, 1 = failed, 2 = still running when it stopped watching, which is not a failure)
   - Builds take 15–70 minutes; the windows-arm64 leg is the long one when its vcpkg cache misses
   - Publishing auto-triggers on build success (~10 minutes)

5. **Finish by hand:** sign the Windows installers and replace the generic GitHub Release body with the release notes (`tools/cicd/post_release.sh <VERSION> --sign --body <file>`), deploy the playground and upload the API docs. See [Manual Steps](#manual-steps).

6. **Verify the release:**
   - GitHub: https://github.com/objeck/objeck-lang/releases
   - Sourceforge: https://sourceforge.net/projects/objeck/files/
   - API Docs: https://objeck.org/api/latest/
   - Playground: https://playground.objeck.org/api/health

---

## Detailed Workflow

### Step 1: Release Build (`release-build.yml`)

**Trigger:** Git tag matching `v*.*.*` (e.g., `v2026.2.1`), or a manual dispatch from a branch, which is a dry run: it cannot publish and does not push `docs/api.zip`

**What Happens:**
1. **Version extraction** from git tag
2. **Version file updates** (automatic)
3. **Parallel platform builds:**
   - Windows x64 (MSI + ZIP)
   - Windows ARM64 (MSI + ZIP; cross-compiled on the x64 runner)
   - Linux x64 (TGZ)
   - Linux ARM64 (TGZ)
   - macOS ARM64 (`.pkg` and `.zip`, signed and notarized, plus the `.tgz`)
   - LSP package (ZIP)

   Every leg except Windows ARM64 then compiles and runs a small program with the toolchain it is about to ship, and checks that it reports the tag's version.
4. **API documentation generation** (a tag build commits the result to master as `docs/api.zip`)
5. **macOS install test:** installs the `.pkg` on clean `macos-14` and `macos-26` runners; a failure fails the run, so nothing is published
6. **Artifact uploads** (retained for 7 days)

**Duration:** 15–70 minutes

**Monitoring:**
```bash
# View workflow status
gh run list --workflow=release-build.yml

# Watch live logs
gh run watch
```

### Step 2: Release Publish (`release-publish.yml`)

**Trigger:** Automatic when a Release Build of a `vYYYY.M.P` tag succeeds; builds of branches and of pre-release tags such as `v2026.9.2-rc1` are skipped (also supports manual dispatch)

**What Happens:**
1. **Download build artifacts**
2. **Binary renaming** with version numbers
3. **Signature check** of the Windows MSIs: it only reports, since CI cannot sign (see [Manual Steps](#manual-steps))
4. **GitHub Release creation:**
   - Release notes generation
   - Binary uploads, with a generated `SHA256SUMS`
   - Tag association
5. **Sourceforge upload** (runs when its secrets are set; currently declared manual)
6. **Playground deploy** (likewise; currently declared manual)
7. **API documentation deployment** (likewise; currently declared manual)

**Duration:** ~10 minutes

**Automatic Trigger:** The publish workflow runs automatically when a tag's Release Build completes successfully.

**Manual Trigger (fallback):**
```bash
# Get the run ID from the release build
RUN_ID=$(gh run list --workflow=release-build.yml --limit 1 --json databaseId --jq '.[0].databaseId')

# Trigger publish workflow
gh workflow run release-publish.yml \
  -f version=2026.2.1 \
  -f run_id=$RUN_ID
```

Or use the GitHub Actions UI:
1. Go to **Actions** → **Release Publish**
2. Click **Run workflow**
3. Enter version and run ID
4. Click **Run workflow**

**Post-Publish:** The generated GitHub Release body has no changelog. Replace it with release notes matching the format of previous releases (see `docs/readme.txt` for content) by passing the file to `post_release.sh --body`, which publishes it and checks that every file it names is a real asset.

---

## Manual Steps

These happen after `release-publish.yml` goes green. `tools/cicd/post_release.sh <VERSION>` checks the outcome of each (assets, binaries in every archive, signatures, `SHA256SUMS`, the release body, the playground). Without `--sign`, `--body` or `--playground` it changes nothing, except that it regenerates and re-uploads a `SHA256SUMS` that no longer matches the published files.

### 1. Sign the Windows installers

The certificate is on a SafeNet eToken, so CI cannot sign. On the Windows release machine, with the token plugged in:

```bash
tools/cicd/post_release.sh <VERSION> --sign --body <release-notes.md>
```

`--sign` runs `tools/cicd/sign_release.cmd`, which downloads the published MSIs, signs and verifies them, uploads them back and regenerates `SHA256SUMS` (signing changes the bytes); the token asks for its password. `post_release.sh` then re-checks the signatures and the manifest against the published files. Details: [SIGNING.md](../SIGNING.md).

### 2. Deploy the playground

Run `programs/web-playground/deploy/update.sh <VERSION>` on the playground host over SSH, or use the `deploy-playground` skill; `post_release.sh --playground` does the same. It is done when https://playground.objeck.org/api/health reports `v<VERSION>` and a program run through the playground reports the same version from `System.Runtime->GetVersion()`.

### 3. Upload the API docs to objeck.org

The tag build commits the generated docs to master as `docs/api.zip` (it is also the run's `api-docs` artifact). Upload the contents of its `api/` folder to objeck.org's `/api/latest/`, the only path the site serves; there are no per-version directories. It is done when https://objeck.org/api/latest/index.html shows `v<VERSION>` in its footer.

Sourceforge is declared manual too, but needs nothing: it mirrors the GitHub release through a webhook.

---

## Troubleshooting

### Build Failures

**Problem:** A platform build fails

**Solution:**
1. Check the workflow logs: https://github.com/objeck/objeck-lang/actions
2. Review the specific step that failed
3. Common issues:
   - **Dependency installation failure** → Cached packages may be corrupted. Re-run workflow to refresh cache.
   - **Compilation error** → Fix code issues and push to master, then re-tag.
   - **Test failure** → Fix tests and re-push.

**Re-running:**
```bash
# Delete the tag
git tag -d v2026.2.1
git push origin :refs/tags/v2026.2.1

# Fix issues, commit, push

# Re-create tag
git tag v2026.2.1
git push origin v2026.2.1
```

### Code Signing Issues

**Problem:** `sign_release.cmd` fails, or `post_release.sh` reports an installer as unsigned

**Solution:**
1. Plug in the SafeNet eToken and check that its client software sees it
2. Check certificate expiration and renewal: `sign_release.cmd` selects the certificate by `THUMBPRINT`, which must match the one on the token (the comment above it has the command that prints the current value)
3. Check that `SIGNTOOL` in `sign_release.cmd` points at an installed Windows SDK
4. Verify timestamp server is accessible (`http://timestamp.sectigo.com`)

`tools/cicd/check_release_signatures.ps1 <VERSION>` reports the signature status of the published MSIs at any time.

### Upload Failures

**Problem:** A Sourceforge, playground or API docs job fails

**Solution:**
1. If the job says its credentials are not configured, its secrets are unset and the step is not listed in `RELEASE_MANUAL_STEPS`: set the secrets, or declare it manual (`gh variable set RELEASE_MANUAL_STEPS --body '<existing>,<step>'`)
2. Verify SSH keys are valid and not expired
3. Check network connectivity from GitHub Actions
4. Verify remote directories exist and have write permissions
5. Can be skipped using workflow inputs if needed

**Skip uploads:**
```bash
gh workflow run release-publish.yml \
  -f version=2026.2.1 \
  -f run_id=$RUN_ID \
  -f skip_sourceforge=true \
  -f skip_playground=true \
  -f skip_docs_deploy=true
```

### Artifact Not Found

**Problem:** Release publish can't find build artifacts

**Solution:**
1. Ensure the release build completed successfully
2. Verify the run ID is correct
3. Check artifact retention (7 days by default)

**Finding the correct run ID:**
```bash
gh run list --workflow=release-build.yml --limit 5
```

---

## Manual Fallback

If the automated process fails completely, you can build and publish by hand.

### Emergency Manual Release

1. **Build locally:**
   ```bash
   # Windows
   cd core/release
   deploy_windows.cmd x64 deploy
   deploy_windows.cmd arm64 deploy

   # Linux
   ./deploy_posix.sh x64 deploy
   ./deploy_posix.sh arm64 deploy

   # macOS
   ./deploy_macos_arm64.sh deploy
   ```

2. **Sign MSI files** (with the eToken plugged in; select the certificate by the thumbprint in `tools/cicd/sign_release.cmd`, since `/a` can pick another certificate in the store):
   ```cmd
   signtool sign /tr http://timestamp.sectigo.com /td sha256 /fd sha256 /sha1 <thumbprint> setup.msi
   ```

3. **Rename binaries:**
   ```bash
   # Build rename tool
   obc -src programs/deploy/util/deploy_rename.obs -lib misc
   obr deploy_rename.obe <directory> <version>
   ```

4. **Generate `SHA256SUMS`** over the renamed, signed files (`sha256sum * > SHA256SUMS`) and publish it with them

5. **Upload manually:**
   - GitHub Releases: https://github.com/objeck/objeck-lang/releases/new
   - Sourceforge: Use SFTP client
   - objeck.org: Use rsync/scp

---

## Version Numbering

Objeck uses calendar versioning: `YYYY.M.P`

- **YYYY**: Year (e.g., 2026)
- **M**: Month (1-12, no leading zero)
- **P**: Release number within the month, starting at 0

**Examples:**
- `2026.2.0` - First release of February 2026
- `2026.2.1` - Second release of February 2026
- `2026.12.0` - First release of December 2026

**Git Tags:**
Always prefix with `v`: `v2026.2.1`

---

## Continuous Integration

Every push to `master`, and every pull request against it, triggers the CI build workflow (`ci-build.yml`), which:

1. Builds all 5 platforms in parallel
2. Runs test suites
3. Runs regression tests
4. Generates build artifacts (retained 7 days)

**Purpose:** Catch issues early before release

**Monitoring:**
- Status badges on README
- GitHub Actions dashboard
- A failure on `master` opens or updates a tracking issue, and sends a push notification when `NTFY_TOPIC` is set

---

## Release Checklist

Use this checklist for each release:

- [ ] All tests passing on master (CI green)
- [ ] Version number updated in `core/shared/version.h` and `core/release/update_version.ps1`
- [ ] Release notes updated in `CHANGELOG.md`, `README.md`, `docs/readme.txt`, `docs/readme.html`, `docs/web/readme.html`, `readme.json.in`
- [ ] Download URLs in `README.md` point to new version
- [ ] Web playground version updated (`backend/app/config.py`, `objeck_version`)
- [ ] Both copies of `objk_apis.json` regenerated if `core/compiler/lib_src` changed
- [ ] `check_release_config.sh` and `check_open_issues.sh` pass
- [ ] Every target verified at the commit to be tagged (`verify_platform`), and a release build dry run is green
- [ ] Git tag created and pushed (`git tag vX.Y.Z && git push origin vX.Y.Z`)
- [ ] Release build workflow completed successfully
- [ ] Release publish workflow completed (auto-triggered)
- [ ] Windows MSIs signed and `SHA256SUMS` regenerated (`post_release.sh --sign`)
- [ ] GitHub Release body updated with proper release notes (`post_release.sh --body`)
- [ ] GitHub Release verified with all platform assets
- [ ] Playground deployed and reporting the new version
- [ ] Sourceforge mirror shows the release
- [ ] API docs uploaded to objeck.org (`/api/latest/` shows the new version)
- [ ] Downloads tested on at least one platform

---

## Support

- **Issues:** https://github.com/objeck/objeck-lang/issues
- **Step-by-step procedure:** `.claude/skills/release/SKILL.md`
- **CI/CD Architecture:** See `docs/CI_CD.md`
- **Code Signing:** See `SIGNING.md`
- **Workflow Files:** `.github/workflows/`

---

## Changelog

| Version | Date | Changes |
|---------|------|---------|
| 2.2 | 2026-09-19 | Windows signing is a local post-publish step; added the manual steps (signing, playground, API docs), the release train and `RELEASE_MANUAL_STEPS`; macOS ships `.pkg` + `.zip`; points at the release skill for the procedure |
| 2.1 | 2026-02-26 | Added pre-release checklist, clarified auto-trigger, LSP update step |
| 2.0 | 2026-02-10 | Fully automated CI/CD release process |
| 1.0 | 2024-xx-xx | Manual release process |
