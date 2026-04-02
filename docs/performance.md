# Objeck Performance

> **Benchmark results and cross-language comparisons**

---

## Benchmark Results

### Test Environment

All benchmarks ran in a single Docker container (Ubuntu 24.04) to ensure reproducible, comparable results across languages.

| | |
|---|---|
| **CPU** | AMD Ryzen AI 9 HX 370 (12C/24T) |
| **RAM** | 32 GB DDR5 (15 GB allocated to Docker) |
| **OS** | Ubuntu 24.04.4 LTS (Docker on Windows 11) |
| **Compiler** | Objeck built from source, `-opt s3` |
| **Methodology** | 3 runs per benchmark, median reported |

### CLBG Benchmarks

Classic [Computer Language Benchmarks Game](https://benchmarksgame-team.pages.debian.net/benchmarksgame/) programs compiled with `-opt s3`.

| Benchmark | Input | Time (s) | Peak RSS |
|-----------|-------|----------|----------|
| **mandelbrot** | 4000 | 2.80 | 9 MB |
| **nbody** | 50M | 42.88 | 7 MB |
| **binarytrees** | 17 | 42.0 | 763 MB |
| **fannkuchredux** | 12 | 91.21 | 7 MB |
| **spectralnorm** | 5500 | 113.58 | 8 MB |

**mandelbrot** and **nbody** benefit from `native`-annotated methods that JIT-compile to x64. **binarytrees** benefits from the MTHD_CALL JIT whitelist, direct JIT-to-JIT calling, atomic CAS mark bits, and operand3 dispatch fix (1.5x faster than v2026.2.1). **spectralnorm** runs in the interpreter; with `native` it drops to **1.16s**.

---

## Cross-Language Comparison

Same inputs, same machine, same Docker container. All languages ran with default settings (no special flags).

| Benchmark | Objeck | Python 3.12 | Ruby 3.2 | LuaJIT 2.1 | Best |
|-----------|--------|-------------|----------|------------|------|
| **nbody** (50M) | **42.88s** | 294.39s | 553.82s | **11.88s** | LuaJIT |
| **fannkuchredux** (12) | **91.21s** | 988.84s | 3393.49s | 316.48s | **Objeck** |
| **binarytrees** (17) | **42.0s** | 10.36s | 10.02s | **6.89s** | LuaJIT |
| **spectralnorm** (5500) | 113.58s | 315.44s | 225.29s | **3.14s** | LuaJIT |

### Where Objeck Wins

- **fannkuchredux** -- 3.5x faster than LuaJIT, 10.8x faster than Python, 37.2x faster than Ruby. The JIT excels at tight integer loops with array permutations.
- **nbody** -- 6.9x faster than Python, 12.9x faster than Ruby. Getter/setter inlining + JIT compilation eliminates method call overhead.

### Where Objeck Needs Improvement

- **binarytrees** -- 6.1x slower than LuaJIT, 4.1x slower than Python/Ruby. Previously 10x slower than LuaJIT. The MTHD_CALL JIT whitelist, direct JIT-to-JIT calling, and GC improvements gave a 1.5x speedup. The young-gen bump allocator (tested at 3.3x faster) is disabled pending GC fixup coverage for array interior pointers.
- **spectralnorm** -- 36x slower than LuaJIT. Adding the `native` keyword drops Objeck to **1.16s** (2.7x behind LuaJIT). The gap is auto-JIT coverage, not code quality.

### Key Takeaways

1. **Allocation overhead is the #1 bottleneck.** The young-gen bump allocator prototype showed 3.3x improvement on binarytrees but needs GC fixup fixes before shipping. Currently all objects go through the old-gen path (mutex + hash-set insert).
2. **Direct JIT-to-JIT calling works.** When explicit `native` methods call other JIT'd methods, the call goes directly to native code without interpreter trampoline.
3. **Integer JIT is already excellent.** fannkuchredux is faster than LuaJIT's tracing JIT for this workload.
4. **Auto-JIT coverage is the #2 bottleneck.** spectralnorm goes from 113s to 1.16s with `native` -- a 100x speedup sitting on the table.

---

## The `native` Keyword

Methods marked `native` are JIT-compiled to x64 or ARM64 machine code. All other methods run in the interpreter unless auto-JIT compiles them after 10 calls.

| spectralnorm (5500) | Time | Speedup |
|---------------------|------|---------|
| Interpreter | 118.53s | -- |
| With `native` (JIT) | **1.16s** | **100x** |

Methods marked `native` that contain `MTHD_CALL` (method calls) are JIT-compiled via ProcessStackCallback. Auto-JIT for methods with `MTHD_CALL` is blocked due to library method crashes (String:Append). Closure/function-reference calls (`DYN_MTHD_CALL`) are not yet supported.

---

## Micro-Benchmarks

Targeted benchmarks for specific optimization patterns (`programs/tests/perf/`). Compiled with `-opt s3`, median of 3 runs.

| Benchmark | Target | Time (s) | Peak RSS |
|-----------|--------|----------|----------|
| `bench_loop_invariant` | Loop-invariant expressions | 0.64 | 7 MB |
| `bench_cse` | Common subexpression elimination | 0.65 | 7 MB |
| `bench_spectralnorm_native` | Float arrays with `native` JIT | 1.16 | 8 MB |
| `bench_copy_prop` | Variable copy chains | 3.05 | 7 MB |
| `bench_dead_code` | Unreachable assignments | 4.39 | 7 MB |
| `bench_gc_churn` | Rapid short-lived object allocation | 4.74 | 15 MB |
| `bench_strength_ext` | Non-power-of-2 multiply patterns | 5.24 | 7 MB |
| `bench_gc_large_heap` | Large live set, GC sweep time | 0.82 | 95 MB |
| `bench_array_intensive` | Sequential array access patterns | 6.44 | 7 MB |
| `bench_method_dispatch` | Repeated method calls on objects | 7.52 | 7 MB |
| `bench_matrix_multiply` | Nested loop float computation (n=500) | 13.41 | 13 MB |

### Running Benchmarks

```bash
# Docker (recommended for reproducible results)
docker build -t objeck-bench -f perf-results/docker/Dockerfile .
docker run --rm -v "$(pwd)/perf-results/docker-results:/results" objeck-bench

# Linux/WSL (using deploy directory)
bash perf-results/run_benchmarks.sh <deploy_dir> <output_dir> [num_runs]
```

---

## Optimization History

| Version | Date | Key Optimization | Impact |
|---------|------|-----------------|--------|
| Pre-2024 | -- | Stack-based VM, interpreter-only | -- |
| v2024.x | 2024 | JIT compilers (ARM64 + x64), basic bytecode optimizer | -- |
| v2026.2.0 | Feb 2026 | O(1) GC lookups, ARM64 JIT optimizations, instruction rewrite framework | Foundation |
| v2026.2.1 | Feb 2026 | Inline limit 128->256, CSE, dead code elimination | **4.38x nbody** |
| v2026.2.1+ | Mar 2026 | JIT whitelist fix: 3 instructions had code generators but weren't enabled | **28.4x mandelbrot** |
| v2026.3.0 | Apr 2026 | MTHD_CALL whitelist, direct JIT-to-JIT calling, atomic mark bits, GC tuning | **1.5x binarytrees** |

### v2026.3.0 Detail

**Headline: binarytrees (depth=17) from 65.8s to 42.0s.** Five shipped optimizations plus a prototype bump allocator (3.3x, pending GC fixup).

| Optimization | Category | Impact |
|-------------|----------|--------|
| Auto-JIT operand3 dispatch fix | VM | **1.5x** -- failed JIT methods were re-attempting `Compile()` on every call. |
| MTHD_CALL JIT whitelist (x64 + ARM64) | JIT | **1.2x** -- methods containing method calls can now be JIT-compiled. |
| Direct JIT-to-JIT calling | JIT/VM | Part of MTHD_CALL -- `JitStackCallback` calls callee native code directly, eliminating interpreter trampoline. |
| Atomic mark bits | GC | ~5% -- lock-free CAS replaces mutex across 3 parallel mark threads. |
| MEM_START_MAX 1 MB -> 8 MB | GC | Fewer early GC cycles; `old_generation.reserve` 4096 -> 65536. |

**Prototype (not shipped):** Young-gen bump allocator showed **3.3x** improvement (65.8s -> 20.0s) but FixupRoots doesn't handle all pointer locations during young->old promotion. Array interior pointers and interpreter temp values are missed, causing crashes in XML/string-heavy tests. Infrastructure is in place (young_region, write barriers, StackFrameMonitor op_stack tracking) for future enablement.

**How it was found:** GC profiling revealed only 18% of binarytrees runtime was in GC. The remaining 82% was per-object allocation overhead and interpreter dispatch -- contradicting the assumption that "GC is the #1 bottleneck."

---

## What We Tried and Reverted

| Optimization | Result | Why |
|-------------|--------|-----|
| Extended strength reduction (x*3,5,7,9,15) | 0.66x slower | Modern CPUs execute MUL in 3 cycles; shift+add has more dispatch overhead |
| Copy propagation | 0.85x slower | Changed patterns the JIT register allocator expected |
| GC lock-free mark via snapshot | 0.64x slower | Copying entire `allocated_memory` set before each mark phase was O(n) |
| Inline limit 512 | 0.91x slower | Exceeded JIT register allocator capacity |
| Auto-JIT MTHD_CALL via interpreter trampoline | 0.5x slower | Per-call trampoline overhead exceeded interpreter cost. Fixed by adding direct JIT-to-JIT calling. |
| ProcessInlineMethod for MTHD_CALL | Crashes | Inlining constructors corrupts INSTANCE_MEM save/restore offsets. Needs frame layout investigation. |

---

## Future Work

| Opportunity | Category | Expected Impact |
|-------------|----------|----------------|
| **Young-gen bump allocator fixup** | GC | **VERY HIGH** -- 3.3x binarytrees improvement prototyped (42s -> 20s). Needs FixupRoots coverage for array interior pointers and interpreter temps. |
| **Auto-JIT for MTHD_CALL** | JIT | **HIGH** -- prototyped at 20s but String:Append crashes. Need to fix library method callback interactions. |
| **ProcessInlineMethod for MTHD_CALL** | JIT | HIGH -- constructor/getter inlining within JIT'd methods would approach LuaJIT parity |
| **Threaded interpreter dispatch** | VM | MED -- computed gotos for ~25% interpreter speedup |
| **DYN_MTHD_CALL JIT support** | JIT | MED -- closure/function-ref calls currently blocked |
| **Escape analysis** | Compiler/VM | MED -- stack-allocate non-escaping objects |

---

*Last updated: April 2026 -- Docker benchmark results on AMD Ryzen AI 9 HX 370*
