# Testing JIT Optimizations - Phase 1

## Changes Implemented

### Branch: `jit-optimization-jan-2026`
**Commit:** c7d380e91

### Optimizations Applied

#### 1. x64 JIT Compiler (`core/vm/arch/jit/amd64/jit_amd_lp64.cpp`)
**Function:** `move_imm_reg()` at line 2628

**Optimization:** Use shorter instruction encoding for 32-bit signed immediate values.

**Before:**
```asm
movabs $imm64, %reg    ; 10 bytes (REX.W + B8+r + imm64)
```

**After (for values in INT32 range):**
```asm
mov $imm32, %reg       ; 7 bytes (REX.W + C7 /0 + imm32, sign-extended)
```

**Benefit:** Saves 3 bytes per instruction when immediate fits in signed 32-bit range.

#### 2. ARM64 JIT Compiler (`core/vm/arch/jit/arm64/jit_arm_a64.cpp`)
**Function:** `move_imm_reg()` at line 2229

**Optimization:** Synthesize immediate values with MOVZ/MOVK instead of constant pool access.

**Before:**
```asm
ldr x9, [sp, #INT_CONSTS]   ; Load constant pool address
ldr reg, [x9, #0]            ; Load value from pool
; Total: 2 instructions + memory access
```

**After (for values with 1-2 non-zero 16-bit chunks):**
```asm
movz reg, #chunk0, lsl #(pos0*16)   ; First chunk
movk reg, #chunk1, lsl #(pos1*16)   ; Second chunk (if needed)
; Total: 1-2 instructions, no memory access
```

**Benefit:**
- Eliminates memory access (faster, better for cache)
- Reduces instruction count for simple values
- Falls back to constant pool only for complex values (3-4 non-zero chunks)

---

## Testing Strategy

### Phase 1: Compilation Verification

#### Windows x64
```cmd
cd core\release
deploy_windows.cmd x64
```

**Expected:** Clean build with no compilation errors.

**Files to check:**
- `deploy-x64\bin\obr.exe` (VM with JIT)
- `deploy-x64\bin\obc.exe` (Compiler)

#### Windows ARM64
```cmd
cd core\release
deploy_windows.cmd arm64
```

**Expected:** Clean build with no compilation errors.

#### Linux x64
```bash
cd core/release
./deploy_posix.sh x64
```

**Expected:** Clean build, CI should pass when PR is created.

#### macOS ARM64
```bash
cd core/release
./deploy_macos_arm64.sh
```

**Expected:** Clean build with ARM64 optimizations active.

---

### Phase 2: Functional Testing

#### Test Program
Location: `programs/tests/prgm_jit_imm_test.obs`

This test verifies:
- Small immediate values (0-4095)
- Medium immediate values (32-bit range: INT32_MIN to INT32_MAX)
- Large immediate values (64-bit range)
- Arithmetic operations with immediate values

**To run:**
```bash
cd core/release/deploy/bin  # or deploy-x64/bin on Windows
./obc -src ../../../../programs/tests/prgm_jit_imm_test.obs
./obr ../../../../programs/tests/prgm_jit_imm_test.obe
```

**Expected output:**
```
=== JIT Immediate Value Optimization Test ===

Test 1: Small immediate values (0-4095)
  ✓ Small immediates: PASS

Test 2: Medium immediate values (32-bit range)
  ✓ Medium immediates (32-bit): PASS

Test 3: Large immediate values (64-bit)
  ✓ Large immediates (64-bit): PASS

Test 4: Arithmetic operations with immediate values
  ✓ Arithmetic with immediates: PASS

=== All tests passed ===
```

---

### Phase 3: Regression Testing

#### Full Test Suite
```bash
cd core/compiler
./regress.sh
```

**Expected:** All 200+ test programs compile and run successfully with correct output.

**Critical programs to verify manually if issues arise:**
- `programs/tests/prgm1.obs` through `prgm200.obs`
- Focus on programs with heavy integer arithmetic
- Focus on programs with array indexing

#### Deployment Examples
```bash
cd core/release/deploy/bin  # or deploy-x64/bin
./obc -src ../../../../programs/deploy/hello_0.obs
./obr ../../../../programs/deploy/hello_0.obe

./obc -src ../../../../programs/deploy/calc_life_10.obs
./obr ../../../../programs/deploy/calc_life_10.obe
```

**Expected:** All deployment examples run without errors.

---

### Phase 4: Performance Benchmarking

#### CLBG Benchmarks
Location: `programs/tests/clbg/`

**Baseline Measurement (before optimization):**
```bash
cd programs/tests/clbg/

# For each benchmark, compile with -opt s3 and measure time
time obr fannkuchredux.obe 10
time obr fasta.obe 1000
time obr mandelbrot.obe 4000
time obr nbody.obe 1000000
time obr spectralnorm.obe 500
time obr binarytrees.obe 15
```

**After Optimization:**
Run the same commands and compare execution times.

**Expected Improvements:**
- **fannkuchredux:** 5-10% faster (many small integer constants)
- **fasta:** 5-8% faster (string manipulation with constant indices)
- **mandelbrot:** 8-12% faster (2D array indexing, integer math)
- **nbody:** 3-5% faster (floating-point heavy, fewer immediate values)
- **spectralnorm:** 5-8% faster (matrix operations)
- **binarytrees:** 3-5% faster (object allocation heavy)

**Most Impacted:** Programs with heavy use of immediate values in arithmetic operations and array indexing.

---

### Phase 5: Platform-Specific Verification

#### Windows x64
- [ ] Compile succeeds
- [ ] Test program runs
- [ ] Regression tests pass
- [ ] Benchmarks show improvement

#### Windows ARM64
- [ ] Compile succeeds
- [ ] Test program runs
- [ ] Verify MOVZ/MOVK synthesis is used (check with debugger)
- [ ] Constant pool fallback works for complex values

#### Linux x64
- [ ] CI/CD pipeline passes
- [ ] All tests in c-cpp.yml workflow succeed
- [ ] Manual verification if needed

#### macOS ARM64
- [ ] Compile succeeds with Xcode
- [ ] Test program runs
- [ ] Verify ARM64 optimizations work on actual Apple Silicon
- [ ] Performance matches or exceeds expectations

---

## Debugging Tips

### Enable JIT Debug Output

**x64:** Uncomment `#define _DEBUG_JIT` in `core/vm/arch/jit/amd64/jit_amd_lp64.cpp` line ~20
**ARM64:** Uncomment `#define _DEBUG_JIT_JIT` in `core/vm/arch/jit/arm64/jit_arm_a64.cpp` line ~20

This will output generated assembly instructions to help verify optimizations are applied.

### Verify Instruction Encoding

**x64:**
- Look for `mov $imm, %reg` with 7-byte encoding for 32-bit immediates
- Look for `movabs $imm, %reg` with 10-byte encoding for 64-bit immediates

**ARM64:**
- Look for `movz` and `movk` sequences for synthesized immediates
- Look for constant pool loads (`ldr`) only for complex values

### Common Issues

1. **Compilation errors about INT32_MIN/INT32_MAX:**
   - Ensure `<climits>` or `<limits.h>` is included
   - These constants are standard C/C++ and should be available

2. **Segmentation faults or crashes:**
   - Check instruction encoding is correct
   - Verify sign extension works properly
   - Check ModRM byte calculation in x64

3. **Incorrect results:**
   - Verify 32-bit values are properly sign-extended to 64-bit on x64
   - Verify ARM64 chunk extraction and synthesis is correct

4. **Performance regression:**
   - Should NOT happen with these optimizations
   - If it does, check if optimization is being applied correctly
   - May indicate compiler is already optimizing this case

---

## Success Criteria

✅ **Must Pass:**
1. All platforms compile without errors
2. Test program (`prgm_jit_imm_test.obs`) passes all checks
3. Full regression suite passes (200+ programs)
4. No segmentation faults or crashes in any test

✅ **Should Achieve:**
1. 5-10% performance improvement on integer-heavy benchmarks
2. Reduced code size (measured in bytes of generated machine code)
3. CI/CD pipeline passes on Linux

✅ **Nice to Have:**
1. 10%+ improvement on specific benchmarks (fannkuchredux, mandelbrot)
2. Measurable reduction in JIT compilation time (though minimal expected)

---

## Next Steps After Phase 1

Once Phase 1 testing is complete and successful:

1. **Phase 2:** Implement peephole optimization for common patterns
2. **Phase 3:** Strength reduction for array indexing
3. **Phase 4:** Architecture-specific optimizations (LEA for x64, extended FP registers for ARM64)

See `.claude/plans/modular-gathering-goose.md` for full optimization roadmap.

---

## Rollback Plan

If testing reveals critical issues:

```bash
git revert c7d380e91
git push origin jit-optimization-jan-2026
```

Or abandon branch and return to master:
```bash
git checkout master
git branch -D jit-optimization-jan-2026
```

---

## Contact and Issues

If you encounter issues during testing:
1. Check debug output with _DEBUG_JIT enabled
2. Review instruction encoding carefully
3. Compare generated code before/after optimization
4. Document the issue with test case that reproduces it

**Testing Status:** Ready for compilation and functional testing on all platforms.
