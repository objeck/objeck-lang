# Auto-JIT Implementation Status

## DONE

### Code Changes (all complete, build succeeds)
1. **core/vm/common.h** — Added `jit_call_count` field, init to 0, `GetJitCallCount()` + `IncrementJitCallCount()` accessors
2. **core/vm/arch/jit/jit_common.h** — Added `JIT_AUTO_THRESHOLD 10` define, `TryAutoJitCompile()` declaration
3. **core/vm/arch/jit/jit_common.cpp** — Added platform includes, `TryAutoJitCompile()` with `_NO_JIT` guard, auto-JIT counting in `MTHD_CALL`/`DYN_MTHD_CALL` JitStackCallback handler
4. **core/vm/interpreter.cpp** — Added auto-JIT counting in BOTH `ProcessDynamicMethodCall` and `ProcessMethodCall` (interpreter-level), plus routing check `|| called->GetNativeCode()`

### Key Design Change from Original Plan
The original plan only counted calls in `JitStackCallback` (JIT callback). This doesn't work because the entry point (`Main`) runs interpreted, so no JIT callback ever fires for a program without `native` keywords.

**Fix:** Added counting in `ProcessMethodCall` and `ProcessDynamicMethodCall` too. This fires once per method call (not per bytecode), so overhead is minimal — just an increment + compare per call.

### Build
- MSBuild Release|x64 succeeded (1 pre-existing LNK4098 warning, 0 errors)
- New obr.exe copied to both `deploy-x64/bin/` and `test-x64/bin/`

### Tests
- **12/12 regression tests PASS** (all .obs files in programs/regression/)

### Laptop Benchmark Results (not workstation — expect different numbers)
- spectralnorm (no `native`): ~158s with auto-JIT vs ~286s pure interpreted = **1.8x speedup**
- binarytrees: not completed (too slow on laptop battery)

## TODO — Run on Workstation

### Rebuild on workstation
```bash
# From VS developer prompt or MSBuild:
MSBuild.exe core/vm/vs/vm.vcxproj -p:Configuration=Release -p:Platform=x64 -t:Build

# Copy to deploy
cp core/vm/Release/win64/obr.exe core/release/deploy-x64/bin/obr.exe
```

### Benchmarks (already compiled .obe files ready)
```bash
BIN="C:/Users/objec/Documents/Code/objeck-lang/core/release/deploy-x64/bin"
CLBG="C:/Users/objec/Documents/Code/objeck-lang/programs/tests/clbg"

# spectralnorm — NO native keyword, tests auto-JIT
# Baseline: ~8s interpreted, target: closer to 0.47s (full native)
time "$BIN/obr.exe" "$CLBG/spectralnorm.obe" 5500

# binarytrees — HAS native keyword, tests no regression
time "$BIN/obr.exe" "$CLBG/binarytrees.obe" 21
```

### Performance Investigation
If spectralnorm speedup is less than expected (~36x), investigate:
- Whether `A(i,j)` inner function is actually getting JIT-compiled (it's called from loops inside other auto-JIT'd methods)
- Whether auto-compiled methods calling other auto-compiled methods properly route through JIT
- Consider if threshold 10 is too high for functions called from tight loops in other compiled methods

### Optional
- Run fannkuchredux, nbody benchmarks for stability
- Run full benchmark suite: `bash perf-results/run_benchmarks.sh <deploy_dir> <output_dir>`
