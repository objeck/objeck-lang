# Regression Test Manifest

This document lists all regression tests, what they validate, and which platforms they cover.

| Test Name | Category | What It Validates | Related Commit | Platforms |
|-----------|----------|-------------------|----------------|-----------|
| `crypto_comprehensive.obs` | Crypto | SHA1/256/512, MD5, RIPEMD160, AES-256, Base64 | 58ce4be37 | x64, ARM64 |
| `arm64_char_arrays.obs` | ARM64 JIT | STRH/LDRH 16-bit instruction encoding | cc65c18b7 | ARM64 |
| `arm64_large_immediates.obs` | ARM64 JIT | add_imm_reg/sub_imm_reg with values > 4095 | cc65c18b7 | ARM64 |
| `arm64_bitwise_not.obs` | ARM64 JIT | Bitwise NOT using 64-bit ORN instruction | cc65c18b7 | ARM64 |
| `arm64_multiply_constants.obs` | ARM64 JIT | Multiply-by-constant optimization (especially *6) | cc65c18b7 | ARM64 |
| `core_arithmetic.obs` | Core Language | Integer/float arithmetic, type conversions, operators | - | x64, ARM64 |
| `core_classes.obs` | Core Language | Class instantiation, inheritance, method calls, select | - | x64, ARM64 |
| `core_collections.obs` | Core Language | Vector, Map, generics, lambdas, iteration | - | x64, ARM64 |

## Test Coverage Summary

- **Total Tests**: 8
- **Crypto Tests**: 1 (comprehensive)
- **ARM64 JIT Tests**: 4
- **Core Language Tests**: 3
- **Cross-Platform Tests**: All tests run on both x64 and ARM64

## Critical Path Coverage

### Crypto Operations
- ✅ Hash functions (SHA1, SHA256, SHA512, MD5, RIPEMD160)
- ✅ AES-256 encryption/decryption
- ✅ Base64 encoding/decoding
- ✅ Nil input handling
- ✅ OpenSSL backward compatibility

### ARM64 JIT Compilation
- ✅ Character array operations (16-bit STRH/LDRH)
- ✅ Large immediate values (> 4095)
- ✅ Bitwise NOT (64-bit ORN)
- ✅ Multiply-by-constant (shift-add optimization)
- ⏳ Zero comparisons (CBZ/CBNZ) - TODO
- ⏳ FP register operations (D8-D15) - TODO
- ⏳ Multi-dimensional arrays - TODO

### Core Language Features
- ✅ Arithmetic operations
- ✅ Type conversions
- ✅ Classes and inheritance
- ✅ Method calls and return values
- ✅ Collections (Vector, Map)
- ✅ Generics
- ✅ Lambda expressions
- ⏳ Threading - TODO
- ⏳ File I/O - TODO
- ⏳ Native method calls - TODO

## Future Test Additions

High priority tests to add:

1. **arm64_zero_comparisons.obs** - CBZ/CBNZ branch optimization
2. **arm64_float_ops.obs** - FP register allocation (D8-D15)
3. **arm64_arrays_multidim.obs** - Multi-dimensional array access
4. **core_control_flow.obs** - If/else, loops, recursion
5. **core_strings.obs** - String operations, concatenation, parsing
6. **platform_hash_consistency.obs** - Verify identical hashes across platforms

## Test Results History

| Date | Platform | Pass | Fail | Notes |
|------|----------|------|------|-------|
| 2026-02-09 | x64 | - | - | Initial suite created |
| 2026-02-09 | ARM64 | - | - | Initial suite created |
