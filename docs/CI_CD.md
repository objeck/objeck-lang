# Objeck Language CI/CD Architecture

**Version:** 1.4
**Last Updated:** 2026-11-18

This document describes the technical architecture of the Objeck Language continuous integration and continuous deployment (CI/CD) system. For what each *test, fuzzer and verifier* in that pipeline checks, with its input, where it runs, how to run it locally and what it cannot find, see [TESTING_AND_ASSURANCE.md](TESTING_AND_ASSURANCE.md); this page covers the workflows, secrets, runners and caching.

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
- ✅ **Multi-destination distribution** (GitHub Releases, which Sourceforge mirrors; the playground deploy and the objeck.org API docs upload are currently manual, both scripted)
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
   - Register the `objeck_sqlite_test` ODBC data source the ODBC regression tests open: `libsqliteodbc` and `~/.odbc.ini` on Linux, `tools/cicd/setup_sqlite_odbc.sh` (Homebrew `sqliteodbc`, `~/.odbc.ini`) on macOS, `tools/cicd/setup_sqlite_odbc.ps1` on Windows (sqliteodbc 0.99991 pinned by checksum: unpacked from the upstream installer on x64, compiled from source on ARM64, which upstream does not ship; a per-user registry DSN). The regression steps set `OBJECK_REQUIRE_ODBC=1`, so a missing data source fails those tests instead of skipping them
   - Build compiler bootstrap (Linux x64 only)
   - Build full toolchain
   - Run the deploy examples: 4 programs compiled and run on every leg, 9 more on Linux x64 (`threads_12` must print its exact total)
   - Run regression tests twice: as is, then with `OBJECK_JIT_THRESHOLD=1` (every method compiled on its first call)
   - Run the debugger, DAP, VM flag and compiler robustness (mutation and stress) tests
   - Run the `obi`/`obr` parity check, the library link and generics round trips, the updater tests, and the unit tests for the old-generation set and the free-block size classes
   - Run the network tests and the ONNX face-detection test as non-gating steps (`continue-on-error`; Linux x64 and Windows x64 for the network tests)
   - Upload artifacts (7-day retention)

3. **Linux x64 Only:**
   - Generate API documentation
   - Upload API docs artifact

4. **Tools Job** (Linux, alongside the matrix, 30-minute cap):
   - Build-free consistency checks, each one a step of `ci-build.yml` and each mirrored in `tools/cicd/local_ci.py` (run `python tools/cicd/local_ci.py` before pushing): library lists, doc-comment fences, regression-test exit paths, JIT opt-out and differential markers, trap handlers and intrinsics, the native write-barrier scan and its positive control, the API index, the examples index, `.in` templates against generated files, `.obl` version stamps, SHA256SUMS re-signing, the README's published release key, documented dependencies, Markdown links and anchors, site pages against their Markdown, and the benchmark set and inputs against `docs/performance.md`
   - Formatter and LSP regression tests, built against a deploy tree assembled in the job
   - VS Code extension install, compile and lint

5. **Loader Fuzz Job** (Linux, after the build, advisory):
   - A 5-minute libFuzzer + ASan/UBSan run of the `.obe` loader against the linux-x64 tree this run built (`tools/fuzz/loader/run_nightly.py`)
   - Never blocks a merge: the job is `continue-on-error`, is not in the Status Job's `needs`, and reports a finding as a warning annotation and in the job summary; any saved input is uploaded as `loader-fuzz-advisory`

6. **Status Job:**
   - Aggregate results
   - Report overall CI status
   - On a `master` failure, open or update a tracking issue (and send a push notification when `NTFY_TOPIC` is set)

**Optimization:**
- Dependency caching, ccache for C++ compilation and parallel jobs (5 platform legs plus the tools job). The hit rates and speedups quoted in [Cache Performance](#cache-performance) are estimates with no measurement behind them in this repository.

**Fast Feedback:**
- Typical time: about 30 minutes (an estimate carried over from earlier versions of this page; not recorded by any workflow)
- Each platform job is capped at 60 minutes (`timeout-minutes: 60` in `ci-build.yml`)

---

### 2. Release Build (`release-build.yml`)

**Purpose:** Create production-ready release artifacts

**Triggers:**
- Git tag matching `v*.*.*` (automatic)
- Manual workflow dispatch from a branch (a dry run: it builds and uploads
  artifacts, but Release Publish skips it)

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

4. **Build Docs Job** (runs before the Build jobs, which need it):
   - Generate API documentation with the correct version (`tools/cicd/gen_api_docs.sh`)
   - Package it as `docs/api.zip` and upload it as the `api-docs` artifact. It is not committed
     to git; each Build job downloads the artifact before deploying (windows-x64 makes its own),
     and Release Publish deploys the same artifact to objeck.org

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
   - Check out the repository (for the deploy script and `version.h`)
   - Extract the API documentation artifact
   - Pin objeck.org's host key with `ssh-keyscan`
   - Run `core/release/deploy_website.sh`, the SAME script a maintainer runs
   - Cleanup SSH keys
   - Declared manual in `RELEASE_MANUAL_STEPS` while the `OBJECK_ORG_*` secrets are unset

   **This job had never worked.** It rsynced to
   `/var/www/objeck.org/api/latest/`, a VPS layout, while objeck.org is GoDaddy
   shared hosting serving `~/public_html` — `/var/www/objeck.org` does not exist
   there. Because the secrets are unset the step was skipped, so the wrong path
   was never exercised: v2026.8.4 published while the site served v2026.8.3 docs
   and the pipeline reported the deploy green. The transport now lives in one
   script shared with the manual path, so a destination fix cannot land in one
   caller and not the other.

6. **Summary Job:**
   - Aggregate deployment status
   - Generate release summary
   - Provide download links

Jobs 3-5 each run when their secrets are set. When they are not, a job passes only if its step (`sourceforge`, `playground` or `docs`) is listed in the `RELEASE_MANUAL_STEPS` repository variable, and fails otherwise. All three are listed today: Sourceforge mirrors the GitHub release through a webhook, and the playground and API docs are deployed by hand.

### Deploying the API reference by hand

Two steps, and the first is the one that gets forgotten.

**1. Regenerate**, or download the release build's artifact (`gh run download <build run> -n api-docs`); `docs/api.zip` is not committed:

```
cmd /c "cd /d <repo>\core\release & <repo>\core\release\code_doc64.cmd x64 deploy"
```

Use the full path to the script: the current directory is not searched for
executables when `cmd` is invoked from Git Bash, so a bare name reports
"not recognized" from inside the very directory holding the file. This
rewrites `docs/api.zip` and `deploy-x64/doc/api`, and refuses to package a
build whose doc comments were rejected, whose page count is zero, or whose
`search_index.json` does not parse.

**2. Deploy:**

```
bash core/release/deploy_website.sh            # add --dry-run first
```

It needs only `ssh` and `tar` — not `rsync`, which Git for Windows does not
ship and which must exist at both ends. The tree is uploaded beside the live
one and swapped in, so a reader never sees a half-written directory and a
failed transfer leaves the previous docs serving.

What it refuses, and why each one exists:

| refusal | the failure it prevents |
|---|---|
| fewer than 400 pages | an `api.zip` holding only the three style files shipped once |
| pages stamped for another version | the committed zip was stamped `v2026.10.0` while its content predated two libraries |
| remote directory absent | uploading a plausible tree to a path nobody serves |
| HTTPS stamp still old afterwards | "the upload exited 0" is not "the site serves the new docs" |

It also **warns** when a bundle declared in `lib_src` has no pages at all.
That is the check that catches the real failure: `API.Inference` (#1051) and
`API.Models` (#1062) were absent from the published reference while the total
page count looked healthy, because a count of ~500 cannot see two missing
libraries. The check is keyed on the **bundle**, not the source filename,
since pages are named after the bundle — `ml_core.obs` and six siblings all
declare `System.ML` and produce `system.ml-*.html`.

**Security:**
- Secrets never logged
- Temporary files cleaned up (always)
- SSH keys removed after use

**Duration:** ~10 minutes

---

### 4. Other Workflows

| Workflow | Trigger | What it does |
|----------|---------|--------------|
| `perf-gate.yml` | Push to `master` (paths `core/vm`, `core/compiler`, `core/shared`, `perf-results`, the workflow) and PRs touching `core/vm`, `core/compiler` or `core/shared`; manual | Objeck vs Java and LuaJIT time ratios against a committed baseline; report-only for now, so it always concludes `success` ([PERF_GATE.md](../perf-results/PERF_GATE.md)) |
| `benchmark.yml` | Manual | Benchmarks on Linux x64 and ARM64 (3, 1 or 5 runs), compared against a baseline release tag |
| `nightly-hardening.yml` | Daily at 07:23 UTC, and manual | Differential regression, program fuzzer, regression suite under the heap verifier (minor and major stress) and GC stress loops on all five platforms against CI's binaries, and a 30-minute libFuzzer run on the `.obe` loader (linux-x64); failures are triaged into infra, known and new and only new ones open issues |
| `soak.yml` | Manual | The release soak: the site server under client load beside regression, GC stress and fuzzer loops, up to 330 minutes per leg, on binaries CI already built |
| `stress-probe.yml` | Push to `probe/**` branches; manual | Loops the GC/JIT regression fixtures in four modes on all five platforms, against a CI build's binaries |
| `probe-gc-stress-arm64.yml` | Push to `probe/**` branches | The windows-arm64 `core_thread_gc_stress` probe behind the closed issue #1077 (builds the toolchain itself; 180-minute cap) |
| `verify-platforms.yml` | Manual | The release gate run in the cloud: `tools/cicd/verify_platform.sh`/`.cmd` per platform, a verdict file per leg, passing only if every leg passes at the same commit |
| `tests-manifest.yml` | Push to `master` touching `programs/regression/*.obs` or `gen_manifest.py`; manual | Regenerates and commits `programs/regression/TESTS.md` |
| `playground-tests.yml` | PRs and pushes touching the playground, `build_libs.sh` or `lib_src` | The web playground's backend tests |
| `release-deb.yml` | After Release Publish, manual, and PRs touching `tools/packaging` | Builds and tests Debian packages from a published release's tarballs, adds them to `SHA256SUMS` and re-signs |
| `resign-manifest.yml` | Manual (also called by `release-deb.yml` and the release scripts) | Signs a published `SHA256SUMS` again with the Actions secret ([release_integrity.md](release_integrity.md)) |
| `verify-signing-credentials.yml` | Weekly (Monday) and manual | Imports the Apple signing certificates to prove the secrets still work; the `manifest-key` job checks the stored minisign secret still matches the committed public key, and `published-key` that objeck.org serves the same key bytes |
| `codeql.yml` | Push and PRs to `master`, weekly (Monday 13:39 UTC) | CodeQL analysis of C/C++ (manual build), JavaScript/TypeScript and Python |
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
| Linux x64 | `ubuntu-24.04` | AlmaLinux 8 container (glibc 2.28), see [PORTABLE_LINUX.md](design/PORTABLE_LINUX.md) | x64 | ~15 min with the static libraries cached | TGZ |
| Linux ARM64 | `ubuntu-24.04-arm` | AlmaLinux 8 container (glibc 2.28) | ARM64 | ~15 min with the static libraries cached | TGZ |
| macOS ARM64 | `macos-15` | macOS 15 | ARM64 | ~7 min | PKG, ZIP, TGZ |
| LSP | `ubuntu-24.04` | Ubuntu 24.04 | x64 | ~5 min | ZIP |

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
uses: actions/cache@<sha> # v6.1.0 (every action in the workflows is pinned to a full commit SHA)
with:
  path: ~/apt-cache
  key: ${{ runner.os }}-${{ matrix.arch }}-apt-${{ hashFiles('.github/workflows/**') }}
  restore-keys: |
    ${{ runner.os }}-${{ matrix.arch }}-apt-
```

**macOS (Homebrew downloads):**
```yaml
uses: actions/cache@<sha> # v6.1.0
with:
  path: ~/Library/Caches/Homebrew
  key: ${{ runner.os }}-brew-downloads-${{ hashFiles('.github/workflows/**') }}
  restore-keys: |
    ${{ runner.os }}-brew-downloads-
```

**ccache (Compilation):**
```yaml
uses: actions/cache@<sha> # v6.1.0
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

*These figures are estimates carried over from the first version of this page; no workflow or script in the repository records hit rates or time saved (conditions not recorded). The one measured fact in the workflows is in `nightly-hardening.yml`: the repository cache was over GitHub's 10 GB per-repository cap at the time that file was written, which is why the nightly only restores caches and never saves. GitHub's cap is 10 GB for the repository, not the 500 MB this page used to state.*

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
- Actions are pinned to **full commit SHAs** with a trailing version comment (e.g., `actions/checkout@<sha> # v7.0.1`); Dependabot (`.github/dependabot.yml`) proposes weekly updates
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
   - Automatic nightly builds (`nightly-hardening.yml` runs daily at 07:23 UTC)

2. **Enhanced Testing**
   - Enforce the perf gate, which is report-only today (see [PERF_GATE.md](../perf-results/PERF_GATE.md))
   - Memory leak detection

3. **Artifact Signing**
   - Sign the Linux archives (still open: they have no platform signature)
   - ~~Sign `SHA256SUMS` so `obu` can detect a substituted manifest~~ **done** (`release-publish.yml` signs, `resign-manifest.yml` re-signs, `obu` verifies; [release_integrity.md](release_integrity.md))

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
| 1.4 | 2026-11-18 | Every workflow in `.github/workflows` is listed with its trigger; the `tools` job, loader-fuzz job, nightly, soak and verification workflows described from the workflow files; cache and timing figures marked as estimates; the signed-manifest item moved to done; link to the assurance map |
| 1.3 | 2026-10-24 | The ODBC regression tests run on every leg: the SQLite ODBC data source is set up on Windows and macOS too, and `OBJECK_REQUIRE_ODBC` turns a missing one into a failure |
| 1.2 | 2026-09-19 | Windows signing moved out of CI (hardware token); publish is automatic; current runners, artifacts and timings; manual publish steps; other workflows |
| 1.1 | 2026-04-05 | Apple signing and notarization secrets |
| 1.0 | 2026-02-10 | Initial CI/CD architecture documentation |
