# Objeck Language CI/CD Architecture

**Version:** 1.2
**Last Updated:** 2026-09-19

This document describes the technical architecture of the Objeck Language continuous integration and continuous deployment (CI/CD) system.

## 📋 Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Workflows](#workflows)
4. [Secrets Management](#secrets-management)
5. [Build Matrix](#build-matrix)
6. [Caching Strategy](#caching-strategy)
7. [Security](#security)
8. [Cost Analysis](#cost-analysis)
9. [Maintenance](#maintenance)

---

## Overview

The Objeck Language CI/CD system is built entirely on **GitHub Actions**, providing:

- ✅ **Zero cost** (public repository = unlimited build minutes)
- ✅ **Parallel builds** across 6 platforms
- ✅ **macOS code signing and notarization** in CI. Windows installers are signed locally afterwards with a hardware token, which CI cannot hold (see [SIGNING.md](../SIGNING.md))
- ✅ **Multi-destination distribution** (GitHub Releases, which Sourceforge mirrors; the playground deploy and the objeck.org API docs upload are currently manual)
- ✅ **Release automation** from a tag (about 25-80 minutes to a published GitHub Release)

### Design Goals

1. **Eliminate manual work** - One git tag triggers everything
2. **Minimize build time** - Parallel builds + aggressive caching
3. **Maximize reliability** - Automated testing at every step
4. **Zero cost** - Leverage free GitHub Actions for public repos
5. **Easy maintenance** - Clear separation of concerns, modular design

---

## Architecture

### Three-Tier Workflow Structure

```
┌─────────────────────────────────────────────────────────────────┐
│                    CI Build (ci-build.yml)                      │
│  Trigger: Every push/PR to master                               │
│  Purpose: Fast feedback, catch issues early                     │
│  Duration: ~30 minutes                                          │
│  Platforms: Windows x64/ARM64, Linux x64/ARM64, macOS ARM64     │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ Tag pushed (v*.*.*)
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                Release Build (release-build.yml)                │
│  Trigger: Git tag (v2026.2.1) or manual                         │
│  Purpose: Full production builds with installers                │
│  Duration: 15-70 minutes (parallel)                             │
│  Platforms: Windows x64/ARM64, Linux x64/ARM64, macOS ARM64, LSP│
│  Outputs: MSI, ZIP, TGZ, PKG, API docs                          │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ Automatic on success (tag builds only)
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│              Release Publish (release-publish.yml)              │
│  Trigger: Automatic when a tag build succeeds, or manual        │
│  Purpose: Rename, checksum, distribute (no signing)             │
│  Duration: ~10 minutes                                          │
│  Actions: GitHub Release, SHA256SUMS, other deploys (see below) │
└─────────────────────────────────────────────────────────────────┘
```

After publishing, the Windows installers are signed, the playground is deployed and the API docs are uploaded to objeck.org by hand; see [release_process.md](release_process.md#manual-steps).

---

## Workflows

### 1. CI Build (`ci-build.yml`)

**Purpose:** Validate every code change

**Triggers:**
- Push to `master` branch
- Pull requests targeting `master`

**Jobs:**
1. **Build Matrix** (parallel, each on a native runner):
   - Windows x64
   - Windows ARM64
   - Linux x64
   - Linux ARM64
   - macOS ARM64

2. **For Each Platform:**
   - Install dependencies (cached)
   - Build compiler bootstrap (Linux x64 only)
   - Build full toolchain
   - Run test suite
   - Run regression tests twice: as is, then with `OBJECK_JIT_THRESHOLD=1` (every method compiled on its first call)
   - Run the debugger, DAP and VM flag tests
   - Upload artifacts (7-day retention)

3. **Linux x64 Only:**
   - Generate API documentation
   - Upload API docs artifact

4. **Tools Job** (Linux, alongside the matrix):
   - Consistency checks, among them: library lists, doc comments, documented dependencies, the examples index, regression-test markers and exit paths
   - Formatter and LSP regression tests
   - VS Code extension install, compile and lint

5. **Status Job:**
   - Aggregate results
   - Report overall CI status
   - On a `master` failure, open or update a tracking issue (and send a push notification when `NTFY_TOPIC` is set)

**Optimization:**
- Dependency caching (~80% cache hit rate)
- ccache for C++ compilation (~60% speedup)
- Parallel job execution (5 concurrent)

**Fast Feedback:**
- Typical time: about 30 minutes
- Each platform job is capped at 60 minutes

---

### 2. Release Build (`release-build.yml`)

**Purpose:** Create production-ready release artifacts

**Triggers:**
- Git tag matching `v*.*.*` (automatic)
- Manual workflow dispatch from a branch (a dry run: it builds and uploads
  artifacts, but Release Publish skips it and it never pushes `docs/api.zip`
  to master)

**Jobs:**

1. **Prepare Job:**
   - Extract version from tag
   - Update version files automatically
   - Generate version metadata artifact

2. **Build Matrix** (parallel):
   - Windows x64 (MSI + ZIP)
   - Windows ARM64 (MSI + ZIP; cross-compiled on the x64 runner)
   - Linux x64 (TGZ)
   - Linux ARM64 (TGZ)
   - macOS ARM64 (`.pkg` and `.zip`, signed and notarized; plus a `.tgz` kept so older copies of `obu` can self-update)

   Every leg except Windows ARM64 smoke-tests the toolchain it is about to ship: it compiles and runs a small program and checks the reported version.

3. **Build LSP Job:**
   - Package Language Server Protocol binaries
   - Package the VS Code extension (`vsce package`; the `.vsix` goes into the ZIP and is not published to the Marketplace)
   - Create LSP ZIP archive

4. **Build Docs Job:**
   - Generate API documentation with correct version
   - Package as ZIP
   - On a tag build, commit it to master as `docs/api.zip`

5. **macOS Install Test Job:**
   - Install the `.pkg` on clean `macos-14` and `macos-26` runners; a failure fails the run, so Release Publish does not publish it

6. **Summary Job:**
   - Aggregate build results
   - Generate build summary report
   - Notify on failures

**Key Features:**
- **Automatic versioning** from git tags (no manual edits)
- **Artifact retention** for 7 days
- **Unsigned Windows installers**: nothing in CI signs them; they are signed locally after publishing (see [SIGNING.md](../SIGNING.md))

**Duration:** 15-70 minutes (parallel builds; the windows-arm64 leg is the long one when its vcpkg cache misses and OpenCV is rebuilt from source)

---

### 3. Release Publish (`release-publish.yml`)

**Purpose:** Rename, checksum, and distribute release

**Triggers:**
- Automatic when a Release Build of a `vYYYY.M.P` tag succeeds (builds of
  branches, including master, and of pre-release tags such as `v2026.9.2-rc1`
  are skipped)
- Manual workflow dispatch (fallback, e.g. to publish a build the automatic
  trigger skipped)

**Required Inputs:**
- `version`: Version number (e.g., 2026.2.1)
- `run_id`: Release build workflow run ID
- `skip_sourceforge`: Optional flag
- `skip_playground`: Optional flag
- `skip_docs_deploy`: Optional flag

**Jobs:**

1. **Prepare Job:**
   - Download all build artifacts
   - Build binary renaming tool
   - Rename files with version numbers
   - Report whether each Windows MSI is signed (`signtool verify /pa`); it does not sign
   - Upload final artifacts (30-day retention)

2. **GitHub Release Job:**
   - Generate `SHA256SUMS`
   - Generate release notes
   - Create GitHub Release
   - Upload all binaries
   - Tag association

3. **Sourceforge Upload Job:**
   - Setup SSH authentication
   - Create version directory
   - Upload via SFTP
   - Cleanup SSH keys

4. **Deploy Playground Job:**
   - Run the playground's `deploy/update.sh` over SSH
   - Check `/api/health` and the version the sandbox engine reports
   - Cleanup SSH keys

5. **Deploy Docs Job:**
   - Extract API documentation
   - Upload to objeck.org's `api/latest/` via rsync (the only served tree; there are no versioned directories)
   - Check that `api/latest/index.html` carries the new version stamp
   - Cleanup SSH keys
   - Declared manual in `RELEASE_MANUAL_STEPS` while the `OBJECK_ORG_*` secrets are unset

6. **Summary Job:**
   - Aggregate deployment status
   - Generate release summary
   - Provide download links

Jobs 3-5 each run when their secrets are set. When they are not, a job passes only if its step (`sourceforge`, `playground` or `docs`) is listed in the `RELEASE_MANUAL_STEPS` repository variable, and fails otherwise. All three are listed today: Sourceforge mirrors the GitHub release through a webhook, and the playground and API docs are deployed by hand.

**Security:**
- Secrets never logged
- Temporary files cleaned up (always)
- SSH keys removed after use

**Duration:** ~10 minutes

---

### 4. Other Workflows

| Workflow | Trigger | What it does |
|----------|---------|--------------|
| `perf-gate.yml` | Push to `master` and PRs touching `core/vm`, `core/compiler` or `core/shared`; manual | Objeck vs Java and LuaJIT time ratios against a committed baseline; report-only for now ([PERF_GATE.md](../perf-results/PERF_GATE.md)) |
| `benchmark.yml` | Manual | Benchmarks on Linux x64 and ARM64, compared against a baseline release tag |
| `nightly-hardening.yml` | Manual (schedule not yet enabled) | Differential regression, fuzzer, heap verifier and GC stress runs against CI's binaries |
| `stress-probe.yml` | Push to `probe/**` branches; manual | Loops the GC/JIT regression fixtures in four modes on all five platforms, against a CI build's binaries |
| `verify-signing-credentials.yml` | Weekly (Monday) and manual | Imports the Apple signing certificates to prove the secrets still work |
| `codeql.yml` | Push, PRs, weekly | CodeQL analysis |
| `secret-scan.yml` | Push to `master`, PRs, weekly, manual | gitleaks secret scan |
| `release-drafter.yml` | Push to `master`, PRs | Keeps a draft release's notes up to date from merged PRs |
| `release-stats.yml` | Weekly and manual | Snapshots release download counts into `tools/release-stats/downloads.csv` |
| `jekyll-gh-pages.yml` | Push to `master`, manual | Publishes `docs/web` to GitHub Pages (objeck.github.io/objeck-lang; objeck.org is hosted separately) |

---

## Secrets Management

### Required Secrets

Set in: **Repository Settings → Secrets and variables → Actions**

| Secret | Purpose | Format | Expiration |
|--------|---------|--------|------------|
| `APPLE_CERTIFICATE_BASE64` | macOS Developer ID Application cert | Base64-encoded P12 | 5 years |
| `APPLE_CERTIFICATE_PASSWORD` | Application cert password | Plain text | Same as cert |
| `APPLE_INSTALLER_CERT_BASE64` | macOS Developer ID Installer cert | Base64-encoded P12 | 5 years |
| `APPLE_INSTALLER_CERT_PASSWORD` | Installer cert password | Plain text | Same as cert |
| `KEYCHAIN_PASSWORD` | CI temporary keychain | Plain text | N/A |
| `APPLE_ID` | Apple ID for notarization | Email | N/A |
| `APPLE_TEAM_ID` | Apple Developer Team ID | 10-char string | N/A |
| `APPLE_APP_PASSWORD` | App-specific password for notarization | Plain text | Revocable |
| `SOURCEFORGE_SSH_KEY` | Sourceforge SFTP | SSH private key | Rotate every 2 years |
| `SOURCEFORGE_USERNAME` | Sourceforge account | Username | N/A |
| `PLAYGROUND_HOST` | Playground deploy target | Hostname or IP | N/A |
| `PLAYGROUND_SSH_KEY` | Playground deploy | SSH private key | Rotate every 2 years |
| `OBJECK_ORG_SSH_KEY` | Web server access | SSH private key | Rotate every 2 years |
| `OBJECK_ORG_USER` | Web server username | Username | N/A |
| `NTFY_TOPIC` | Push notification on a failed build (optional) | ntfy topic | N/A |

The Sourceforge, playground and objeck.org secrets are not set: those steps are declared manual in the `RELEASE_MANUAL_STEPS` repository **variable** (`sourceforge,playground,docs`), which `release-publish.yml` reads. `tools/cicd/check_release_config.sh` compares the secrets the release workflows reference with the ones that are set.

There is no Windows signing secret. The certificate is on a SafeNet eToken and is used locally after publishing; the old `CODESIGN_CERT_BASE64` and `CODESIGN_PASSWORD` secrets are referenced by no workflow (see [SIGNING.md](../SIGNING.md)).

### Creating Secrets

**macOS Code Signing (see [SIGNING.md](../SIGNING.md) for full details):**
```bash
# Export Developer ID cert from Keychain Access as .p12
# Base64 encode and set as GitHub secret
base64 -i certificate.p12 | gh secret set APPLE_CERTIFICATE_BASE64
# CI workflow creates temporary keychain and imports cert automatically
```

**SSH Keys:**
```bash
# Generate SSH key pair
ssh-keygen -t ed25519 -f objeck_sf_key -N ""

# Add public key to Sourceforge account (Settings → SSH Keys)

# Copy private key content to SOURCEFORGE_SSH_KEY secret
cat objeck_sf_key

# Set SOURCEFORGE_USERNAME to your Sourceforge username

# Repeat for objeck.org with OBJECK_ORG_SSH_KEY and OBJECK_ORG_USER,
# and for the playground with PLAYGROUND_SSH_KEY and PLAYGROUND_HOST
```

### Security Best Practices

1. **Never commit secrets** to repository
2. **Use GitHub Secrets** (encrypted at rest, masked in logs)
3. **Rotate regularly** (SSH keys every 2 years, certificates before expiry)
4. **Limit scope** (use dedicated keys, not personal accounts)
5. **Monitor usage** (GitHub audit logs)

---

## Build Matrix

### Platform Configuration

| Platform | Runner | OS Version | Arch | Build Time | Artifacts |
|----------|--------|------------|------|------------|-----------|
| Windows x64 | `windows-2025-vs2026` | Server 2025, VS 2026 | x64 | ~10 min | MSI, ZIP |
| Windows ARM64 | `windows-2025-vs2026` | Server 2025, VS 2026 | ARM64 | 15-70 min | MSI, ZIP |
| Linux x64 | `ubuntu-latest` | Ubuntu 24.04 | x64 | ~4 min | TGZ |
| Linux ARM64 | `ubuntu-24.04-arm` | Ubuntu 24.04 | ARM64 | ~4 min | TGZ |
| macOS ARM64 | `macos-15` | macOS 15 | ARM64 | ~7 min | PKG, ZIP, TGZ |
| LSP | `ubuntu-latest` | Ubuntu 24.04 | x64 | ~5 min | ZIP |

**Notes:**
- This is the release build (`release-build.yml`); build times are from the v2026.9.4 and v2026.9.5 tag builds. CI (`ci-build.yml`) uses the same runners except for Windows ARM64, which it builds and tests natively on `windows-11-arm`
- All builds run **in parallel** (maximum parallelization)
- Total elapsed time = longest single build, Windows ARM64: ~15 minutes with warm caches, up to ~70 when vcpkg rebuilds OpenCV from source
- In the release build, Windows ARM64 is **cross-compiled** on the x64 runner, so its shipped toolchain is not smoke-tested there
- The macOS `.pkg` is install-tested on `macos-14` and `macos-26`

### Runner Specifications

**GitHub-hosted runners** (standard):
- **CPU:** 2-4 cores
- **RAM:** 7-14 GB
- **Disk:** 14-150 GB SSD
- **Network:** Fast (Azure datacenter)

**Cost:** $0 (unlimited for public repos)

---

## Caching Strategy

### Dependency Caching

**Linux (APT):** a user-owned archive directory, because restoring the root-owned `/var/cache/apt/archives` fails with permission errors
```yaml
uses: actions/cache@v5
with:
  path: ~/apt-cache
  key: ${{ runner.os }}-${{ matrix.arch }}-apt-${{ hashFiles('.github/workflows/**') }}
  restore-keys: |
    ${{ runner.os }}-${{ matrix.arch }}-apt-
```

**macOS (Homebrew downloads):**
```yaml
uses: actions/cache@v5
with:
  path: ~/Library/Caches/Homebrew
  key: ${{ runner.os }}-brew-downloads-${{ hashFiles('.github/workflows/**') }}
  restore-keys: |
    ${{ runner.os }}-brew-downloads-
```

**ccache (Compilation):**
```yaml
uses: actions/cache@v5
with:
  path: ~/.ccache
  key: ${{ runner.os }}-${{ matrix.arch }}-ccache-${{ github.sha }}
  restore-keys: |
    ${{ runner.os }}-${{ matrix.arch }}-ccache-
```

**Prebuilt dependencies:** the static QUIC stack (`.github/actions/quic-deps`, keyed on `tools/deps/build_quic_deps.sh`), the libraries bundled into the macOS package (`.github/actions/macos-bundle-deps`, keyed on `tools/deps/build_macos_deps.sh`) and the Windows OpenCV runtime DLLs are cached as well.

### Cache Performance

| Cache Type | Hit Rate | Speedup | Size |
|------------|----------|---------|------|
| APT packages | ~80% | 2-3 min saved | 100-200 MB |
| Homebrew | ~75% | 5-8 min saved | 200-500 MB |
| ccache | ~60% | 5-10 min saved | 200-400 MB |

**Total cache usage:** ~500 MB per platform (well within 500 MB limit)

### Cache Invalidation

Caches are invalidated when:
- Workflow files change (`.github/workflows/**` hash changes)
- Manual cache clear (repository settings)
- 7 days of inactivity (GitHub auto-expires)

---

## Security

### Code Signing

**Windows MSI Signing** (outside CI; see [SIGNING.md](../SIGNING.md)):
1. The private key is on a SafeNet eToken and cannot be exported, so no workflow holds it
2. After publishing, `tools/cicd/sign_release.cmd` (run by `tools/cicd/post_release.sh --sign`) downloads the MSIs and signs them with `signtool`, selecting the certificate by thumbprint
3. It uploads the signed MSIs back to the release and regenerates `SHA256SUMS`
4. Signatures **timestamped** (valid after cert expires)

**Verification** (`sign_release.cmd`, and `release-publish.yml`, which only reports):
```yaml
signtool verify /pa setup.msi
```

**macOS Signing:** the p12 certificates are imported into a temporary keychain that a cleanup step deletes even when the job fails (`if: always()`).

### SSH Key Management

**Best Practices:**
1. **Dedicated keys** (not personal accounts)
2. **Ed25519 algorithm** (modern, secure)
3. **No passphrase** (can't be interactive in CI)
4. **Restricted permissions** (chmod 600)
5. **Always cleaned up** after use
6. **Added to known_hosts** (prevent MITM)

**Example:**
```yaml
- name: Setup SSH key
  run: |
    mkdir -p ~/.ssh
    echo "${{ secrets.SOURCEFORGE_SSH_KEY }}" > ~/.ssh/sf_key
    chmod 600 ~/.ssh/sf_key
    ssh-keyscan frs.sourceforge.net >> ~/.ssh/known_hosts

- name: Cleanup SSH key
  if: always()
  run: rm -f ~/.ssh/sf_key
```

### Secrets in Logs

GitHub Actions **automatically masks** secret values in logs:
- `echo ${{ secrets.MY_SECRET }}` → `***`
- Certificate passwords, SSH keys, etc. are never visible

### Dependency Security

**Mitigation:**
- Actions are pinned to **major-version tags** (e.g., `actions/checkout@v5`), not commit SHAs
- Mostly official actions; the third-party ones are `ilammy/msvc-dev-cmd`, `gitleaks/gitleaks-action` and `release-drafter/release-drafter`
- Regularly update action versions
- Monitor GitHub Security Advisories

---

## Cost Analysis

### Current Cost: $0/month ✅

**Why Free?**
- Objeck is a **public repository**
- GitHub Actions is **unlimited** for public repos
- Includes:
  - Unlimited build minutes (Linux, Windows, macOS)
  - 500 MB artifact storage (sufficient with 7-day retention)
  - 20 concurrent jobs

### If Repository Becomes Private

**Estimated Cost:** $15-25/month

| Runner | Cost/minute | Minutes/month | Monthly Cost |
|--------|-------------|---------------|--------------|
| Linux | $0.008 | 500 | $4 |
| Windows | $0.016 | 400 | $6.40 |
| macOS | $0.08 | 100 | $8 |
| **Total** | | | **~$18.40** |

**Assumptions:**
- 10 releases/month
- 5 CI builds/day
- Average build time: 20 minutes

**Still within budget:** Target was $20-$50/month.

---

## Maintenance

### Regular Tasks

**Weekly:**
- Monitor build times (should stay under 30 minutes cached)
- Check cache hit rates (aim for >75%)
- Review failed builds

**Monthly:**
- Review secret expiration dates
- Update GitHub Actions versions
- Check for security advisories

**Yearly:**
- Rotate SSH keys (every 2 years)
- Renew code signing certificate (before expiry)
- Review and optimize caching strategy

### Updating Workflows

**Best Practices:**
1. **Test changes in a branch** first
2. **Use workflow_dispatch** for manual testing
3. **Monitor CI builds** before tagging release
4. **Keep workflows simple** and well-commented
5. **Use composite actions** to reduce duplication

**Testing Workflow Changes:**
```bash
# Push changes to a branch
git checkout -b test-ci-update
git add .github/workflows/
git commit -m "Test CI workflow update"
git push origin test-ci-update

# Create PR to trigger CI
gh pr create --title "Test CI update" --body "Testing workflow changes"

# Monitor results
gh pr checks --watch

# If successful, merge to master
gh pr merge --squash
```

### Monitoring

**GitHub Actions Dashboard:**
- https://github.com/objeck/objeck-lang/actions
- View all workflow runs
- Filter by status, workflow, branch

**Command Line:**
```bash
# List recent runs
gh run list --limit 10

# Watch a running build
gh run watch

# View logs for a specific run
gh run view <run-id> --log

# Re-run failed jobs
gh run rerun <run-id> --failed
```

**Status Badges:**
Add to README.md:
```markdown
[![CI Build](https://github.com/objeck/objeck-lang/actions/workflows/ci-build.yml/badge.svg)](https://github.com/objeck/objeck-lang/actions/workflows/ci-build.yml)
```

### Troubleshooting

**Common Issues:**

1. **Cache corruption**
   - Symptom: Build failures after successful run
   - Solution: Clear caches in repository settings

2. **Runner out of disk space**
   - Symptom: "No space left on device"
   - Solution: Clean up build artifacts during workflow

3. **Dependency installation timeout**
   - Symptom: APT/Homebrew hangs
   - Solution: Add timeout-minutes to steps

4. **SSH connection failures**
   - Symptom: "Permission denied" or "Connection refused"
   - Solution: Verify SSH keys, check server availability

**Getting Help:**
- GitHub Actions Documentation: https://docs.github.com/actions
- Objeck Issues: https://github.com/objeck/objeck-lang/issues
- GitHub Support: https://support.github.com

---

## Future Enhancements

### Potential Improvements

Already done: automatic publishing after a successful tag build, CodeQL scanning, `SHA256SUMS` checksums, macOS signing and notarization, and categorized draft release notes (`release-drafter.yml`).

1. **Continuous Deployment**
   - Deploy to test environment on every commit
   - Automatic nightly builds (`nightly-hardening.yml` exists; its schedule is not enabled yet)

2. **Enhanced Testing**
   - Enforce the perf gate, which is report-only today (see [PERF_GATE.md](../perf-results/PERF_GATE.md))
   - Memory leak detection

3. **Artifact Signing**
   - Sign the Linux archives
   - Sign `SHA256SUMS` so `obu` can detect a substituted manifest (design: [release_integrity.md](release_integrity.md))

4. **Docker Images**
   - Publish Docker images to Docker Hub
   - Multi-architecture support

5. **Release Notes Automation**
   - Publish the release body without hand editing (today it is written from the release-drafter draft and `docs/readme.txt`)

6. **Notifications**
   - Slack/Discord notifications
   - Email on release completion

---

## References

- **GitHub Actions Docs:** https://docs.github.com/actions
- **GitHub-hosted Runners:** https://docs.github.com/actions/using-github-hosted-runners
- **Workflow Syntax:** https://docs.github.com/actions/reference/workflow-syntax-for-github-actions
- **Caching Dependencies:** https://docs.github.com/actions/using-workflows/caching-dependencies-to-speed-up-workflows

---

## Changelog

| Version | Date | Changes |
|---------|------|---------|
| 1.2 | 2026-09-19 | Windows signing moved out of CI (hardware token); publish is automatic; current runners, artifacts and timings; manual publish steps; other workflows |
| 1.1 | 2026-04-05 | Apple signing and notarization secrets |
| 1.0 | 2026-02-10 | Initial CI/CD architecture documentation |
