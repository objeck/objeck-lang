# Performance Optimization Debug Context

## Background

Three optimization branches were implemented and merged to master:
- `perf/gen-gc` - Two-generation garbage collector (young/old, minor/major GC, write barriers)
- `perf/compiler-opts` - Dead block elimination, peephole optimization, VM inline hot opcodes, frame cache optimization
- `perf/combined-v2` - Cherry-pick merge of both

After merging, two runtime bugs were found and fixed (MSVC `__attribute__` -> `__declspec`, auto-JIT `exit(1)` -> graceful failure). But code_doc generation still crashes with heap corruption.

## Current State of Fixes (this commit)

### 1. Peephole Optimization - REMOVED
**Files:** `core/compiler/optimization.cpp`, `core/compiler/optimization.h`

The peephole pass (PeepholeOptimize) at s3 optimization level caused heap corruption (STATUS_HEAP_CORRUPTION 0xC0000374). Confirmed by bisecting optimization levels:
- `-opt s0`: PASS
- `-opt s1`: PASS (dead block elimination OK)
- `-opt s2`: PASS (strength reduction + dead code elimination OK)
- `-opt s3` with peephole: CRASH
- `-opt s3` without peephole: still CRASH (see #2)

The peephole pass has been fully removed (function + declaration + pipeline call).

### 2. Inline Hot Opcodes - REMOVED
**File:** `core/vm/interpreter.cpp`

Even after removing peephole, s3 still crashed. Further isolation found that the **inline hot opcodes** (a switch statement inlining LOAD_INT_LIT, LOAD_LOCL_INT_VAR, STOR_LOCL_INT_VAR, ADD_INT, SUB_INT, MUL_INT, LBL, JMP directly in the dispatch loop) interact badly with s3-level InstructionReplacement pass output.

- s2 + inline opcodes: PASS
- s3 + inline opcodes: CRASH (heap corruption)
- s3 + dispatch table only: PASS

The inline implementations were verified to match the dispatch handlers exactly (same arithmetic, same operand order). The root cause is unclear - possibly an MSVC optimization bug with `continue` inside a switch inside a do-while loop, or a subtle aliasing issue. The inline opcodes have been removed; the dispatch loop now uses the function pointer table uniformly.

### 3. Minor GC (CollectMinor) - DISABLED
**File:** `core/vm/arch/memory.cpp`

CollectMinor has been reduced to a no-op (`return;`). Two bugs were found and fixed in the sweep/promotion paths, but the minor GC still has correctness issues:

**Fix applied - minor GC sweep:** During minor GC, old generation objects must be skipped during sweep since they aren't fully traced. Added early `continue` in the sweep loop for objects with `GC_OLD_BIT` set.

**Fix applied - promotion remembered set:** Newly promoted objects (young -> old) must be added to the remembered set since they may reference young objects. Added `GC_RSET_BIT` and `remembered_set.push_back(mem)` during promotion.

**Still broken:** Even with these fixes, code_doc crashes with access violation (0xC0000005) when minor GC is enabled. The old-to-young reference tracking via write barriers + remembered set has deeper correctness issues that need investigation.

### 4. Frame Cache Optimization - REVERTED
**File:** `core/vm/interpreter.cpp` (ReleaseStackFrame)

The optimization to clear only `method->GetMemorySize()` bytes instead of full `LOCAL_SIZE` had a units bug (`GetMemorySize()` returns size_t slot count, not bytes). It was fixed but then confirmed NOT to be the cause of the remaining crash. Reverted to the original `memset(frame->mem, 0, LOCAL_SIZE * sizeof(char))` for safety.

## What Still Works

- **Dead block elimination** (s1+) - Removes unreachable code after unconditional JMPs. Tested and working.
- **Generational GC data structures** - young/old generation tracking, write barriers, remembered set. The major GC (CollectMajor) works correctly with these. Minor GC is disabled.
- **GC tuning** from earlier commits - Larger heap, atomic marking, faster sweep.
- **Auto-JIT** - With graceful failure for unknown instructions.

## Verification Status

- **Regression tests:** 12/12 PASS
- **code_doc generation:** PASS (at default s3 optimization, with peephole removed and inline opcodes removed)
- **Build:** MSVC x64 Release builds successfully

## TODO / Future Investigation

1. **Inline hot opcodes:** The performance benefit was real (~5-10% on compute benchmarks). Worth investigating the MSVC interaction. Could try:
   - Adding `volatile` or `__declspec(noinline)` to prevent MSVC optimization
   - Using a different inlining approach (computed goto instead of switch)
   - Testing with Clang/GCC to confirm if MSVC-specific

2. **Minor GC:** The generational approach could significantly help allocation-heavy benchmarks (binarytrees). Needs:
   - Thorough review of write barrier placement (are ALL old->young stores covered?)
   - Review of remembered set scanning in CollectMinor
   - Possibly a card-marking approach instead of remembered set
   - Testing with a simpler program first (not code_doc)

3. **Peephole optimization:** The patterns looked mathematically correct. Worth re-investigating with careful bytecode diffing (dump s2 vs s3 output and compare).

## Helper Scripts (not committed, in repo root)

- `run_build.bat` - Calls vcvarsall.bat + deploy_windows.cmd with full paths
- `run_regression.bat` - Runs regression tests + code_doc64
- `run_codedoc.bat` - Standalone code_doc compile + run with configurable -opt level

These use PowerShell wrapping to run from bash:
```
powershell.exe -NoProfile -Command "& cmd.exe /c 'C:\...\run_build.bat' 2>&1 | Out-String"
```
