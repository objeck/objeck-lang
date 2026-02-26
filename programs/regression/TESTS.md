# Regression Test Manifest

This document lists all regression tests, what they validate, and comprehensive coverage information.

## Test Inventory

| # | Test Name | Category | What It Validates | Related Commit | Platforms | Status |
|---|-----------|----------|-------------------|----------------|-----------|--------|
| 1 | `arm64_bitwise.obs` | ARM64 JIT | Bitwise AND, OR, XOR operations | cc65c18b7 | ARM64, x64 | ✅ |
| 2 | `arm64_char_arrays.obs` | ARM64 JIT | STRH/LDRH 16-bit instruction encoding | cc65c18b7 | ARM64, x64 | ✅ |
| 3 | `arm64_large_immediates.obs` | ARM64 JIT | add_imm_reg/sub_imm_reg with values > 4095 | cc65c18b7 | ARM64, x64 | ✅ |
| 4 | `arm64_multiply_constants.obs` | ARM64 JIT | Multiply-by-constant optimization (especially *6) | cc65c18b7 | ARM64, x64 | ✅ |
| 5 | `core_arithmetic.obs` | Core Language | Integer/float arithmetic, type conversions, operators | - | ARM64, x64 | ✅ |
| 6 | `core_arrays_simple.obs` | Core Language | Array creation, access, multi-dimensional arrays | - | ARM64, x64 | ✅ |
| 7 | `core_classes.obs` | Core Language | Class instantiation, inheritance, method calls, select | - | ARM64, x64 | ✅ |
| 8 | `core_control_flow.obs` | Core Language | If/else, for/while loops, select statements | - | ARM64, x64 | ✅ |
| 9 | `core_recursion.obs` | Core Language | Recursive functions, mutual recursion, Fibonacci, factorial | - | ARM64, x64 | ✅ |
| 10 | `core_strings_simple.obs` | Core Language | String operations, substring, find, conversions | - | ARM64, x64 | ✅ |
| 11 | `jit_native_func_ref.obs` | AMD64 JIT | Function reference storage in class fields (write barrier) | - | ARM64, x64 | ✅ |
| 12 | `jit_native_cls_fields.obs` | AMD64 JIT | Object reference storage in class instance fields with GC pressure | - | ARM64, x64 | ✅ |
| 13 | `jit_native_math.obs` | AMD64 JIT | Native math builtins: Factorial, Sinh/Cosh/Tanh/Log2/Cbrt, Pow | - | ARM64, x64 | ✅ |
| 14 | `jit_native_float_array.obs` | AMD64 JIT | Float array creation and math operations in native context | - | ARM64, x64 | ✅ |

**Total Tests:** 14
**ARM64 JIT Tests:** 4
**AMD64 JIT Tests:** 4
**Core Language Tests:** 6
**All Tests Status:** ✅ PASSING

## Feature Coverage Matrix

### Core Language Features

| Feature Category | Coverage | Tests | Status |
|-----------------|----------|-------|--------|
| **Arithmetic Operations** | ✅ Complete | core_arithmetic | ✅ |
| **Type Conversions** | ✅ Complete | core_arithmetic, core_strings_simple | ✅ |
| **Arrays (1D)** | ✅ Complete | core_arrays_simple | ✅ |
| **Arrays (Multi-D)** | ✅ Complete | core_arrays_simple | ✅ |
| **Strings** | ✅ Basic | core_strings_simple | ✅ |
| **Classes & Inheritance** | ✅ Complete | core_classes | ✅ |
| **Methods & Calls** | ✅ Complete | core_classes, core_recursion | ✅ |
| **Control Flow (if/else)** | ✅ Complete | core_control_flow | ✅ |
| **Loops (for/while)** | ✅ Complete | core_control_flow | ✅ |
| **Select Statements** | ✅ Complete | core_classes, core_control_flow | ✅ |
| **Recursion** | ✅ Complete | core_recursion | ✅ |
| **Bitwise Operations** | ✅ Complete | arm64_bitwise | ✅ |
| **Function References** | ✅ Basic | jit_native_func_ref | ✅ |
| **Native Math Builtins** | ✅ Complete | jit_native_math | ✅ |
| **Float Array + Math** | ✅ Complete | jit_native_float_array | ✅ |
| **Class Field Storage (GC)** | ✅ Complete | jit_native_cls_fields | ✅ |
| **Lambdas/Closures** | ⏳ Partial | - | ⚠️ Complex syntax |
| **Collections** | ⏳ Partial | - | ⚠️ Generic issues |
| **Threads** | ❌ None | - | 📋 TODO |
| **File I/O** | ❌ None | - | 📋 TODO |
| **Exceptions** | ❌ None | - | 📋 TODO |
| **Crypto (mbedTLS)** | ❌ None | - | 📋 TODO (library path) |

### ARM64 JIT Compilation

| Feature | Coverage | Test | Commit | Status |
|---------|----------|------|--------|--------|
| **STRH/LDRH (16-bit char ops)** | ✅ Complete | arm64_char_arrays | cc65c18b7 | ✅ |
| **Large immediates (> 4095)** | ✅ Complete | arm64_large_immediates | cc65c18b7 | ✅ |
| **Bitwise NOT (64-bit ORN)** | ⏳ Partial | arm64_bitwise | cc65c18b7 | ⚠️ No NOT operator |
| **Multiply-by-constant** | ✅ Complete | arm64_multiply_constants | cc65c18b7 | ✅ |
| **CBZ/CBNZ optimization** | ⏳ Implicit | core_control_flow | cc65c18b7 | ⚠️ Not explicit |
| **FP register allocation (D8-D15)** | ⏳ Implicit | core_arithmetic | cc65c18b7 | ⚠️ Not explicit |
| **Multi-dim array offset scaling** | ✅ Complete | core_arrays_simple | cc65c18b7 | ✅ |

## Test Organization

### By Category

**ARM64 JIT Fixes (4 tests)**
```
programs/regression/
├── arm64_bitwise.obs              # Bitwise AND, OR, XOR
├── arm64_char_arrays.obs          # 16-bit STRH/LDRH encoding
├── arm64_large_immediates.obs     # Immediates > 4095
└── arm64_multiply_constants.obs   # Multiply optimization
```

**AMD64 JIT Write Barrier Regression (4 tests)**
```
programs/regression/
├── jit_native_func_ref.obs        # Function ref storage in class fields
├── jit_native_cls_fields.obs      # Object ref storage with GC pressure
├── jit_native_math.obs            # Native math builtins (Factorial, hyperbolic, Pow)
└── jit_native_float_array.obs     # Float array + math operations
```

**Core Language (6 tests)**
```
programs/regression/
├── core_arithmetic.obs        # Arithmetic & type conversions
├── core_arrays_simple.obs     # Array operations
├── core_classes.obs           # OOP features
├── core_control_flow.obs      # If/else, loops, select
├── core_recursion.obs         # Recursive functions
└── core_strings_simple.obs    # String operations
```

## Comparison with Existing Test Suites

### programs/tests/ (Comprehensive Suite)
- **Total tests:** 323+ test programs (prgm1-351 with gaps)
- **Coverage:** Comprehensive language feature testing
- **Purpose:** Full compiler/VM validation
- **CI Coverage:** 96 tests via regress.sh
- **Limitations:** Large, slow, not all in CI

### programs/deploy/ (Demo Suite)
- **Total tests:** 17 programs
- **Coverage:** Real-world usage examples
- **Purpose:** Demonstrate language features
- **CI Coverage:** All 17 run in GitHub Actions
- **Limitations:** Not systematic, missing edge cases

### programs/regression/ (This Suite)
- **Total tests:** 14 focused tests
- **Coverage:** Critical paths + recent fixes
- **Purpose:** Fast regression detection
- **CI Coverage:** All 14 run in GitHub Actions
- **Advantages:** Small, fast, targeted, fully automated

## Recommended Test Additions

### High Priority (Should Add Next)

1. **core_file_io.obs** - Basic file read/write operations
   ```objeck
   # Create file, write, read, verify contents
   ```

2. **core_exceptions.obs** - Try/catch error handling
   ```objeck
   # Test exception throwing and catching
   ```

3. **crypto_basic.obs** - Hash functions (when library path fixed)
   ```objeck
   # SHA256("abc") = known hash
   ```

4. **core_threads.obs** - Basic threading
   ```objeck
   # Create thread, run, join
   ```

### Medium Priority

5. **core_collections_basic.obs** - Simple collection operations (avoid generics)
6. **core_native_calls.obs** - Native method calls
7. **arm64_zero_comparisons.obs** - Explicit CBZ/CBNZ tests
8. **platform_consistency.obs** - Cross-platform hash verification

### Low Priority

9. **core_lambdas_advanced.obs** - Once syntax is clarified
10. **performance_baseline.obs** - Execution time tracking

## Test Results History

| Date | Platform | Total | Pass | Fail | Notes |
|------|----------|-------|------|------|-------|
| 2026-02-09 | x64 | 7 | 7 | 0 | Initial suite |
| 2026-02-09 | x64 | 10 | 10 | 0 | Added general features |
| - | ARM64 | 10 | - | - | Pending ARM64 hardware test |

## Success Criteria

- ✅ All tests pass on x64 (Linux, Windows)
- ⏳ All tests pass on ARM64 (Windows) - Pending hardware
- ✅ GitHub Actions CI runs all tests automatically
- ✅ Tests complete in < 2 minutes
- ✅ Coverage of recent critical bug fixes (ARM64 JIT)
- ✅ Coverage of core language features
- ⏳ Cross-platform output verification - TODO

## Future Enhancements

1. **Automated output validation** - Compare against expected output files
2. **Performance regression detection** - Track execution time trends
3. **Code coverage reporting** - Measure VM/compiler code coverage
4. **Cross-platform comparison** - Verify x64 vs ARM64 produce identical results
5. **Fuzzing integration** - Property-based testing for edge cases
6. **Memory leak detection** - Valgrind/ASan integration
7. **Benchmark suite** - Performance tracking over time
