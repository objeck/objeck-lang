# Objeck Language Testing Infrastructure

Complete guide to testing the Objeck language compiler, virtual machine, and libraries.

## Overview

The Objeck project has **3 primary test suites** with different purposes, plus the fuzzers, verifiers and static analysis listed in [docs/TESTING_AND_ASSURANCE.md](../docs/TESTING_AND_ASSURANCE.md), which says for each mechanism what it checks, where it runs, how to run it and what it cannot find:

| Suite | Location | Tests | Purpose | CI Coverage |
|-------|----------|-------|---------|-------------|
| **Regression Suite** | `programs/regression/` | 812 `.obs` files (count from the generated [TESTS.md](regression/TESTS.md)) | Fast, focused regression detection | ✅ All, on 5 platforms, twice per platform |
| **Comprehensive Suite** | `programs/tests/` | 327 numbered `prgm*.obs` plus about 27 single-purpose programs | Older feature programs and benchmarks; **no expected output, no runner** | ⏳ 4 network programs (non-gating: `prgm_http2`, `prgm_http3`, `http3_downgrade_test`, `http_header_echo_test`) |
| **Demo Suite** | `programs/deploy/` | 32 indexed programs (plus `welcome.obs`, the index) | Real-world usage examples | ✅ 13 programs compiled and run, 4 more compiled only |

What a green CI run on these suites does and does not establish is summarised under [Limits](#limits-of-this-test-evidence) below.

---

## 1. Regression Test Suite

**Location:** `programs/regression/`
**Purpose:** Fast, automated regression detection for critical functionality
**Size:** 812 `.obs` files in the checkout this page was checked against (the generated [TESTS.md](regression/TESTS.md) says 812 runtime tests plus 14 debugger tests and lists every one; 128 files are named `bad_*`, and 139 carry an `EXPECT_COMPILE_ERROR` or `EXPECT_RUNTIME_ERROR` marker, so they pass only when an error is produced)
**Execution Time:** the workflow comment in `ci-build.yml` puts a pass at about three minutes per leg; not otherwise recorded

### Quick Start

The runners use the deploy tree under `core/release` (`deploy-<arch>`, else `deploy`), so build it first (see [Testing by Platform](#5-testing-by-platform)).

```bash
# Linux/macOS
cd programs/regression
./run_regression.sh x64

# Windows
cd programs\regression
run_regression.cmd x64      # x64 build
run_regression.cmd arm64    # ARM64 build
```

### What It Tests

- **Core language**: classes, generics, closures, records, strings, collections, exceptions
- **JIT**: AMD64 and ARM64 code generation (`jit_*`, `arm64_*`)
- **GC and concurrency**: minor and major collection, write barriers, threads
- **Negative tests** (`bad_*`): code the compiler or VM must reject, with the expected error
- **Libraries and tools**: JSON, XML, regex, networking, ODBC, ML/AI, OpenGL, ONNX, debugger, LSP, MCP

[TESTS.md](regression/TESTS.md) has the per-category counts and a line on each test.

### How a test passes, and what that proves

Each test is one `.obs` program compiled as `obc -opt s3 -lib cipher,collect,xml,json` (plus its `# EXTRA_LIBS:` line) and run once with `obr` (`run_regression.sh` / `run_regression.cmd`; the exact rules are in [regression/README.md](regression/README.md)):

- **Ordinary test:** passes when `obr` exits 0. Whether the test *checks* anything is up to the test; CI's `check_test_exit_codes.py` rejects a test that prints `FAIL` and has no non-zero exit path. A test that prints `SKIP:` at the start of a line and exits 0 is counted as skipped, not passed.
- **`# EXPECT_COMPILE_ERROR: <message>`:** passes only when compilation fails *and* the compiler's output contains `<message>`; `# EXPECT_ERROR_COUNT: <n>` additionally pins the number of error lines.
- **`# EXPECT_RUNTIME_ERROR`:** passes on any non-zero exit with some output. It does **not** check the message or the exit code, so a different crash passes; `run_vm_flag_tests.py` covers the few runtime errors whose text and status matter (zero divisor, escaped VM error, loader refusals).
- **Timeout:** 60 s per test on POSIX, 300 s on Windows, `TEST_TIMEOUT` to change; a timeout is a failure even for a negative test.
- **Only `-opt s3`, default libraries, one run.** The optimizer-off configuration, the interpreter-only configuration and repeated runs are covered elsewhere (the differential runner and the stress loops, nightly only; the second CI pass with `OBJECK_JIT_THRESHOLD=1`).

### Limits of this test evidence

- A test finds what its author thought to assert. Most tests assert a few of the values they print; the nightly differential runner (`run_differential.py`) exists because a miscompile can pass those assertions.
- A passing suite on a platform says nothing about code paths that need hardware the runner lacks: the OpenGL, ODBC and ONNX tests skip or require specific set-up, and CI sets `OBJECK_GL_REQUIRED` and `OBJECK_REQUIRE_ODBC` on the legs where they are meant to run so a skip becomes a failure there. Three tests skip when their service is absent: `api_gemini_test` (no Gemini key), `api_openai_test` (no OpenAI key) and `api_ollama_test` (Ollama not running).
- Concurrency and GC races are probabilistic (a failure rate of 0.1% needs thousands of runs to see); the regression suite runs each test once per mode. The nightly stress loops and the soak exist for that, see the assurance map.

### CI Integration

Runs automatically on every:
- Push to master
- Pull request to master

It runs on all five platforms (Windows x64/ARM64, Linux x64/ARM64, macOS ARM64), twice on each: once as is and once with `OBJECK_JIT_THRESHOLD=1`, which compiles every method on its first call.

See `.github/workflows/ci-build.yml` for configuration.

### Documentation

- [README.md](regression/README.md) - Usage, directives and the other runners
- [TESTS.md](regression/TESTS.md) - Generated list of every test, by category

---

## 2. Comprehensive Test Suite

**Location:** `programs/tests/`
**Purpose:** Exhaustive validation of compiler and VM correctness
**Size:** 327 `prgm*.obs` files (`prgm0` to `prgm351`, numbered with gaps), plus about 27 other programs in the directory
**Execution Time:** not recorded (the figure this page used to give, 30-60 minutes, has no measurement behind it in the repository)

### Structure

```
programs/tests/
├── prgm1.obs through prgm351.obs    # Main test programs
├── aoc_2022/                        # Advent of Code 2022 solutions
├── aow/                             # Advent of Code solutions
├── clbg/                            # Computer Language Benchmarks Game
├── data/                            # Test data files
│   ├── JSON test files
│   └── Images for ML tests
└── test_mbedtls_migration.obs       # Crypto library tests
```

### Running Tests

There is no runner for this suite. The old `core/compiler/regress.sh` expected `core/compiler/test_src/` and an `update_libs` script, neither of which exists any more, and was removed.

**Run individual test** (with `obc` and `obr` from a deploy tree or an install on `PATH`):
```bash
cd programs/tests
obc -src prgm10.obs -opt s3 -dest prgm10.obe    # add -lib <libraries> as the program needs, e.g. -lib xml,json
obr prgm10.obe
```

### Test Categories

| Category | Example Files | Coverage |
|----------|--------------|----------|
| **Language features** | prgm1-50 | Arrays, functions, classes, generics |
| **Collections** | prgm63-64, prgm100-111 | Vector, Map, List, Hash |
| **String operations** | prgm1, prgm102-103 | Parsing, manipulation |
| **Networking** | prgm102-103 | HTTP, HTTPS, XML/RSS |
| **JSON** | prgm3, prgm102-103 | Parsing and generation |
| **Encryption** | prgm35, prgm38, prgm63-64 | Cipher operations |
| **Threading** | prgm6 | Concurrent execution |
| **Regex** | prgm4 | Pattern matching |
| **Database** | ODBC tests | Database operations |
| **Graphics** | SDL2 tests | Rendering |
| **Machine Learning** | ml_simple_test, ml_enhancements_test | System.ML (ONNX tests are under `frameworks/opencv_onnx/tests/`) |
| **Benchmarks** | clbg/* | Performance tests |

### Special Tests

**test_mbedtls_migration.obs** - Comprehensive crypto test
```bash
cd programs/tests
obc -src test_mbedtls_migration.obs -lib cipher -dest test_crypto.obe
obr test_crypto.obe
```

Prints a PASS/FAIL line per case (the "40+ cases" figure this page used to give was not re-counted)

### Framework-Specific Tests

```
programs/frameworks/
├── json/tests/           # two JSON data files (test0.json, test4.json), no program
├── sdl/engine/tests/     # SDL graphics demos (10 programs; need a display)
└── opencv_onnx/tests/    # models and image data only; the programs are one level up
                          # (test_all.obs, test_face_detect.obs, demo_*.obs)
```

The ONNX tests that run in CI live in `programs/regression/` (`onnx_*`, `nlp_onnx_*`), and the face-detection test in `programs/frameworks/opencv_onnx/` runs non-gating on linux-x64.

---

## 3. Demo/Deploy Test Suite

**Location:** `programs/deploy/`
**Purpose:** Demonstrate real-world language usage
**Size:** 53 programs, shipped in every release's `examples/` directory and indexed in [deploy/README.md](deploy/README.md); `build_examples.py` compiles every one from that index and, with `--run`, runs the ones that need no display, database, key or network and checks each against the `output:` block in its own header
**CI Coverage:** 13 programs compile and run in GitHub Actions, each with the `-lib` line from its own header

### Tests in CI

These 4 programs run on every platform:

1. `hello_0.obs` - Hello World
2. `functions_3.obs` - Functions, named arguments and `ref` parameters
3. `errors_6.obs` - Typed errors
4. `xml_15.obs` - XML parsing

These 9 also run on Linux x64:

5. `values_1.obs` - Types, literals and conversions
6. `closures_8.obs` - Lambdas and closures
7. `strings_10.obs` - String methods, SHA-256 and Base64
8. `collections_11.obs` - Collections, including the unboxed ones
9. `regex_17.obs` - Regular expressions
10. `sql_tables_18.obs` - SQL over in-memory tables
11. `ml_outliers_27.obs` - Anomaly detection
12. `nlp_28.obs` - Text processing
13. `threads_12.obs` - Threading (one run with a 120 s timeout, no retry; it must print `shared total: 5000050000`, and a non-gating step measures its flake rate over 50 runs)

The rest need a display, a data source, an API key or the network (`game_31`, `odbc_21`, `llm_29`, `http_client_22`, `rss_reader_24`), so CI compiles them through `build_examples.py` rather than running them. Every other example is run by `build_examples.py --run` at `-opt s0` and `s3`, and must print what its header says. On Linux x64 the compile step also builds four network programs from `programs/tests/` (`prgm_http2`, `prgm_http3`, `http3_downgrade_test`, `http_header_echo_test`); they are run only by the non-gating network step.

---

## 4. GitHub Actions CI/CD

**Workflow:** `.github/workflows/ci-build.yml`
**Platforms:** Windows x64 and ARM64, Linux x64 and ARM64, macOS ARM64 (native runners)
**Trigger:** Push to master, Pull requests

### CI Pipeline

```
1. Setup Environment
   ├── Install dependencies (mbedtls, opencv, sdl2, etc.)
   └── Update packages

2. Build Toolchain
   ├── Update version numbers
   ├── Build compiler (obc)
   ├── Build VM (obr)
   └── Build debugger

3. Run Tests
   ├── Deploy suite (4 programs; 13 on Linux x64)
   ├── Regression suite (twice: as is, then OBJECK_JIT_THRESHOLD=1)
   ├── Debugger and DAP tests, VM flag tests, compiler robustness (mutation and stress)
   ├── obi/obr parity, library link and generics round trips, updater tests, unit tests
   └── Network tests (non-gating; Linux x64 and Windows x64)

   Beside the matrix: a `tools` job (consistency checks, formatter, LSP, VS Code
   extension) and an advisory `loader-fuzz` job (5-minute libFuzzer run on the .obe loader).
   Nightly, not per push: nightly-hardening.yml.

4. Generate Documentation
   └── Build API docs (Linux x64)
```

### Dependencies Installed

On the Linux legs:

```bash
sudo apt-get install \
  build-essential \
  libopencv-dev \
  libmbedtls-dev \
  unixodbc-dev \
  libsqliteodbc \
  libsdl2-dev \
  libsdl2-image-dev \
  libsdl2-ttf-dev \
  libsdl2-mixer-dev \
  libegl1 \
  libegl-mesa0 \
  libgl1-mesa-dri \
  libreadline-dev \
  libeigen3-dev \
  libmp3lame-dev \
  ccache \
  expect
```

---

## 5. Testing by Platform

### Linux (x64/ARM64)

**Dependencies:**
```bash
sudo apt-get install \
  build-essential \
  libmbedtls-dev \
  unixodbc-dev \
  libsdl2-dev \
  libsdl2-image-dev \
  libsdl2-ttf-dev \
  libsdl2-mixer-dev \
  libreadline-dev \
  libeigen3-dev \
  libmp3lame-dev
```

**Build:**
```bash
cd core/release
./deploy_posix.sh x64    # or arm64
```

**Test:**
```bash
cd programs/regression
./run_regression.sh x64
```

### macOS (ARM64)

**Dependencies:**
```bash
brew install cmake
MACOSX_DEPLOYMENT_TARGET=13.3 bash tools/deps/build_quic_deps.sh
bash tools/deps/build_macos_deps.sh
```

**Build:**
```bash
cd core/release
./deploy_macos_arm64.sh
```

**Test:**
```bash
cd programs/regression
./run_regression.sh arm64
```

### Windows (x64/ARM64)

**Build:**
```cmd
cd core\release
deploy_windows.cmd x64      REM or arm64
```

**Test:**
```cmd
cd programs\regression
run_regression.cmd x64      REM or arm64
```

---

## 6. Test Coverage Summary

### Overall Coverage

| Feature Area | Regression | Comprehensive | Deploy |
|--------------|------------|---------------|--------|
| **Arithmetic** | ✅ | ✅ | ✅ |
| **Arrays** | ✅ | ✅ | ✅ |
| **Strings** | ✅ | ✅ | ✅ |
| **Classes** | ✅ | ✅ | ✅ |
| **Control Flow** | ✅ | ✅ | ✅ |
| **Recursion** | ✅ | ✅ | ⏳ |
| **Collections** | ✅ | ✅ | ✅ |
| **Lambdas** | ✅ | ✅ | ✅ |
| **Threads** | ✅ | ✅ | ✅ |
| **File I/O** | ✅ | ✅ | ✅ |
| **Networking** | ✅ | ✅ | ✅ |
| **JSON** | ✅ | ✅ | ✅ |
| **XML** | ✅ | ✅ | ✅ |
| **Crypto** | ⏳ | ✅ | ✅ |
| **Database** | ✅ | ✅ | ⏳ |
| **Graphics** | ✅ | ✅ | ⏳ |
| **ML/ONNX** | ✅ | ✅ | ⏳ |

In the regression suite, crypto is covered only through hashing and Base64 (for example the SHA-256 and MD5 known-answer checks in `byte_array_header_test.obs`); nothing exercises encryption. The ODBC tests skip unless the `objeck_sqlite_test` data source exists, which CI sets up on Linux.

### ARM64 JIT Coverage

| Feature | Regression | Status |
|---------|------------|--------|
| Char arrays (STRH/LDRH) | ✅ | Validated |
| Large immediates (> 4095) | ✅ | Validated |
| Bitwise operations | ✅ | Validated |
| Multiply-by-constant | ✅ | Validated |
| 64-bit immediates (Windows LLP64) | ✅ | Validated |
| Zero comparisons (CBZ/CBNZ) | ⏳ | Implicit |
| FP register allocation | ⏳ | Implicit |
| Multi-dim arrays | ✅ | Validated |

These are the `arm64_*` tests. Beyond them, the whole suite runs natively on the three ARM64 legs (Linux, macOS, Windows) in both JIT modes.

---

## 7. Adding New Tests

### To Regression Suite

1. Create test file in `programs/regression/`
2. Name format: `category_feature.obs`
3. Include PASS/FAIL output, and exit non-zero on failure (`Runtime->Exit(1)`): the runners go by the exit code, so a test that only prints FAIL is scored a pass
4. Test locally
5. Leave TESTS.md alone: a workflow regenerates it on master after the merge

The directives a test can carry (`# EXTRA_LIBS:`, `# EXPECT_COMPILE_ERROR:`, `# JIT_DISABLE` and others) are listed in [regression/README.md](regression/README.md).

Example template:
```objeck
#~
# Category Feature Test
# Description
~#

class MyTest {
    function : Main(args : String[]) ~ Nil {
        "Testing my feature..."->PrintLine();

        pass := true;

        # Your tests here

        if(pass) {
            "PASS: My feature"->PrintLine();
        } else {
            "FAIL: My feature"->PrintLine();
            Runtime->Exit(1);
        };
    }
}
```

### To Comprehensive Suite

1. Create `prgmNNN.obs` in `programs/tests/`
2. Find next available number
3. Test compilation and execution

---

## 8. Continuous Integration

### Current CI Coverage

- ✅ Deploy suite: 13 programs compiled and run (4 on every platform, 9 more on Linux x64)
- ✅ Regression suite: every test, on all five platforms, twice (as is and with `OBJECK_JIT_THRESHOLD=1`), plus the debugger, DAP, VM flag and compiler robustness tests
- ⏳ Comprehensive suite: not in CI, apart from 4 network tests that run non-gating on Linux x64 and Windows x64

ARM64 runs on native runners: `ubuntu-24.04-arm`, `macos-15` and `windows-11-arm`. Relative performance is tracked separately by `.github/workflows/perf-gate.yml` (report-only; see [perf-results/PERF_GATE.md](../perf-results/PERF_GATE.md)). The long checks (differential regression, program fuzzer, heap-verifier and forced-GC runs, GC stress loops, a 30-minute loader fuzz) run nightly from `.github/workflows/nightly-hardening.yml`; see [docs/TESTING_AND_ASSURANCE.md](../docs/TESTING_AND_ASSURANCE.md).

### Future CI Enhancements

1. **Cross-platform validation** - Compare x64 vs ARM64 outputs
2. **Performance tracking** - Benchmark suite over time (the perf gate is report-only; no history is kept)
3. **Code coverage** - Measure compiler/VM code coverage (none is measured today: no line or branch coverage figure exists for the compiler or the VM)
4. **Nightly full suite** - Run the `programs/tests/` programs overnight (the nightly runs the regression suite and the fuzzers, not those programs)

---

## 9. Test Maintenance

### Regular Tasks

- **Weekly:** Run full comprehensive suite locally
- **Before release:** Run all tests on all platforms
- **After major changes:** Run regression suite
- **New features:** Add regression tests

### Test Quality Guidelines

1. **Fast:** Regression tests should complete in < 2 minutes
2. **Focused:** One test per feature/bug fix
3. **Deterministic:** Same input = same output
4. **Isolated:** No dependencies between tests
5. **Clear:** Obvious PASS/FAIL output

---

## 10. Resources

### Documentation

- [Regression Suite README](regression/README.md)
- [Regression Test Manifest](regression/TESTS.md)
- [Main Project README](../README.md)

### Contact

- Issues: https://github.com/objeck/objeck-lang/issues
- Website: https://www.objeck.org

---

## Summary

**Quick Reference:**

```bash
# Regression suite (~3 min)
cd programs/regression && ./run_regression.sh x64

# Comprehensive suite: no runner; compile and run programs/tests/prgmN.obs one at a time

# CI test suite (deploy + regression)
# Automatically runs on every push/PR
```

**Test Count:**
- Regression: 812 test files ✅
- Comprehensive: 327 numbered programs, not run by CI ⏳
- Deploy: 32 programs ✅
- **Total: about 1,170 programs; only the regression and deploy suites are run and judged automatically**

**Coverage:**
- Core language: Complete
- ARM64 JIT: Whole regression suite on three native ARM64 platforms
- Libraries: Extensive
- Real-world usage: Demonstrated
