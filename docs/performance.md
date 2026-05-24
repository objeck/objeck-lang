# Objeck Performance

> **Benchmark results and cross-language comparisons**

---

## Benchmark Results

### Test Environment

| | |
|---|---|
| **CPU** | AMD Ryzen AI 9 HX 370 (12C/24T) |
| **RAM** | 32 GB DDR5 |
| **OS** | Ubuntu 24.04 (WSL2 on Windows 11) |
| **Compiler** | Objeck v2026.5.3 built from source, `-opt s3` |
| **Methodology** | 3 runs per benchmark, median reported |

### CLBG Benchmarks

Classic [Computer Language Benchmarks Game](https://benchmarksgame-team.pages.debian.net/benchmarksgame/) programs compiled with `-opt s3`.

| Benchmark | Input | Time (s) | Peak RSS |
|-----------|-------|----------|----------|
| **mandelbrot** | 4000 | 1.00 | 9 MB |
| **nbody** | 50M | 23.11 | 7 MB |
| **binarytrees** | 17 | 10.78 | 210 MB |
| **fannkuchredux** | 12 | 35.04 | 7 MB |
| **spectralnorm** | 5500 | 35.54 | 8 MB |

**mandelbrot** and **nbody** benefit from `native`-annotated methods that JIT-compile to x64. **binarytrees** benefits from the young-gen bump allocator and MTHD_CALL JIT whitelist. **spectralnorm** runs in the interpreter; with `native` (input 2000) it drops to **0.45s**.

---

## Cross-Language Comparison

Same inputs, same hardware. Objeck on WSL2; Python/Ruby times from prior Docker run on same machine.

| Benchmark | Objeck | Python 3.12 | Ruby 3.2 | Best |
|-----------|--------|-------------|----------|------|
| **nbody** (50M) | **23.11s** | 133.55s | 195.18s | **Objeck** |
| **fannkuchredux** (12) | **35.04s** | 359.04s | -- | **Objeck** |
| **binarytrees** (17) | **10.78s** | 3.31s | 3.31s | Python/Ruby |
| **spectralnorm** (5500) | 35.54s | 109.67s | 84.58s | **Objeck** |

*Python/Ruby times from prior Docker run on same hardware.*

### Where Objeck Wins

- **nbody** -- 5.8x faster than Python, 8.4x faster than Ruby. Getter/setter inlining + JIT compilation eliminates method call overhead.
- **fannkuchredux** -- 10.2x faster than Python. The JIT excels at tight integer loops with array permutations.
- **spectralnorm** -- 3.1x faster than Python, 2.4x faster than Ruby (interpreter only). With `native` it drops to **0.45s**.

### Where Objeck Needs Improvement

- **binarytrees** -- 3.3x slower than Python/Ruby. The young-gen bump allocator (128MB nursery), MTHD_CALL JIT whitelist, direct JIT-to-JIT calling, and atomic CAS mark bits combined for major speedups across releases. RSS stable at 210MB.

### Key Takeaways

1. **JIT register cache is a ~3x win.** Keeping local variables in registers after store and reusing them on subsequent loads eliminates redundant memory traffic across all benchmarks.
2. **Link-time optimization adds further gains.** `-flto=auto` enables cross-translation-unit inlining in both the compiler and VM.
3. **Integer JIT is excellent.** nbody and fannkuchredux are faster than Python and Ruby across the board.
4. **Auto-JIT coverage is the #1 bottleneck.** spectralnorm goes from 35.54s (input 5500, interpreter) to 0.45s (input 2000, `native`) -- an ~79x speedup sitting on the table.

---

## The `native` Keyword

Methods marked `native` are JIT-compiled to x64 or ARM64 machine code. All other methods run in the interpreter unless auto-JIT compiles them after 10 calls.

| spectralnorm | Input | Time | Speedup |
|-------------|-------|------|---------|
| Interpreter | 5500 | 35.54s | -- |
| With `native` (JIT) | 2000 | **0.45s** | **~79x** |

Methods marked `native` that contain `MTHD_CALL` (method calls) are JIT-compiled via ProcessStackCallback. Auto-JIT for methods with `MTHD_CALL` is blocked due to library method crashes (String:Append). Closure/function-reference calls (`DYN_MTHD_CALL`) are not yet supported.

---

## Micro-Benchmarks

Targeted benchmarks for specific optimization patterns (`programs/tests/perf/`). Compiled with `-opt s3`, median of 3 runs.

| Benchmark | Target | Time (s) | Peak RSS |
|-----------|--------|----------|----------|
| `bench_loop_invariant` | Loop-invariant expressions | 0.20 | 7 MB |
| `bench_cse` | Common subexpression elimination | 0.21 | 7 MB |
| `bench_spectralnorm_native` | Float arrays with `native` JIT (n=2000) | 0.45 | 8 MB |
| `bench_copy_prop` | Variable copy chains | 0.93 | 7 MB |
| `bench_dead_code` | Unreachable assignments | 1.38 | 7 MB |
| `bench_gc_churn` | Rapid short-lived object allocation | 0.71 | 135 MB |
| `bench_strength_ext` | Non-power-of-2 multiply patterns | 1.62 | 7 MB |
| `bench_gc_large_heap` | Large live set, GC sweep time | 0.11 | 55 MB |
| `bench_array_intensive` | Sequential array access patterns | 1.99 | 7 MB |
| `bench_method_dispatch` | Repeated method calls on objects | 2.66 | 7 MB |
| `bench_matrix_multiply` | Nested loop float computation (n=500) | 4.60 | 13 MB |

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
| v2026.3.0 | Apr 2026 | Young-gen bump allocator, MTHD_CALL whitelist, direct JIT-to-JIT calling, atomic mark bits | **2.3x binarytrees** |
| v2026.4.2 | Apr 2026 | JIT local variable register cache, LTO (`-flto=auto`), ARM64 `-mcpu=native` | **~3x all benchmarks** |
| v2026.5.3 | May 2026 | Jump table dispatch for dense integer `select` (O(1) vs O(log n) BST) | select-heavy programs; no regression on existing benchmarks |

### v2026.3.0 Detail

**Headline: binarytrees (depth=17) from 65.8s to 28.7s.** Young-gen bump allocator enabled with complete call stack fixup.

| Optimization | Category | Impact |
|-------------|----------|--------|
| Auto-JIT operand3 dispatch fix | VM | **1.5x** -- failed JIT methods were re-attempting `Compile()` on every call. |
| MTHD_CALL JIT whitelist (x64 + ARM64) | JIT | **1.2x** -- methods containing method calls can now be JIT-compiled. |
| Direct JIT-to-JIT calling | JIT/VM | Part of MTHD_CALL -- `JitStackCallback` calls callee native code directly, eliminating interpreter trampoline. |
| Atomic mark bits | GC | ~5% -- lock-free CAS replaces mutex across 3 parallel mark threads. |
| MEM_START_MAX 1 MB -> 8 MB | GC | Fewer early GC cycles; `old_generation.reserve` 4096 -> 65536. |

| Young-gen bump allocator | GC | **1.5x** (on top of above) -- `atomic_fetch_add` replaces mutex + hash-set insert. 128MB nursery; short-lived objects die without promotion. Fixed call stack fixup to include top frame pushed by direct JIT-to-JIT calls. |

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
| **Auto-JIT for MTHD_CALL** | JIT | **HIGH** -- prototyped at 20s but String:Append crashes when auto-JIT'd. Need to fix library method callback interactions. |
| **ProcessInlineMethod for MTHD_CALL** | JIT | HIGH -- constructor/getter inlining within JIT'd methods would approach LuaJIT parity |
| **Threaded interpreter dispatch** | VM | MED -- computed gotos for ~25% interpreter speedup |
| **DYN_MTHD_CALL JIT support** | JIT | MED -- closure/function-ref calls currently blocked |
| **Escape analysis** | Compiler/VM | MED -- stack-allocate non-escaping objects |

---

*Last updated: May 20, 2026 -- WSL2 benchmark results on AMD Ryzen AI 9 HX 370 (v2026.5.3)*
