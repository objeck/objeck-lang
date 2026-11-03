# tools/cicd

Scripts the build, the release and the checks run. Most are Python 3 with the standard library only; the release scripts are shell, batch or PowerShell. The pipeline they serve is described in [docs/CI_CD.md](../../docs/CI_CD.md) and [docs/release_process.md](../../docs/release_process.md); the whole system in [docs/ARCHITECTURE_MAP.md](../../docs/ARCHITECTURE_MAP.md).

## Run the checks before you push

```
python tools/cicd/local_ci.py            # every build-free check ci-build.yml's Tools job runs, in parallel
python tools/cicd/local_ci.py --list     # name them
python tools/cicd/local_ci.py doc        # only the checks whose name contains "doc"
python tools/cicd/local_ci.py --self-test  # every check_*/test_* the workflow runs is listed in local_ci.py
```

`local_ci.py` mirrors the Tools job: when a step is added to `ci-build.yml`, add it to `CHECKS` there (the self-test fails if you do not). It needs no build. For anything that does, build the committed tree in a clean Linux copy with `local_linux.sh [REF]` (WSL2 or Linux) or `local_docker.sh [REF]` (a clean `ubuntu:24.04` container); both accept `--jit1`, `--flags` and `--skip-regression`.

## Checks (`check_*.py`, `test_*.py`)

Each fails (exit 1) when two things that must agree by hand have drifted. Several take `--test`, which proves the check can still fail.

| Script | Keeps in agreement |
|---|---|
| `check_trap_handlers.py` | `traps.h`, scanner, parser, emitter and `ProcessTrap` for every trap and intrinsic ([ADDING_AN_INTRINSIC](../../docs/ADDING_AN_INTRINSIC.md)) |
| `check_version_templates.py`, `check_obl_versions.py` | generated version files vs their `.in` templates; every committed `.obl` vs `VER_NUM` |
| `check_doc_lists.py`, `check_doc_fences.py`, `check_docs_deps.py`, `check_doc_links.py` | library lists vs `lib_src`; doc-comment code fences; install instructions vs `runtime_deps.json`; Markdown links and anchors vs the tree (known breaks in `doc_links_known.txt`) |
| `check_api_index.py`, `check_examples_index.py`, `check_sdl_natives.py`, `check_openai_image_dispatch.py` | the editor API index, the shipped example index, SDL bindings and OpenAI image calls vs their sources; the first and last need a built toolchain |
| `check_test_exit_codes.py`, `check_jit_optouts.py`, `check_diff_markers.py` | regression-test conventions: a test can fail, an opt-out has a stated reason |
| `check_native_write_barrier.py` | native code that stores a received object runs the write barrier |
| `check_benchmark_inputs.py` | `perf-results/run_benchmarks.sh` vs the inputs documented in `docs/performance.md` |
| `check_manifest_resign.py` | anything that replaces a published `SHA256SUMS` re-signs it |
| `check_onnx_provider_parity.py` | needs a machine with an execution provider |
| `test_*.py` | unit tests for the scripts above, for `nightly_triage.py`, `stress_probe.py`, `obu verify`, and `API.Inference` over a mock server (`mock_inference_server.py`, `serve_onnx_model.py`) |

`doc_coverage.py` is a measurement, not a gate: it writes [docs/DOC_COVERAGE.md](../../docs/DOC_COVERAGE.md) (`python tools/cicd/doc_coverage.py --write docs/DOC_COVERAGE.md`).

## Nightly and probes

`nightly_triage.py` runs the nightly steps and sorts failures into infrastructure, known and new; `.github/workflows/nightly-hardening.yml` calls it, and `test_nightly_contract.py` checks that the workflow and the scripts agree. `stress_probe.py` loops GC and JIT fixtures against a prebuilt deploy tree (`stress-probe.yml`).

## Release

Run by the release workflows or by the maintainer after them; read [docs/release_process.md](../../docs/release_process.md) first. `post_release.sh <VERSION>` finishes a published release; `watch_release.sh`, `verify_release.cmd` observe it; `check_release_config.sh` is the pre-flight that asks whether the pipeline is configured to do what it claims; `verify_platform.sh` and `.cmd` are the per-platform release gate (`verify-platforms.yml` runs them in the cloud); `sign_release.cmd`, `check_release_signatures.ps1`, `update_sha256sums.ps1`, `sign_macos_tree.sh`, `notarize.sh` and `macos_install_test.sh` sign and test the artifacts; `check_open_issues.sh` is the release gate on open issues; `cleanup_runs.sh` deletes failed workflow runs. These touch published releases and signing keys: do not run one to see what it does.

## Setup helpers

`setup_sqlite_odbc.sh` and `.ps1` install the SQLite ODBC driver and register the `objeck_sqlite_test` data source the ODBC regression tests use. `gen_examples_readme.py` regenerates `programs/deploy/README.md`. `hoist_native_names.py` holds native entry-point names in fields (`--check` is one of the local checks); `audit_ratios.py` lists the divisions in Objeck sources and shortlists the unguarded ones.
