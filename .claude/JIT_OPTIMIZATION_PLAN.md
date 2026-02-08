# JIT Compiler Advanced Optimizations Implementation Plan

## Context

The Objeck JIT compilers (x64 and ARM64) have already received significant optimizations including power-of-2 strength reduction, immediate value optimizations, and array indexing improvements. A comprehensive analysis of the codebase has identified four additional high-impact, lower-complexity optimizations that can deliver meaningful performance improvements with manageable implementation risk.

**Why these optimizations:**
- Current JIT already handles ~60% of common patterns
- These optimizations target the remaining high-frequency patterns
- Focus on low-hanging fruit: high impact, lower complexity
- Incremental approach allows testing and validation at each step

**Expected cumulative impact:**
- **Performance:** 5-15% overall speedup
- **Code size:** 5-10% reduction
- **Branch efficiency:** 10-15% improvement
- **Register pressure:** 10% reduction (ARM64)

## Priority Optimizations (Recommended Order)

### 1. ARM64 CBZ/CBNZ - Compare and Branch If Zero (Priority 1)

**Impact:** HIGH | **Complexity:** MEDIUM | **Risk:** MEDIUM

**What:** Replace 2-instruction sequences (CMP + B.eq/B.ne) with single CBZ/CBNZ instructions when comparing against zero.

**Why:** Zero comparisons occur in ~15% of conditional branches (nil checks, loop counters, comparison operations). Single instruction reduces code size by 50% and improves I-cache utilization.

**Files to modify:**
- `core/vm/arch/jit/arm64/jit_arm_a64.h` - Add function declarations
- `core/vm/arch/jit/arm64/jit_arm_a64.cpp:2826-2847` - Track zero comparisons in `cmp_imm_reg()`
- `core/vm/arch/jit/arm64/jit_arm_a64.cpp:3626-3733` - Emit CBZ/CBNZ in `cond_jmp()` for EQL_INT/NEQL_INT

**Implementation approach:**
1. Add member variables: `last_cmp_was_zero`, `last_cmp_reg`
2. Track when `cmp_imm_reg(0, reg)` is called
3. In `cond_jmp()`, check if last comparison was zero
4. Emit CBZ (0xB4000000) or CBNZ (0xB5000000) instead of CMP+B.eq
5. Use existing branch fixup mechanism for offset patching

**Critical considerations:**
- Only works for integer comparisons (not floats)
- Branch offset limitation: ±1MB range (adequate for typical methods)
- Must properly handle both "jump if true" and "jump if false" cases

**Testing strategy:**
- Unit test with various zero comparison patterns (nil checks, loop conditions)
- Verify DEBUG_JIT shows CBZ/CBNZ instead of CMP+B.eq
- Run full regression suite to ensure correctness
- Measure branch instruction reduction in real programs

---

### 2. Multiply-by-Constant Strength Reduction (Priority 2)

**Impact:** HIGH | **Complexity:** LOW | **Risk:** LOW

**What:** Expand existing power-of-2 optimization to handle common non-power-of-2 constants (3, 5, 6, 7, 9, 10) using shift-and-add sequences.

**Why:** Multiply instruction has 3-4 cycle latency vs 1-2 cycles for shift+add. Small constants occur frequently in indexing, scaling, and arithmetic expressions.

**Mathematical patterns:**
```
x * 3  = (x << 1) + x       # x64: LEA, ARM64: ADD with LSL #1
x * 5  = (x << 2) + x       # x64: LEA, ARM64: ADD with LSL #2
x * 7  = (x << 3) - x       # Requires temp register
x * 9  = (x << 3) + x       # x64: LEA, ARM64: ADD with LSL #3
x * 10 = (x << 1) + (x<<3)  # Two operations
```

**Files to modify:**
- `core/vm/arch/jit/amd64/jit_amd_lp64.cpp:3827-3897` - Extend `mul_imm_reg()`
- `core/vm/arch/jit/arm64/jit_arm_a64.cpp:2482-2507` - Extend `mul_imm_reg()`

**Implementation approach:**
- **x64:** Use LEA instruction for 3, 5, 9 (single instruction, no temp register)
  - LEA allows: `lea [reg + reg*{2,4,8}], reg` for multiply by {3,5,9}
  - For 7, 10: Use temp register + shift + add/sub
- **ARM64:** Use ADD with shifted operand: `ADD Xd, Xn, Xm, LSL #shift`
  - Natural support for shift-and-add in single instruction

**Fallback:** If GetRegister() fails (register pressure), use standard MUL

**Testing strategy:**
- Test correctness with positive, negative, and zero values
- Verify assembly shows optimized sequences (LEA on x64, ADD+LSL on ARM64)
- Micro-benchmark multiplication loops
- Check that non-optimized constants (e.g., 11, 13) still use MUL

---

### 3. Modulo-to-AND Optimization (Priority 3)

**Impact:** MEDIUM | **Complexity:** MEDIUM | **Risk:** HIGH (negative numbers)

**What:** Convert `x % 2^n` to `x & (2^n - 1)` for power-of-2 divisors.

**Why:** Modulo by division takes 20-40 cycles, bitwise AND takes 1 cycle. Common in hash tables, ring buffers, and array partitioning.

**Mathematical basis:**
```
x % 8  = x & 7   (for non-negative x)
x % 16 = x & 15
x % 32 = x & 31
```

**CRITICAL SAFETY CONCERN:** Only works correctly for **non-negative** integers. Negative numbers require special handling:
```
For x >= 0:  x % 8 = x & 7  ✓ Correct
For x < 0:   Modulo semantics differ from AND
```

**Files to modify:**
- `core/vm/arch/jit/amd64/jit_amd_lp64.cpp:3929-3976` - Modify `div_imm_reg()`
- `core/vm/arch/jit/arm64/jit_arm_a64.cpp:2536-2541` - Modify `div_imm_reg()`

**Implementation approach:**
1. Detect: `is_mod && imm is power-of-2`
2. Insert sign check: TEST reg, reg (x64) or TBZ bit 63 (ARM64)
3. **Fast path (positive):** AND reg, (imm-1)
4. **Slow path (negative):** Full division (branch to existing code)
5. Use branch fixup to patch offsets

**Alternative simpler approach:** Always include sign check even though most use cases involve non-negative values. This ensures correctness at minimal cost.

**Testing strategy:**
- **CRITICAL:** Extensive testing with negative numbers
- Test edge cases: 0, -1, MIN_INT, MAX_INT
- Verify both positive and negative paths produce correct results
- Test power-of-2 values: 2, 4, 8, 16, 32, 64, 128, 256
- Test non-power-of-2 (should use standard division)

---

### 4. ARM64 Logical Immediate Instructions (Priority 4)

**Impact:** MEDIUM | **Complexity:** HIGH | **Risk:** MEDIUM

**What:** Use ARM64's native ANDI/ORRI instructions with encoded logical immediates instead of loading constants into registers.

**Why:** Current code allocates temp register, loads immediate (1-4 instructions), then performs operation. Native immediate encoding reduces this to 1 instruction.

**Current pattern (3-6 instructions):**
```
GetRegister()           // Allocate temp
move_imm_reg()          // Load immediate (1-4 instructions)
and_reg_reg()           // AND registers
ReleaseRegister()       // Free temp
```

**Optimized pattern (1 instruction):**
```
ANDI Xd, Xn, #imm       // Single instruction (if encodable)
```

**Files to modify:**
- `core/vm/arch/jit/arm64/jit_arm_a64.h` - Add `EncodeLogicalImmediate()` declaration
- `core/vm/arch/jit/arm64/jit_arm_a64.cpp:2723-2728` - Optimize `and_imm_reg()`
- `core/vm/arch/jit/arm64/jit_arm_a64.cpp:2778-2783` - Optimize `or_imm_reg()`

**Implementation approach:**
1. Create lookup table for common encodable values (0xFF, 0xFFFF, 0x7F, powers-of-2 minus 1, etc.)
2. Implement `EncodeLogicalImmediate()` function to check if value is encodable
3. If encodable: Emit single ANDI/ORRI instruction (0x92000000/0xB2000000)
4. If not encodable: Fall back to existing register-based approach

**ARM64 logical immediate encoding:** Not all 64-bit values can be encoded. Encodable patterns:
- Powers of 2 minus 1: 0x1, 0x3, 0x7, 0xF, 0xFF, 0xFFFF, etc.
- Repeating patterns: 0x0101010101010101
- Shifted masks: 0x7F00, 0x3FF0

**Practical approach:** Start with lookup table covering 80% of common cases, optionally implement full encoding algorithm later.

**Testing strategy:**
- Test common masks (byte, word, dword boundaries)
- Verify encodable values use ANDI/ORRI
- Verify non-encodable values fall back to register path
- Check register allocation count (should decrease)

---

## Implementation Sequence

### Week 1: ARM64 CBZ/CBNZ
- Day 1-2: Implementation
- Day 3: Testing and debugging
- Day 4: Performance measurement
- Day 5: Documentation

**Deliverable:** CBZ/CBNZ optimization passing all tests

### Week 2: Multiply & Modulo
- Day 1-2: Multiply-by-constant (both platforms)
- Day 3-4: Modulo-to-AND (both platforms, focus on negative number testing)
- Day 5: Integration testing

**Deliverable:** Arithmetic optimizations operational

### Week 3: ARM64 Logical Immediates
- Day 1-2: Encoding function + lookup table
- Day 3-4: Integration and testing
- Day 5: Benchmarks and documentation

**Deliverable:** Complete optimization suite

### Week 4: Integration & Validation
- Day 1-2: Full regression testing
- Day 3: Performance profiling
- Day 4-5: Documentation and finalization

**Deliverable:** Production-ready optimized JIT compilers

---

## Critical Files

**Must modify:**
1. `core/vm/arch/jit/arm64/jit_arm_a64.cpp` (4,780 lines)
   - Multiple functions across optimizations 1, 3, 4
2. `core/vm/arch/jit/arm64/jit_arm_a64.h`
   - Add new function declarations and member variables
3. `core/vm/arch/jit/amd64/jit_amd_lp64.cpp` (5,667 lines)
   - Optimize multiply and modulo functions
4. `core/vm/arch/jit/amd64/jit_amd_lp64.h`
   - Minimal changes (existing interface sufficient)

**Create for testing:**
- `programs/test/jit_optimizations/cbz_test.obs`
- `programs/test/jit_optimizations/multiply_test.obs`
- `programs/test/jit_optimizations/modulo_test.obs` (FOCUS ON NEGATIVES)
- `programs/test/jit_optimizations/logical_imm_test.obs`
- `programs/test/jit_optimizations/benchmark_harness.obs`

---

## Verification Strategy

### For Each Optimization:

**1. Correctness Testing**
- Unit tests covering typical cases, edge cases, and error conditions
- **Special focus:** Negative numbers for modulo optimization
- Regression testing: All existing tests must pass

**2. Assembly Verification**
- Enable DEBUG_JIT/DEBUG_JIT_JIT flags
- Inspect generated assembly for expected patterns
- Count occurrences of optimized vs unoptimized instructions

**3. Performance Measurement**
- Micro-benchmarks for each optimization
- Real-world program benchmarks
- Compare before/after with statistical significance

**4. Integration Testing**
- Run full test suite (200+ programs)
- Test on both x64 and ARM64 platforms
- Verify CI/CD passes

---

## Risk Mitigation

### Compile-Time Safety Switches
Add flags to enable/disable each optimization:
```cpp
#define ENABLE_JIT_OPT_CBZ        1
#define ENABLE_JIT_OPT_MUL_CONST  1
#define ENABLE_JIT_OPT_MOD_AND    1
#define ENABLE_JIT_OPT_LOGICAL    1
```

### Rollback Plan
If critical bug found:
1. Disable via compile flag (0)
2. Rebuild and deploy
3. Investigate with DEBUG_JIT output
4. Fix and re-enable

### High-Risk Areas
- **Modulo with negative numbers:** Most likely source of bugs
- **Branch offset calculations:** CBZ/CBNZ range limitations
- **Logical immediate encoding:** Incorrect encoding produces wrong results

**Mitigation:**
- Extensive negative number testing for modulo
- Reuse existing branch fixup mechanism
- Start with lookup table, validate against known values

---

## Success Criteria

**Functional:**
- ✓ All optimizations pass correctness tests
- ✓ Zero regression in existing test suite
- ✓ Proper handling of edge cases (negatives, overflows, etc.)
- ✓ Appropriate fallback for unsupported patterns

**Performance:**
- ✓ Measured 5-15% speedup on benchmark suite
- ✓ Code size reduction of 5-10%
- ✓ No performance regression in worst case

**Code Quality:**
- ✓ Comprehensive comments explaining optimizations
- ✓ Follows existing code style and patterns
- ✓ Proper use of DEBUG_JIT macros
- ✓ Clean compile with zero warnings

---

## References

**Existing work to build on:**
- Current power-of-2 optimizations (multiply/divide)
- Immediate value encoding optimizations
- Array indexing optimization
- Identity operation optimizations (multiply by 0/1, AND with 0/-1)

**Technical references:**
- ARM Architecture Reference Manual: CBZ/CBNZ, logical immediates
- Intel 64 and IA-32 Architectures Manual: LEA instruction
- Compiler optimization literature: Strength reduction, peephole optimization
