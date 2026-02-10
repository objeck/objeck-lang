# Objeck Language Testing Infrastructure

Complete guide to testing the Objeck language compiler, virtual machine, and libraries.

## Overview

The Objeck project has **3 primary test suites** with different purposes:

| Suite | Location | Tests | Purpose | CI Coverage |
|-------|----------|-------|---------|-------------|
| **Regression Suite** | `programs/regression/` | 10 | Fast, focused regression detection | ✅ All |
| **Comprehensive Suite** | `programs/tests/` | 323+ | Full language feature validation | ✅ 96 tests |
| **Demo Suite** | `programs/deploy/` | 17 | Real-world usage examples | ✅ All |

---

## 1. Regression Test Suite

**Location:** `programs/regression/`
**Purpose:** Fast, automated regression detection for critical functionality
**Size:** 10 tests (< 30 target)
**Execution Time:** ~1-2 minutes

### Quick Start

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

- **ARM64 JIT fixes** (4 tests): Recent bug fixes in ARM64 code generation
- **Core language** (6 tests): Fundamental language features

### CI Integration

Runs automatically on every:
- Push to master
- Pull request to master

See `.github/workflows/c-cpp.yml` for configuration.

### Documentation

- [README.md](regression/README.md) - Usage and organization
- [TESTS.md](regression/TESTS.md) - Detailed test manifest and coverage matrix

---

## 2. Comprehensive Test Suite

**Location:** `programs/tests/`
**Purpose:** Exhaustive validation of compiler and VM correctness
**Size:** 323+ test programs (prgm0-351 with gaps)
**Execution Time:** ~30-60 minutes for full suite

### Structure

```
programs/tests/
├── prgm1.obs through prgm351.obs    # Main test programs
├── aoc_2022/                        # Advent of Code 2022 solutions
├── aow/                             # Advent of Code solutions
├── clbg/                            # Computer Language Benchmarks Game
├── data/                            # Test data files
│   ├── JSON test files
│   ├── ONNX models
│   └── Images for ML tests
└── test_mbedtls_migration.obs       # Crypto library tests
```

### Running Tests

**Run specific range:**
```bash
cd core/compiler
./regress.sh  # Runs prgm1-96
```

**Run individual test:**
```bash
cd core/compiler
./obc -src test_src/prgm10.obs -lib xml.obl,collect.obl -opt s3 -dest a.obe
cd ../vm
./obr ../compiler/a.obe
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
| **Machine Learning** | prgm352 | ONNX model inference |
| **Benchmarks** | clbg/* | Performance tests |

### Special Tests

**test_mbedtls_migration.obs** - Comprehensive crypto test
```bash
cd programs/tests
obc -src test_mbedtls_migration.obs -lib cipher -dest test_crypto.obe
obr test_crypto.obe
```

Expected output: 40+ test cases passing, 0 failures

### Framework-Specific Tests

```
programs/frameworks/
├── json/tests/           # JSON parsing tests
├── sdl/engine/tests/     # SDL graphics tests (10+ programs)
└── opencv_onnx/tests/    # OpenCV and ONNX ML tests
```

---

## 3. Demo/Deploy Test Suite

**Location:** `programs/deploy/`
**Purpose:** Demonstrate real-world language usage
**Size:** 17 programs
**CI Coverage:** All programs run in GitHub Actions

### Tests in CI

The following 17 programs run on every CI build:

1. `calc_life_10.obs` - Game of Life calculation
2. `closure_19.obs` - Closure demonstrations
3. `encrypt_7.obs` - Encryption examples
4. `first_class_18.obs` - First-class functions
5. `fs_query_12.obs` - File system queries
6. `functions_5.obs` - Function examples
7. `hello_0.obs` - Hello World
8. `http_xml_regex_8.obs` - HTTP, XML, Regex
9. `json_3.obs` - JSON processing
10. `lambda_17.obs` - Lambda expressions
11. `regex_4.obs` - Regular expressions
12. `rss_https_xml_15.obs` - RSS feed parsing
13. `serial_14.obs` - Serialization
14. `threads_6.obs` - Threading
15. `visitor_20.obs` - Visitor pattern
16. `loops_19.obs` - Loop constructs
17. `xml_2.obs` - XML parsing

---

## 4. GitHub Actions CI/CD

**Workflow:** `.github/workflows/c-cpp.yml`
**Platform:** Ubuntu (x64)
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
   ├── Deploy suite (17 tests)
   └── Regression suite (10 tests)  ← NEW!

4. Generate Documentation
   └── Build API docs
```

### Dependencies Installed

```bash
sudo apt-get install \
  build-essential \
  libopencv-dev \
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
./deploy_posix.sh x64    # or 'rpi' for ARM64
```

**Test:**
```bash
cd programs/regression
./run_regression.sh x64
```

### macOS (ARM64)

**Dependencies:**
```bash
brew install lame opencv onnxruntime
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
| **Collections** | ⏳ | ✅ | ✅ |
| **Lambdas** | ❌ | ✅ | ✅ |
| **Threads** | ❌ | ✅ | ✅ |
| **File I/O** | ❌ | ✅ | ✅ |
| **Networking** | ❌ | ✅ | ✅ |
| **JSON** | ❌ | ✅ | ✅ |
| **XML** | ❌ | ✅ | ✅ |
| **Crypto** | ❌ | ✅ | ✅ |
| **Database** | ❌ | ✅ | ⏳ |
| **Graphics** | ❌ | ✅ | ⏳ |
| **ML/ONNX** | ❌ | ✅ | ⏳ |

### ARM64 JIT Coverage

| Feature | Regression | Status |
|---------|------------|--------|
| Char arrays (STRH/LDRH) | ✅ | Validated |
| Large immediates (> 4095) | ✅ | Validated |
| Bitwise operations | ✅ | Validated |
| Multiply-by-constant | ✅ | Validated |
| Zero comparisons (CBZ/CBNZ) | ⏳ | Implicit |
| FP register allocation | ⏳ | Implicit |
| Multi-dim arrays | ✅ | Validated |

---

## 7. Adding New Tests

### To Regression Suite

1. Create test file in `programs/regression/`
2. Name format: `category_feature.obs`
3. Include PASS/FAIL output
4. Test locally
5. Update TESTS.md

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
        };
    }
}
```

### To Comprehensive Suite

1. Create `prgmNNN.obs` in `programs/tests/`
2. Find next available number
3. Test compilation and execution
4. Update `regress.sh` if needed

---

## 8. Continuous Integration

### Current CI Coverage

- ✅ Deploy suite: 17 tests
- ✅ Regression suite: 10 tests
- ⏳ Comprehensive suite: 96 tests (via regress.sh, not in CI)

### Future CI Enhancements

1. **ARM64 CI runners** - When GitHub Actions supports ARM64
2. **Cross-platform validation** - Compare x64 vs ARM64 outputs
3. **Performance tracking** - Benchmark suite over time
4. **Code coverage** - Measure compiler/VM code coverage
5. **Nightly full suite** - Run all 323+ tests overnight

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
- [Main Project README](../readme.md)
- [ARM64 Build Fix](../core/lib/crypto/WINDOWS_ARM64_BUILD_FIX.md)

### Contact

- Issues: https://github.com/objeck/objeck-lang/issues
- Website: https://www.objeck.org

---

## Summary

**Quick Reference:**

```bash
# Fast regression check (1-2 min)
cd programs/regression && ./run_regression.sh x64

# Comprehensive test (30-60 min)
cd core/compiler && ./regress.sh

# CI test suite (deploy + regression)
# Automatically runs on every push/PR
```

**Test Count:**
- Regression: 10 tests ✅
- Comprehensive: 323+ tests ✅
- Deploy: 17 tests ✅
- **Total: 350+ tests**

**Coverage:**
- Core language: Complete
- ARM64 JIT: Recent fixes validated
- Libraries: Extensive
- Real-world usage: Demonstrated
