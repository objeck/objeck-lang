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
| **mandelbrot** | 4000 | 2.78 | 9 MB |
| **binarytrees** | 17 | 20.0 | 173 MB |
| **nbody** | 50M | 43.04 | 7 MB |
| **fannkuchredux** | 12 | 94.64 | 7 MB |
| **spectralnorm** | 5500 | 118.53 | 8 MB |

**mandelbrot** and **nbody** benefit from `native`-annotated methods that JIT-compile to x64. **binarytrees** benefits from the young-generation bump allocator and direct JIT-to-JIT calling (3.3x faster than v2026.2.1). **spectralnorm** runs in the interpreter; with `native` it drops to **1.16s**.

---

## Cross-Language Comparison

Same inputs, same machine, same Docker container. All languages ran with default settings (no special flags).

| Benchmark | Objeck | Python 3.12 | Ruby 3.2 | LuaJIT 2.1 | Best |
|-----------|--------|-------------|----------|------------|------|
| **nbody** (50M) | **43.04s** | 294.39s | 553.82s | **11.88s** | LuaJIT |
| **fannkuchredux** (12) | **94.64s** | 988.84s | 3393.49s | 316.48s | **Objeck** |
| **binarytrees** (17) | **20.0s** | 10.87s | 10.60s | **6.95s** | LuaJIT |
| **spectralnorm** (5500) | 118.53s | 315.44s | 225.29s | **3.14s** | LuaJIT |

### Where Objeck Wins

- **fannkuchredux** -- 3.3x faster than LuaJIT, 10.4x faster than Python, 35.9x faster than Ruby. The JIT excels at tight integer loops with array permutations.
- **nbody** -- 6.8x faster than Python, 12.9x faster than Ruby. Getter/setter inlining + JIT compilation eliminates method call overhead.

### Where Objeck Is Competitive

- **binarytrees** -- 2.9x slower than LuaJIT, 1.8x slower than Python/Ruby. Previously 10x slower than LuaJIT. The remaining gap is per-callback overhead and constructor calls not yet inlined within JIT'd methods.

### Where Objeck Needs Improvement

- **spectralnorm** -- 38x slower than LuaJIT. Adding the `native` keyword drops Objeck to **1.16s** (2.7x behind LuaJIT). The gap is auto-JIT coverage, not code quality.

### Key Takeaways

1. **Young-generation GC closed the binarytrees gap.** Bump allocator + 128 MB nursery reduced allocation overhead ~50x and cut peak RSS from 6.2 GB to 173 MB.
2. **Direct JIT-to-JIT calling eliminates trampoline overhead.** Auto-JIT'd methods call other JIT'd methods directly without bouncing through the interpreter.
3. **Integer JIT is already excellent.** fannkuchredux is faster than LuaJIT's tracing JIT for this workload.
4. **Auto-JIT coverage is the #1 remaining bottleneck.** spectralnorm goes from 118s to 1.16s with `native` -- a 100x speedup sitting on the table.

---

## The `native` Keyword

Methods marked `native` are JIT-compiled to x64 or ARM64 machine code. All other methods run in the interpreter unless auto-JIT compiles them after 10 calls.

| spectralnorm (5500) | Time | Speedup |
|---------------------|------|---------|
| Interpreter | 118.53s | -- |
| With `native` (JIT) | **1.16s** | **100x** |

Auto-JIT now compiles methods containing `MTHD_CALL` (regular method calls). Closure/function-reference calls (`DYN_MTHD_CALL`) are not yet supported.

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
| v2026.3.0 | Apr 2026 | Young-gen bump allocator, MTHD_CALL whitelist, direct JIT-to-JIT calling, atomic mark bits | **3.3x binarytrees** |

### v2026.3.0 Detail

Seven optimizations combined to eliminate binarytrees as an order-of-magnitude bottleneck:

| Optimization | Category | Impact |
|-------------|----------|--------|
| Young-gen bump allocator | GC | **1.9x** -- `atomic_fetch_add` replaces mutex + hash-set insert. 128 MB nursery; short-lived objects die without promotion. |
| Auto-JIT operand3 dispatch fix | VM | **1.5x** -- failed JIT methods were re-attempting `Compile()` on every call. |
| Direct JIT-to-JIT calling | JIT/VM | **1.4x** -- `JitStackCallback` calls callee native code directly, eliminating 5 layers of trampoline. |
| MTHD_CALL JIT whitelist | JIT | **1.2x** -- methods containing method calls can now be JIT-compiled (x64 + ARM64). |
| Auto-JIT for MTHD_CALL | JIT | Combined -- BottomUpTree/ItemCheck auto-JIT compiled; recursive calls use direct JIT path. |
| Atomic mark bits | GC | ~5% -- lock-free CAS replaces mutex across 3 parallel mark threads. |
| MEM_START_MAX 1 MB -> 8 MB | GC | Fewer early GC cycles; `old_generation.reserve` 4096 -> 65536. |

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
| **ProcessInlineMethod for MTHD_CALL** | JIT | HIGH -- constructor/getter inlining within JIT'd methods would approach LuaJIT parity |
| **Threaded interpreter dispatch** | VM | MED -- computed gotos for ~25% interpreter speedup |
| **DYN_MTHD_CALL JIT support** | JIT | MED -- closure/function-ref calls currently blocked |
| **Escape analysis** | Compiler/VM | MED -- stack-allocate non-escaping objects |
| **SIMD vectorization** | JIT | MED -- NEON (ARM64) and SSE/AVX (x64) for array ops |

---

*Last updated: April 2026 -- Docker benchmark results on AMD Ryzen AI 9 HX 370*
