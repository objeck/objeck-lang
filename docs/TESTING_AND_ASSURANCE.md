# Testing and assurance: what is checked, how, and what is not

This page maps every mechanism in the repository that tests, fuzzes, verifies or statically analyses Objeck (the compiler in `core/compiler`, the VM and JIT in `core/vm`, the libraries in `core/lib` and `core/compiler/lib_src`, the tools). It is written for people who want to judge how well Objeck is tested and to reproduce what is claimed. Each entry says what it checks, what input it uses, where it runs, how to run it locally and, in one line, what it cannot find.

How to read it:

- **Evidence status.** Everything below was read from the files linked; nothing here was re-run for this page. Counts (tests, legs, runs) are from the checkout the page was written against (master `3042a96eee`, 2026-11-18) and drift; the generated [programs/regression/TESTS.md](../programs/regression/TESTS.md) is the live inventory of the regression suite.
- **"Per push"** means `.github/workflows/ci-build.yml`, which runs on every push to `master` and every pull request to `master`, on five native legs: windows-x64, windows-arm64, linux-x64, linux-arm64, macos-arm64 (plus the `tools` job on Linux). **"Nightly"** means `.github/workflows/nightly-hardening.yml`, daily at 07:23 UTC and on dispatch, run against the binaries of the latest successful master `ci-build` run.
- **What is claimed and what is not.** The checks below find defects; none of them is a proof. The section [What nothing here covers](#what-nothing-here-covers) lists the known holes.

## At a glance

| # | Mechanism | Input | Runs | Finds |
|---|---|---|---|---|
| [1](#1-the-regression-suite) | Regression suite, 812 `.obs` programs | hand-written programs | per push, all legs, twice (`OBJECK_JIT_THRESHOLD=1` second) | behavioural regressions |
| [2](#2-negative-tests-bad_obs-and-expect_-markers) | Negative tests (`bad_*.obs`, `EXPECT_*`) | programs that must be rejected | inside 1 | missing or wrong diagnostics, crashes on bad input |
| [3](#3-compiler-robustness-mutation-and-stress) | Compiler mutation and stress | valid programs broken 16 ways, hostile files | per push, all legs | compiler crash, hang, diagnostic accuracy drift |
| [4](#4-differential-regression) | Differential regression | every regression test at `-opt s0`/`s3` x JIT modes | nightly | optimizer and JIT miscompiles |
| [5](#5-differential-program-fuzzer) | Program fuzzer | generated programs | nightly; soak | optimizer and JIT miscompiles, crashes |
| [6](#6-emi-fuzzing) | EMI fuzzing | mutated regression tests | by hand only | front-end, optimizer and JIT miscompiles |
| [7](#7-the-obe-loader-fuzzer) | Loader fuzzer (libFuzzer + ASan/UBSan) | mutated `.obe` images | nightly 30 min; advisory 5 min per push | loader crashes, hangs, unbounded allocation |
| [8](#8-vm-flag-and-vm-behaviour-tests) | VM flag tests, 31 sections | targeted fixtures | per push, all legs | flag handling, verifier/loader refusals, GC root typing |
| [9](#9-gc-stress-and-the-objeck_gc_-verification-modes) | GC stress and `OBJECK_GC_*` modes | the regression suite and stress fixtures under forced collection | nightly; stress-probe; soak | GC invariant violations, stale roots |
| [10](#10-debugger-and-dap-tests) | Debugger and DAP tests | scripted sessions | per push, all legs | debugger regressions |
| [11](#11-build-free-consistency-checks) | `tools/cicd/check_*.py` | two hand-kept artefacts at a time | per push (`tools` job); `local_ci.py` | drift between lists, docs, generated files |
| [12](#12-unit-tests-and-tool-regressions) | Unit tests, LSP and formatter regressions | small C++ and Python tests | per push | local logic errors |
| [13](#13-static-analysis-and-secret-scanning) | CodeQL, Coverity, gitleaks | the source | CodeQL and gitleaks per push and weekly; Coverity by hand | classes of static defects |
| [14](#14-performance-checks) | Perf gate, library benchmarks, baselines | benchmark programs | perf gate per push (report-only) | gross relative slowdowns |
| [15](#15-release-gates-and-long-runs) | Soak, platform verification, signing checks | built release candidates | by dispatch | long-running leaks, per-platform release failures |

Everything in section 4 to 9 that is "nightly" is a step of one workflow; its steps are tabulated in [Nightly hardening](#nightly-hardening-what-one-night-runs).

---

## 1. The regression suite

- **What it checks.** Compiler, VM, JIT, GC and library behaviour through self-asserting programs: core language, generics, closures, records, strings, collections, exceptions, AMD64 and ARM64 JIT code generation (`jit_*`, `arm64_*`), minor and major collection and write barriers, threads, JSON, XML, regex, networking and TLS, ODBC, ML and AI, OpenGL, ONNX, MCP and LSP entry points. [TESTS.md](../programs/regression/TESTS.md) lists each test and its category.
- **Input.** 812 `.obs` files in `programs/regression/`. Each compiles as `obc -opt s3 -lib cipher,collect,xml,json` (plus its `# EXTRA_LIBS:` libraries) and runs once with `obr`. Pass means exit 0 (a printed `SKIP:` line makes it a skip); the full rules, timeouts and directives are in [programs/regression/README.md](../programs/regression/README.md) and in the runners [run_regression.sh](../programs/regression/run_regression.sh) and [run_regression.cmd](../programs/regression/run_regression.cmd).
- **Where it runs.** Per push on all five legs, twice: as is, and with `OBJECK_JIT_THRESHOLD=1` so every method is compiled on its first call (steps "Run regression tests" and "...with every method compiled" in [ci-build.yml](../.github/workflows/ci-build.yml)). Nightly it runs again under the heap verifier (section 9). `OBJECK_GL_REQUIRED` and `OBJECK_REQUIRE_ODBC` turn the OpenGL and ODBC environment skips into failures on the legs that have those. Three tests skip without their service (`api_gemini_test`, `api_openai_test`, `api_ollama_test`).
- **Locally.** `cd programs/regression && ./run_regression.sh x64` (POSIX) or `run_regression.cmd x64` (Windows) against a built deploy tree; `REGRESSION_SHARD=i/n` shards the POSIX runner; `tools/cicd/local_linux.sh` builds the committed tree in a clean copy and runs it (also `local_docker.sh`, `--jit1`, `--flags`).
- **Cannot find.** A bug the test's own assertions do not cover, anything that needs `-opt s0`, a different library list or a particular interleaving (the suite compiles at `s3` only and runs each test once per mode), and a race with a low per-run probability.

Lints that keep the suite honest, per push in the `tools` job: [check_test_exit_codes.py](../tools/cicd/check_test_exit_codes.py) (a test that prints `FAIL` must also exit non-zero; before it existed 125 of 168 asserting tests could print `FAIL` and still pass, per the workflow comment), [check_jit_optouts.py](../tools/cicd/check_jit_optouts.py) (an opt-out from the JIT needs a stated reason) and [check_diff_markers.py](../tools/cicd/check_diff_markers.py) (markers that remove coverage need a reason; nondeterministic-output tests are capped at 5% of the suite).

## 2. Negative tests: `bad_*.obs` and `EXPECT_*` markers

- **What it checks.** That the compiler or VM *rejects* what it should, with the right message: syntax errors, type and overload errors, ambiguous `use` aliases, literal range errors, generics, named arguments, `ref` parameters, jagged arrays, readonly records, nesting limits, runtime bounds, null, divide-by-zero and stack-overflow behaviour. 128 files are named `bad_*`; 139 files carry `# EXPECT_COMPILE_ERROR: <message>` (116) or `# EXPECT_RUNTIME_ERROR` (23); 37 add `# EXPECT_ERROR_COUNT: <n>`.
- **Input.** The `.obs` files themselves.
- **Where it runs.** Inside the regression suite (section 1).
- **Locally.** Same runner; to see one result, compile it: `obc -src programs/regression/bad_call_undefined.obs -lib cipher,collect,xml,json`.
- **Cannot find.** For a compile-error test the match is a substring of the compiler output, so a *different* error that contains the same words passes unless an error count pins it. For `EXPECT_RUNTIME_ERROR` any non-zero exit with output passes, so a crash in place of the intended error also passes; [VM flag tests](#8-vm-flag-and-vm-behaviour-tests) check the exit status and text for the few cases where that matters. No negative test says which *other* programs are wrongly accepted; that is what mutation testing (next) probes.

## 3. Compiler robustness: mutation and stress

- **What it checks.** That the compiler front end, given wrong code, neither crashes nor hangs and reports errors near the fault without a cascade. Gates (from the script header): no crash or hang ever (exit status 0 to 3 within a time limit); at least 80% of mutations report an error on or within two lines of the mutation; at most 5% cascade into more than four errors; at most 5% report only far away; at most 15% compile with no error.
- **Input.** Valid regression programs broken in sixteen ways (a `;`, `)`, `(`, `{`, `}`, `,` or keyword deleted, `:=` written `=`, an identifier misspelled, a string left open, a malformed number, a stray character, a swapped type, a doubled operator, a truncated file, a call to a nonexistent method), and hostile files (empty, NUL, random bytes, invalid UTF-8, deep nesting of parentheses, blocks, calls and generics, thousands of chained operators, a 2 MB string, 20000 errors, a call nested 30 deep). Seed and program count are fixed by default, so a failure reruns identically.
- **Where it runs.** Per push on every leg, step "Compiler robustness, mutation and stress".
- **Locally.** `python programs/regression/run_compiler_robustness.py <bin_dir> [--programs N] [--seed S]` ([source](../programs/regression/run_compiler_robustness.py)).
- **Cannot find.** Wrong *code generation* (nothing is run), a wrong message that happens to land near the mutation, errors in constructs the corpus programs do not contain, and anything the gates' thresholds tolerate (up to 15% of mutations may compile silently).

## 4. Differential regression

- **What it checks.** That every regression test prints the same stdout and agrees on success under five configurations: `s0/off` (no optimizer, interpreter only; the reference), `s3/off`, `s3/default` (what users run), `s3/jit1` (every method compiled) and, with `--rotate`, `s0/jit1`. It exists because a miscompile that only the optimizer or the JIT makes passes the ordinary suite whenever the test's own assertions do not touch the affected value.
- **Input.** Every `programs/regression/*.obs` (skipping compile-error tests), with per-test markers (`DIFF_CONFIGS`, `DIFF_REQUIRES_JIT`, `NONDETERMINISTIC_OUTPUT`, `DIFF_SERIAL`, `DIFF_TIMEOUT`) that must carry a stated reason.
- **Where it runs.** Nightly only, all five legs, step "Differential regression" (`-j 3`, 55-minute step). Its unit test [test_run_differential.py](../programs/regression/test_run_differential.py) runs per push in the `tools` job, against fake `obc` and `obr`.
- **Locally.** `python3 programs/regression/run_differential.py <bin_dir> [-j N] [--rotate] [tests...]` ([source](../programs/regression/run_differential.py)).
- **Cannot find.** A defect all configurations share (a front-end bug, a library bug); output that differs only on stderr (not compared); behaviour the test does not print; the tests marked `NONDETERMINISTIC_OUTPUT`, whose output is not compared.

## 5. Differential program fuzzer

- **What it checks.** Whether a randomly generated, deterministic program prints the same digest and exit status under five configurations (`s0/off` reference, `s3/off`, `s3/default`, `s3/jit1`, `s0/jit1`), and whether the JIT is actually exercised: the run fails if fewer than 70% of generated methods are positively reported compiled (when at least 20 were sampled), so a fuzzer whose code never reached the JIT cannot pass.
- **Input.** Programs from a typed-AST generator driven by a recorded choice stream, sampling feature layers F1 to F6 (integers and control flow, floats, calls and virtual dispatch, `select` and strings, function references and zero-argument closures, allocation-heavy object graphs). The generator avoids undefined behaviour by construction. Failures are reduced with [reduce.py](../tools/fuzz/reduce.py); findings are deduplicated by signature against [known.json](../tools/fuzz/known.json) (empty today).
- **Where it runs.** Nightly on all five legs (200 programs per leg by default, seed = the UTC date), and one batch per iteration of the soak.
- **Locally.** `python tools/fuzz/run_fuzz.py --bin core/release/deploy-x64/bin --count 300 --seed 1 -j 6`; `--replay SEED` reruns one program. Details, files and the fault-injection tests that prove the fuzzer can fail: [tools/fuzz/README.md](../tools/fuzz/README.md).
- **Cannot find.** Anything outside the generated subset (no threads, I/O, exceptions, generics or library calls), GC-timing bugs (no configuration sets a nursery size or verifier), defects every configuration shares, and anything at the integer-overflow or divide-by-zero edge (excluded by construction).

## 6. EMI fuzzing

- **What it checks.** "Equivalence modulo inputs" on real programs: variants that insert dead code behind a guard the optimizer cannot fold, delete blocks that a probe run shows never execute, or rewrite expressions to identity forms must print exactly what the original prints under `--jit=off` and `--jit=1` at `s0` and `s3`.
- **Input.** Regression tests that compile, exit 0, have no `EXPECT_*`, nondeterminism, network, thread or timing marker and whose output repeats.
- **Where it runs.** **Not in any workflow**; by hand only.
- **Locally.** `python tools/fuzz/emi.py --bin <bin_dir> --variants 10 --seed 1 -j 8` (`--list` prints eligible tests).
- **Cannot find.** The same classes as section 5 plus anything in the tests it excludes; since it is never scheduled, its coverage is whatever someone last ran.

## 7. The `.obe` loader fuzzer

- **What it checks.** That `obr`'s loader, which reads an `.obe` before anything runs, refuses every malformed input with a message and never crashes, hangs, leaks or allocates without bound. An `.obe` is untrusted input to `obr` ([SECURITY_MODEL.md](SECURITY_MODEL.md) lists what the loader and the verification pass `Loader::Verify` check, and what is still not checked).
- **Input.** Two libFuzzer targets built with clang, AddressSanitizer and UBSan: `loader_fuzz` (a decompressed image) and `loader_file_fuzz` (an `.obe` file as `obc` writes it). The seed corpus is every regression program compiled by the deploy tree's `obc`, plus slices cut from them.
- **Where it runs.** Two places. The nightly `loader-fuzz` job (linux-x64, 30 minutes of fuzzing, 60-minute step) files issues for new findings. The advisory `Loader fuzz (advisory)` job in `ci-build.yml` runs a 5-minute pass on every push, against the linux-x64 tree that run built; it is `continue-on-error`, is not in the `CI Status` job's `needs`, and reports only as a warning annotation and a job summary, so it can never block a merge; saved inputs upload as `loader-fuzz-advisory`.
- **Locally.** Needs clang and `libclang-rt`: `python3 tools/fuzz/loader/run_nightly.py --bin <bin_dir> --work <dir> --minutes 5 --seed 1 --out <dir>`, or the manual commands in [tools/fuzz/loader/README.md](../tools/fuzz/loader/README.md). Every finding found so far is fixed and is a case in section 30 of `run_vm_flag_tests.py` (reduced inputs in `programs/regression/vm_flag_fixtures/loader/`).
- **Cannot find.** What the loader accepts but is wrong in meaning: SECURITY_MODEL.md states that a crafted `.obe` can still corrupt memory through the operand stack or a trap's arguments, because the verification pass does not check what a trap pops or operand types in general (stack underflow, joins at different heights and a non-zero literal read as a reference have been refused since PR #1598, and a method that holds more than 1,000 operand words since the follow-up that bounds the stack). Five minutes per push explores little; 30 nightly minutes is a sample, not a proof. It runs on linux-x64 only, so Windows- and ARM64-specific loader paths are not fuzzed.

## 8. VM flag and VM behaviour tests

- **What it checks.** `obr` and `obc` behaviours the regression runner cannot see because it passes no VM flags and accepts any non-zero exit: flag validation (`--jit`, `--gc-threshold`, `--nursery`, `--call-depth`, `--lib-path`), identical output under `--jit=off`, default and `--jit=1`, exit status of a program that dies inside the VM, the zero-divisor message, non-ASCII paths and console output, locale failures, the heap verifier and its injected faults, the GC reference-map audit (`OBJECK_GC_ROOTS=audit`), Int and `Int[]` values that equal an object's address surviving a collection, JIT compilation of specific shapes (conditional values, float limits, multi-dimensional index registers), `--jit=off` truly interpreting `native` methods, a fatal compiler error leaving through the command line, and, in section 30, loader refusals and one mutated operand per verification rule.
- **Input.** Fixtures in `programs/regression/` and [programs/regression/vm_flag_fixtures/](../programs/regression/vm_flag_fixtures) (including `loader/`).
- **Where it runs.** Per push on every leg ("Run VM flag tests").
- **Locally.** `python programs/regression/run_vm_flag_tests.py <bin_dir>` (about ten minutes by the script's own note); `--only gc_roots` (or any `check_` function name fragment) runs just those sections ([source](../programs/regression/run_vm_flag_tests.py), whose header describes each numbered section and the bug that motivated it).
- **Cannot find.** Anything not shaped like one of its fixtures; each check was added after a specific defect, so it guards known failure shapes rather than searching for new ones. The MinGW (MSYS2) build is checked by hand (`--unicode-only`), not in CI.

## 9. GC stress and the `OBJECK_GC_*` verification modes

The generational collector (nursery plus old generation, write barrier, minor and major collections) is the component with the hardest bugs to see, so it has the most dedicated machinery. All modes are inert unless set.

| Knob | Effect | Source |
|---|---|---|
| `OBJECK_GC_VERIFY=<N>` or `checkmark` | heap verifier on the collecting thread inside stop-the-world: checks the write-barrier remembered set (A2), old-generation headers (B1), that typed references are zero or live old objects of the right type (B2), that none points into the nursery after a collection (B3), frame slots (B4), that every `Char[]` ends in its zero spare element (B5), fixup (C), and with `checkmark` that every young object reachable by a typed trace is marked (A1). `N` is a collection period for the expensive whole-old-generation walk (the first 8 collections are always full) | [memory_verify.cpp](../core/vm/arch/memory_verify.cpp) |
| `OBJECK_GC_VERIFY_INJECT=field\|barrier\|mark\|terminator` | plants a fault so the verifier can be shown to catch one (tested in `run_vm_flag_tests.py` section 11) | same |
| `OBJECK_GC_ROOTS=audit` or `conservative` | `audit` computes the operand-stack reference maps at every collection and checks them against the live stacks without changing what is marked; `conservative` runs the old "treat every word as maybe a reference" behaviour, used as a negative control. `OBJECK_GC_ROOTS_DUMP` dumps maps | [memory_verify.cpp](../core/vm/arch/memory_verify.cpp), [GC_ROOTS_DESIGN.md](GC_ROOTS_DESIGN.md) |
| `OBJECK_NURSERY=<size>` / `--nursery` | nursery size (`256k` forces collections every few allocations) | [vm_options.h](../core/vm/vm_options.h) |
| `--gc-threshold=<size>` | old-generation collection threshold (`64k` forces frequent major collections) | same |
| `OBJECK_GC_STATS=1` | prints a collection summary (counts, pauses, promoted bytes, peak RSS) on exit | [memory.cpp](../core/vm/arch/memory.cpp) |
| `OBJECK_GC_TRACE` | collection trace | same |
| `OBJECK_STW_WATCHDOG_MS=<n>` | a collection that has waited more than `n` ms for other threads to park prints, once, which threads have not (thread id, innermost method, instruction pointer, callers) and keeps waiting. A native call that blocks without `BeginBlocking`/`EndBlocking` otherwise hangs the VM with no output. Off by default and free when off. `runtime.test.stall_ms` (needs `OBJECK_GC_TEST_HOOKS=1`) is the fixture's stand-in for such a call | [memory.cpp](../core/vm/arch/memory.cpp), `stw_watchdog.obs` |
| `OBJECK_GC_TEST_HOOKS=1` | enables `Runtime->TestAddressOf(o)` so a test can hold an object's address as an `Int` across a collection | [common.cpp](../core/vm/common.cpp) |
| `OBJECK_JIT_THRESHOLD=N`, `OBJECK_JIT_DISABLE=1`, `--jit=off\|1`, `OBJECK_JIT_REPORT=1` | JIT call-count threshold (default 10), JIT off, force compilation, and a per-method compile report used by the fuzzer and the flag tests | [jit_common.h](../core/vm/arch/jit/jit_common.h) |

- **Dirty-list overflow.** The write barrier records old objects that gain young references in a 65536-entry list; past it, a minor collection scans all of the old generation instead. Programs rarely get there, so `programs/regression/dirty_list_overflow.sh <deploy-dir> [deps-prefix]` builds a VM from a copy of `core/vm` with `EXTRA_FLAGS=-DOBJECK_DIRTY_LIST_MAX=64` (every collection overflows), runs `dirty_list_overflow.obs` on it and on the deployed VM, interpreted and compiled with a 256k nursery, and requires the same checksum from both. It is not part of `run_regression.sh` (the build takes minutes); run it after touching the write barrier, the dirty list or the minor collector. POSIX only.
- **Stress fixtures.** `minor_gc_stress`, `core_thread_gc_stress` (12 threads x 25000 iterations x 10 rounds), `jit_gc_stress` and `gc_mt_small_nursery_stress` in `programs/regression/`.
- **Where it runs.** Per push: the stress fixtures are ordinary regression tests, run again with every method compiled. Nightly on all five legs: the whole suite under `OBJECK_NURSERY=256k OBJECK_GC_VERIFY=1` ("minor stress"), the whole suite under `OBJECK_VM_ARGS=--gc-threshold=64k OBJECK_GC_VERIFY=1` ("major stress"), and the three stress fixtures looped 20 times each in `--jit=off` and `--jit=1` (45-minute budget; the result file is rewritten after every run). `stress-probe.yml` (pushes to `probe/**`, dispatch) loops the GC and JIT fixtures in four modes (default, `jit1`, `nursery256k`, `verify256k`) on all five platforms and reports runs, failures and the rule-of-three bound (3/n) per cell. `soak.yml` runs them for hours beside the site server.
- **Locally.** For example `OBJECK_NURSERY=256k OBJECK_GC_VERIFY=1 ./run_regression.sh x64`, or `OBJECK_VM_ARGS=--gc-threshold=64k OBJECK_GC_VERIFY=1 ./run_regression.sh x64`, in `programs/regression`; `run_vm_flag_tests.py --only gc_roots`. `check_native_write_barrier.py` (section 11) scans native libraries for unbarriered stores.
- **Cannot find.** The verifier trusts only references the collector can *type* (statics, declared frame slots, declared fields, closure captures): operand stacks, JIT temporaries and pending-thread roots legitimately hold stale words and are not checked, so a stale operand-stack root is the class of defect it can miss (the reference-map audit exists for exactly that). Races need real interleavings: a failure rate of 0.1% needs thousands of runs, and "no failure in n runs" bounds the rate at about 3/n, nothing more. Two regression tests carry `GC_STRESS_SKIP` (timeouts under a 64k threshold) and are not exercised in that mode.
- **Sanitizers.** The VM and compiler are not built with AddressSanitizer or UBSan in CI; the only sanitizer build in the repository is the loader fuzz targets (`tools/fuzz/loader/build.sh`).

## 10. Debugger and DAP tests

- **What it checks.** `obd` command-line scenarios (scripted with `expect` on POSIX) and Debug Adapter Protocol sessions (`obd --dap` over stdio): breakpoints, stepping, variables, `setVariable`, function breakpoints, logpoints, restart, exception breakpoints, threads, data breakpoints, stepInTargets, goto, cancel, progress and invalidated events, disassemble, non-ASCII program, source and output names, teardown.
- **Input.** Programs compiled with debug symbols, and the scenario scripts in `programs/regression/` ([run_debugger_tests.sh](../programs/regression/run_debugger_tests.sh), [run_dap_tests.py](../programs/regression/run_dap_tests.py), `dap_*_test.py`).
- **Where it runs.** Per push, every leg.
- **Locally.** `./run_debugger_tests.sh x64` (needs `expect`), `python3 run_dap_tests.py <bin_dir>` in `programs/regression`.
- **Cannot find.** Behaviour in editors' own clients, and stack-walk bugs in programs the scenarios do not use.

## 11. Build-free consistency checks

- **What it checks.** That two hand-maintained things still agree: library lists across five files and `lib_src`; doc-comment code fences; install instructions against `runtime_deps.json`; Markdown links and anchors; site pages against their Markdown; every VM trap and intrinsic wired through `traps.h`, the scanner, the parser, the emitter and `ProcessTrap` (with a self-test that mutates the sources); every native library that stores a received object calls the write barrier (with a positive control); the editor API index class set; the shipped example index; `.in` templates against generated files; every committed `.obl` carrying the toolchain version; every `SHA256SUMS` upload being re-signed; the README publishing the committed release key; the benchmark set and inputs against [performance.md](performance.md); the differential and verifier markers; apt retry logic. Many carry a `--test` or companion test proving the check can still fail.
- **Input.** The repository tree; nothing is built.
- **Where it runs.** Per push in the `tools` job (30-minute cap) of `ci-build.yml`, one step per check; and locally.
- **Locally.** `python tools/cicd/local_ci.py` (all, in parallel), `python tools/cicd/local_ci.py --list`, `python tools/cicd/local_ci.py doc` (name filter), `--self-test` (every check the workflow runs is listed). Catalogue and the scripts' roles: [tools/cicd/README.md](../tools/cicd/README.md).
- **Cannot find.** Whether either side of a pair is *correct*; only that they disagree. `check_sdl_natives.py` is run by no workflow.

## 12. Unit tests and tool regressions

- **What it checks.** The old generation's pointer set (growth, erase during iteration, tombstone reuse) in [ptr_set_test.cpp](../core/vm/arch/ptr_set_test.cpp) and the free-block size-class rounding in [size_class_test.cpp](../core/vm/arch/size_class_test.cpp), both compiled with `g++` and run on the POSIX legs; the code formatter and the language server in the `tools` job (`bash tools/lsp/server/format_code/run_regression.sh`, `python3 tools/lsp/tests/run_lsp_tests.py <bin_dir>`); the updater (`obu`) and its signature verification ([test_obu_verify.py](../tools/cicd/test_obu_verify.py), against a fixture signed with the real key); `API.Inference` over a mock server; `obi`/`obr` parity on compiled-in protocols; library link, generics and array-copy round trips through `.obl` files; and the fuzzer's, nightly triage's and Coverity-upload flow's own tests ([tools/fuzz/test_*.py](../tools/fuzz/README.md), [tools/cicd/test_nightly_triage.py](../tools/cicd/test_nightly_triage.py), `core/release/cov_scan_test.py` against a mock Coverity server).
- **Where it runs.** Per push (the steps named in `ci-build.yml`); the fuzzer and triage unit tests partly in the nightly's triage job.
- **Cannot find.** Interactions between components; these are narrow, mostly regression guards for specific past defects.

## 13. Static analysis and secret scanning

| Tool | What it checks | Input | Where | Locally | Cannot find |
|---|---|---|---|---|---|
| CodeQL ([codeql.yml](../.github/workflows/codeql.yml)) | default query suites for C/C++, JavaScript/TypeScript and Python (the `queries:` override is commented out, so GitHub's default suite applies) | the repository; C/C++ is built manually with `deploy_posix.sh x64` on Linux | push and PR to `master`, weekly (Monday 13:39 UTC); findings land in GitHub code scanning | not provided; use the CodeQL CLI with the same build | Windows-, macOS- and ARM64-only code (the C/C++ analysis is one Linux x64 build); dynamic and logic defects; Objeck source itself |
| Coverity Scan | a full Coverity build capture of the toolchain, uploaded to scan.coverity.com | a clean rebuild under `cov-build` | **by hand**: no workflow runs the scan (CI only runs [cov_scan_test.py](../core/release/cov_scan_test.py), the large-submission flow against a mock server) | `core/release/cov_scan.sh [x64\|arm64]` (needs the Coverity install and a project token; the script header lists the variables) or `cov_scan.cmd` | the same limits as any static analyser; one platform per capture; the results page is external, so what was found and fixed is not reproducible from this repository |
| gitleaks ([secret-scan.yml](../.github/workflows/secret-scan.yml)) | committed secrets in the tree | the working tree (history is never scanned there) | push to `master`, PRs, weekly (Monday 06:00 UTC), dispatch | run `gitleaks` on the tree | secrets in history; secrets in a format its rules do not know |

GitGuardian is mentioned in [release_integrity.md](release_integrity.md) as an external service; nothing in this repository configures it, so it is not described here.

## 14. Performance checks

- **Perf gate** ([perf-gate.yml](../.github/workflows/perf-gate.yml), [PERF_GATE.md](../perf-results/PERF_GATE.md)): Objeck against Java and LuaJIT on four CI-scaled CLBG programs, gating on the ratio. **Report-only** (`--no-fail`), so it cannot fail a build, and its committed baseline predates the F7 calling convention.
- **Library benchmarks** ([perf-results/libs/README.md](../perf-results/libs/README.md)): standard-library cases timed against an in-run plain-array reference. Not run in CI; baseline is Windows AMD64 only.
- **Baselines and sweeps** ([perf-results/baseline-2026.10.0/README.md](../perf-results/baseline-2026.10.0/README.md)): master against v2026.9.4, interleaved, 15 runs, Mann-Whitney p-values, on an Apple M4 Max; the nursery sweep on three platforms.
- **Published numbers** ([performance.md](performance.md)): one Docker run on a 7950X3D, three runs per cell, wall-clock; the page states its conditions and limits.
- **Cannot find.** A regression the benchmarks do not exercise, or one smaller than run-to-run noise on a shared runner; none of these records variance of the ratios.

## 15. Release gates and long runs

- **Soak** ([soak.yml](../.github/workflows/soak.yml), [soak.py](../tools/cicd/soak.py)): up to 330 minutes per leg of the objeck.org site server under steady client load (whole-file GETs, ranges, bad ranges, missing paths, traversal attempts, bodies compared byte for byte with the file on disk, resident memory sampled), beside loops of the regression suite, the GC stress loop and the program fuzzer; stops at the first failure. Dispatch only, on the commit about to be tagged. Cannot find: anything the site server's request mix does not touch, and a leak too slow to show in 5.5 hours.
- **Platform verification** ([verify-platforms.yml](../.github/workflows/verify-platforms.yml), [verify_platform.sh](../tools/cicd/verify_platform.sh) and `.cmd`): per platform, builds the deploy tree as the release does, runs the regression suite twice and the VM flag, debugger and DAP tests, and writes a verdict file; passes only if every requested leg passes at the same commit. Cannot find: anything the per-push suite also misses; its value is the record and the single-commit guarantee.
- **Signing and manifest checks:** release signing and `SHA256SUMS` signing are described, with their limits, in [release_integrity.md](release_integrity.md); weekly credential checks are in `verify-signing-credentials.yml`.

---

## Nightly hardening: what one night runs

[nightly-hardening.yml](../.github/workflows/nightly-hardening.yml), daily at 07:23 UTC and on dispatch. Resolves the latest successful master `ci-build` run (needs all five `objeck-<platform>` artifacts unexpired), then, per leg:

| Step | Command (abridged) | Budget |
|---|---|---|
| Differential regression | `run_differential.py <tree>/bin -j 3` | 55 min |
| Fuzzer | `tools/fuzz/run_fuzz.py --bin ... --leg <leg> --count 200 --seed <UTC date>` | 45 min |
| Regression, heap verifier, minor stress | `OBJECK_NURSERY=256k OBJECK_GC_VERIFY=1 TEST_TIMEOUT=300`, the regression runner | 70 min |
| Regression, heap verifier, major stress | `OBJECK_VM_ARGS=--gc-threshold=64k OBJECK_GC_VERIFY=1 TEST_TIMEOUT=300` | 70 min |
| GC stress loops | `nightly_triage.py loop --tests minor_gc_stress,core_thread_gc_stress,jit_gc_stress --runs 20 --modes off,1 --timeout 300 --budget 2700` | 50 min |
| Loader fuzz (linux-x64 only, own job) | `tools/fuzz/loader/run_nightly.py ... --minutes 30` | 60 min |

A `Triage` job sorts every failure into infrastructure, known (`tools/fuzz/known.json`) or new; only new ones open issues (one tracking issue per run, plus one per cross-leg signature, crash or verifier violation). Each step records an "unfinished" marker first, so a hang is a recorded failure rather than a lost result, and a tool missing from the checkout fails its step instead of skipping. Limits: it tests the master binaries CI already built, not a fresh build; a leg whose step times out reports a failure, not a partial pass; the nightly seed is shared across legs.

## What nothing here covers

Stated plainly so that absence is not read as coverage:

- **No code-coverage measurement** of the compiler, VM or libraries (no gcov, llvm-cov or similar in any workflow or Makefile), so which lines the suites execute is unknown.
- **No cross-implementation oracle.** Every differential comparison is between configurations of the same toolchain; there is no second Objeck implementation, and no semantic specification the outputs are checked against beyond the tests' own assertions.
- **No formal verification** of the verifier, the collector or the JIT, and no model checking of the stop-the-world protocol.
- **Sanitizers** run only on the loader fuzz targets; the VM, JIT and compiler are not run under ASan, UBSan or TSan in CI.
- **Hardware and platform spread:** the JIT backends are AMD64 and ARM64; per-push CI runs both on native runners, but nothing runs other CPUs, other GPU or ONNX execution providers, or the optional MSYS2 build.
- **Operand-stack safety of the loader** is listed as unchecked in [SECURITY_MODEL.md](SECURITY_MODEL.md); the loader fuzzer therefore cannot show it safe.
- **Library behaviour against real services** (HTTP/2 and HTTP/3 against live servers, cloud AI APIs): the network tests are non-gating and the API tests skip without a key.
- **Long-horizon behaviour beyond the 5.5-hour soak**, and any workload other than the ones in the soak and the benchmarks.

## Reproducing a number or a result

1. Build a deploy tree (`core/release/deploy_posix.sh x64`, `deploy_macos_arm64.sh`, or `deploy_windows.cmd x64`), or take one from a CI run's `objeck-<platform>` artifact.
2. Run the command given in the entry. All runners take the deploy tree's `bin` directory (the regression runners take `x64` or `arm64` and find `core/release/deploy-<arch>`).
3. For anything probabilistic, record the seed, the leg and the commit; the nightly and the fuzzers print their seed, and `tools/cicd/local_linux.sh [REF]` builds an exact commit in a clean copy.
4. Do not time or stress-test on a machine that is busy: the suites' timeouts and the performance numbers both assume a quiet host.
