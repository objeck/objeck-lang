# Design: build the standard libraries in CI instead of committing them

**Status: approved, being landed in four stages, each its own PR proven green before the next.**
Decisions: stop committing `core/lib/*.obl` entirely; Windows builds its own `lang.obl` through a new
`_SYSTEM` MSBuild configuration (no Linux artifact dependency, nothing kept committed).

| Stage | What | State |
|---|---|---|
| 1 | `.github/workflows/libs.yml` (reusable) builds every library from source, fails on any byte that differs from the committed set, uploads the `libs` artifact; every consumer below `needs: libs` and downloads it over `core/lib/`. The `.obl` stay committed. | this change |
| 2 | Windows `_SYSTEM` MSBuild configuration producing a `lang.obl` byte-identical to the committed one | |
| 3 | deploy scripts fail clearly on a missing `.obl`; `check_obl_versions.py`, `local_linux.sh`, bump-version steps 5-6 and the release skill retargeted | |
| 4 | `git rm --cached core/lib/*.obl` and ignore them | after the release is cut |

The library list is read from `core/compiler/build_libs.sh` only (never hard-coded in CI), so a library PR that
adds a source and its `.obl` needs no edit to the job.

Today the 52 compiled libraries in `core/lib/*.obl` are tracked in git. Each is derived from
`core/compiler/lib_src/*.obs`, and each embeds the toolchain version (`VER_NUM` in
`core/shared/version.h`), so the whole set is rebuilt and re-committed at every version bump
(`bump-version` step 5, `check_obl_versions.py` exists to catch the bump that forgets).

This page records what depends on the committed copies, what replacing them would take, and the
decision the maintainer has to make.

## Why

Measured on this tree:

- `git log --since=2026-06-01 -- 'core/lib/*.obl'`: 451 commits touched a library, 82 of them `lang.obl`.
- Between the 2026.11.2 and 2026.11.3 bump commits all 52 files changed. Of those, 47 kept their
  inflated length and 28 differ in 16 bytes or fewer (the version stamps); only a handful (gen_collect,
  ml, ml_vector, ...) changed because their source did.
- The set is about 1.1 MB, so the cost is not size. It is review noise (every bump PR is 52 binary
  diffs), merge conflicts on binaries, and the structural failure the set exists to cause: a tree whose
  source and `.obl` disagree ("a committed .obl lags its source", `local_linux.sh`; "Toolchain version
  mismatch reading lang.obl", `check_obl_versions.py`).
- CI already rebuilds them. `ci-build.yml` runs `update_version.sh` (linux-x64), `update_version_arm.sh`
  (linux-arm64, macOS) or `build_libs.sh` with the MSBuild-built `obc` (Windows), then copies the result
  into the deploy tree, so the committed copies are a bootstrap and a cache, not the tested artifact.
  `tools/cicd/local_linux.sh` already asserts a rebuild is byte-identical to the committed set (all but
  `lang.obl`). Determinism is therefore an existing, checked property.

## What builds a library

- `core/compiler/build_libs.sh <obc>` is the ordered list: 51 libraries, each `obc -src lib_src/X.obs
  -lib <deps> -tar lib -dest ../lib/X.obl`. It is the single copy; `update_version.sh` and CI call it.
- `lang.obl` is not in that list. It is compiled by the bootstrap compiler `sys_obc` (the same sources built
  with `-D_SYSTEM`, `make -f make/Makefile.sys.amd64`), because `obc` cannot compile anything without
  `lang.obl`. `update_version.sh` does `sys_obc -> lang.obl -> obc -> build_libs.sh` in one go, from nothing.
- **Windows has no way to build `lang.obl`.** `core/compiler/vs/compiler.vcxproj` has no `_SYSTEM` configuration.
  The Windows legs take `lang.obl` from the tree, i.e. from git, and `build_libs.sh` then rebuilds the other 51
  with the MSBuild `obc`. The bump skill's note "version bumps must rebuild and commit `lang.obl` via WSL2
  `sys_obc`, or windows-x64 CI fails" is this fact.

### Stage 1: what consumes the artifact now (every workflow that read `core/lib/*.obl` or ran a deploy script from the checkout)

`ci-build.yml` (`build` x5 legs, `tools`; `status` also needs `libs`), `perf-gate.yml`, `codeql.yml` (the C++ manual build),
`benchmark.yml` (current toolchain; the baseline checkout brings its own tag's files), `probe-gc-stress-arm64.yml`,
`release-build.yml` (`build` x5, `build-lsp`, `build-docs`; its `libs` call stamps `version-metadata` first),
`release-publish.yml` (`prepare`, the Windows build), `verify-platforms.yml`. `nightly-hardening.yml`, `soak.yml` and
`stress-probe.yml` download ci-build's deploy trees, which already carry them. Not workflows, left for stage 3:
`core/release/cov_scan.sh`, `docker/ci_repro.sh`, `tools/cicd/local_linux.sh`, `refresh_deploy.ps1`, `tools/lsp/scripts/update_lsp.sh`.

## Consumers of the committed copies

Every place that reads `core/lib/*.obl` out of the checkout rather than from a freshly built tree:

| Consumer | How it uses the committed set | What replaces it |
|---|---|---|
| `deploy_posix.sh:44`, `deploy_macos_arm64.sh:83`, `deploy_msys2-*.sh:63` | `cp ../lib/*.obl` into the deploy tree | the libs job's artifact, or `update_version.sh` having run first |
| `deploy_windows.cmd:110` | `copy ..\lib\*.obl` | same; plus `lang.obl` (see below) |
| `ci-build.yml` linux-x64 | `update_version.sh` rebuilds in the tree first, so already independent | nothing |
| `ci-build.yml` linux-arm64, macOS | `update_version_arm.sh` rebuilds first, already independent | nothing |
| `ci-build.yml` Windows x64/ARM64 | deploy copies the committed set, then `build_libs.sh` rebuilds 51 of them | `lang.obl` from the artifact; the other 51 as today |
| `ci-build.yml` `tools` job (line ~1995) | builds only `obc` and `obr`, then `cp core/lib/*.obl` | must build or download the libs (it is the cheap job; the download is cheaper than building) |
| `perf-gate.yml:97` | builds `obc`/`obr`, `cp core/lib/*.obl` into its bin dir | same as the `tools` job |
| `probe-gc-stress-arm64.yml:89` | `cp ../lib/*.obl deploy-arm64/lib/`, then `build_libs.sh` | `lang.obl` from the artifact |
| `codeql.yml:106`, `benchmark.yml`, `cov_scan.sh` | `deploy_posix.sh` with no prior rebuild, so they ship the committed set | run the libs step first (or download the artifact) |
| `release-build.yml` Windows legs | `deploy_windows.cmd` copies the committed set and nothing rebuilds it: **Windows releases ship the committed `.obl`** | download the artifact before the deploy |
| `release-build.yml` `build-docs`, `build-lsp` | `build-docs` already runs `update_version.sh`; `build-lsp` copies the deploy tree's libs | already independent / same as the POSIX legs |
| `release-publish.yml` Windows | `deploy_windows.cmd x64` with the committed set | same as the release Windows legs |
| regression runners (`run_regression.sh`, `.cmd`) | resolve libraries from `../lib` next to the compiler, i.e. from a deploy tree, which is a COPY of `core/lib` | nothing, as long as the deploy tree is populated before the run |
| LSP bundle (`release-build.yml:990`) | `cp core/release/deploy/lib/*.obl` (a deploy tree) | nothing |
| `refresh_deploy.ps1` (local Windows dev) | copies `core/lib/*.obl` into `deploy-x64` and warns on a stale tree | see "Local development" |
| `docker/ci_repro.sh`, `docker/Dockerfile.ci-repro`, `perf-results/docker/Dockerfile` | `COPY core/lib/*.obl` as a fallback / cache | build with `update_version.sh` (ci_repro already does) |
| `programs/web-playground/docker/build-sandbox.sh` | takes libs from a deploy tree | nothing |
| `tools/cicd/check_obl_versions.py` + its `tools` job step | reads the committed bytes and fails on a version mismatch | delete (nothing committed to go stale), or point it at a built tree |
| `tools/cicd/local_linux.sh` "libraries equal the committed .obl" | compares a rebuild to git | becomes "libraries build"; the comparison disappears |
| `bump-version` skill, steps 5-6 and its `git add core/lib/*.obl` in `release` | rebuild and commit | step removed |

## Design

One producer, many consumers, per workflow run.

1. A `libs` job on `ubuntu-24.04` builds the compiler bootstrap and the libraries from source:
   `core/compiler/update_version.sh` (sys_obc, `lang.obl`, obc, `build_libs.sh`), then uploads
   `core/lib/*.obl` as the `obl` artifact. Roughly the cost of the existing "Build bootstrap" step.
2. Every job that needs a library set `needs: libs` and downloads `obl` into `core/lib/` before its deploy
   script. The deploy scripts are unchanged: they still `cp ../lib/*.obl`.
3. Linux and macOS legs may keep rebuilding for themselves, as they do now; they then do not need the artifact.
   The artifact is mandatory for what cannot build `lang.obl`: Windows x64, Windows ARM64, and the
   `tools`/`perf-gate`/probe jobs that build only `obc` and `obr`.
4. The libraries are portable bytecode. This is already relied on: the committed set is produced on Linux
   (WSL2) and run on Windows. The prototype's comparison step reports whether a CI-built set equals the
   committed one byte for byte.

The same shape as `docs/api.zip` (PR for item 3): the file is made once per run and handed to consumers as
an artifact.

### Windows and `lang.obl`

Two ways to give Windows a `lang.obl` without git:

- **A. Artifact (recommended).** The Linux `libs` job builds it; Windows legs download it. No new build
  configuration. Cost: the Windows legs wait for `libs` (a few minutes) and a local Windows developer needs
  another source (below).
- **B. A `Release-SYSTEM` configuration of `compiler.vcxproj`** that defines `_SYSTEM` and emits `sys_obc.exe`,
  plus a step that runs it. Removes the cross-platform dependency, adds a build configuration to maintain,
  and gives a second `lang.obl` producer whose output should be byte-identical to the Linux one.

### Local development

A fresh clone has no `.obl`, so the first build needs them. The existing flows already cover most of it:

- Linux/macOS: `core/compiler/update_version.sh` builds everything from nothing (this is exactly what the
  prototype runs). `deploy_posix.sh` would call it, or fail with that instruction, when `core/lib/lang.obl`
  is absent.
- Windows: `refresh_deploy.ps1` / `deploy_windows.cmd` need `lang.obl`. Options: (1) build it in WSL2 with
  `update_version.sh`, as the maintainer does today for the bump; (2) fetch the `obl` artifact of the latest
  green master run (`gh run download -n obl`); (3) option B above.

### The compromise: commit only `lang.obl`

`lang.obl` is the only library with no Windows build path. Keeping just that file tracked removes 51 of 52
files from every bump and leaves the local Windows flow unchanged. It still changes at every bump (it embeds
`VER_NUM`), so it keeps `check_obl_versions.py` and the "rebuild and commit lang.obl" bump step alive for one
file. It is the smallest change that deletes most of the churn; it does not remove the class of failure.

## Prototype

`.github/workflows/obl-from-source.yml` (runs on demand and on a PR touching it or `build_libs.sh`), driven
by `tools/cicd/obl_from_source.sh`:

1. Save the committed `core/lib/*.obl` aside, then delete every one. The tree now has no compiled library.
2. `core/compiler/update_version.sh`: sys_obc, `lang.obl`, obc, all 51 libraries from `build_libs.sh`.
   It fails if the committed set has a library the build did not produce.
3. `core/release/deploy_posix.sh x64` (the same deploy script CI runs), then the full regression suite
   (`run_regression.sh x64`) against that tree.
4. Byte-compare the rebuilt set with the saved one (information, not a gate), run
   `check_obl_versions.py` on the rebuilt files, and upload them as the `obl-from-source` artifact: what a
   `libs` job would hand to the other legs.

Result (run 45223275631, ubuntu-24.04, 2026-11-23): all 52 libraries rebuilt from nothing in about 2 minutes;
**all 52, `lang.obl` included, are byte-identical to the committed files**; `deploy_posix.sh x64` and the
regression suite pass (877 passed, 10 skipped, 0 failed; the skips are the ODBC and GL tests, whose data
source and context the prototype does not configure); `check_obl_versions.py` accepts the rebuilt set.
So on Linux the committed copies are pure derivatives of the source.

What the prototype does **not** prove: a Windows or ARM64 leg consuming the artifact (needs the `needs:`
edges and a download step in `ci-build.yml`/`release-build.yml`); that a CI-built `lang.obl` is accepted by a
Windows `obc` (expected, since the committed one is built the same way on WSL2, but not exercised); the
cache-hit paths of the release workflow.

## Rollout, if approved

1. Add the `libs` job and `needs: libs` plus a download step to the jobs in the table above; make the deploy
   scripts fail with a clear message when `core/lib/lang.obl` is absent. Land this with the `.obl` still
   committed and proven green on `master`, a dry-run `release-build.yml` dispatch, and one cache-hit run.
2. Only then `git rm --cached core/lib/*.obl`, ignore them, drop `check_obl_versions.py` (or retarget it),
   and delete bump-version steps 5-6.
3. History keeps the old blobs; the repo does not shrink, but new bumps stop adding to it.

## Decision for the maintainer

1. **Do it at all?** The cost is a hard dependency from the Windows legs and the lightweight jobs on a Linux
   job, and a slightly harder first build for a Windows contributor. The gain is deleting the bump's
   52-file rebuild and the whole stale-library failure class.
2. **Windows `lang.obl`: artifact (A), a `_SYSTEM` MSBuild configuration (B), or keep just `lang.obl` committed?**
3. **Local Windows flow:** accept "build `lang.obl` in WSL2 or download the artifact" for a fresh clone?
