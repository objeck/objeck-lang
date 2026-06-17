# Objeck Performance

> **Benchmark results and cross-language comparisons**

---

## Benchmark Results

### Test Environment

| | |
|---|---|
| **CPU** | AMD Ryzen 9 7950X3D (16C/32T) |
| **RAM** | 128 GB (62 GB visible in Docker) |
| **OS** | Ubuntu 24.04.4 in Docker (Windows 11 host) |
| **Compiler** | Objeck v2026.6.2 + JIT GC-safepoint fixes (PR #539), built from source, `-opt s3` |
| **Methodology** | 3 runs per benchmark, median reported |

> All tables below come from a **single unified Docker run** (`perf-results/docker/Dockerfile`) on the box above, so the single-language and cross-language numbers are directly comparable. Note Docker's virtualized backend runs Objeck's **interpreter**-bound benchmarks ~30–50% slower than a native (bare-metal/WSL2) build, while JIT/`native` code is nearly unaffected — so the interpreter rows here are conservative.

### CLBG Benchmarks

Classic [Computer Language Benchmarks Game](https://benchmarksgame-team.pages.debian.net/benchmarksgame/) programs compiled with `-opt s3`.

| Benchmark | Input | Time (s) | Peak RSS |
|-----------|-------|----------|----------|
| **mandelbrot** | 4000 | 0.81 | 12 MB |
| **nbody** | 50M | 17.70 | 10 MB |
| **binarytrees** | 17 | 7.79 | 207 MB |
| **fannkuchredux** | 12 | 30.67 | 12 MB |
| **spectralnorm** | 5500 | 43.11 | 11 MB |

**mandelbrot** and **nbody** benefit from `native`-annotated methods that JIT-compile to x64. **binarytrees** benefits from the young-gen bump allocator and auto-JIT for methods containing `MTHD_CALL`. **fannkuchredux** is the headline gain from the new back-edge GC-safepoint work (PR #539). **spectralnorm** runs in the interpreter; with `native` (input 2000) it drops to **0.37s**.

---

## Cross-Language Comparison

All five languages measured in **one Docker run on identical inputs** (`perf-results/docker/Dockerfile`), so these times are directly comparable to each other. Median of 3 runs.

| | |
|---|---|
| **Objeck** | v2026.6.2 + JIT GC-safepoint fixes (PR #539), `-opt s3` |
| **Python** | 3.12.3 (CPython) |
| **Ruby** | 3.2.3 |
| **LuaJIT** | 2.1 (tracing JIT) |
| **Java** | OpenJDK 21.0.11 (HotSpot, tiered JIT) |
| **Host** | AMD Ryzen 9 7950X3D (16C/32T), Ubuntu 24.04.4 in Docker (32 vCPU / 62 GB) |

> Same unified Docker run as the single-language tables above, so every number on this page is directly comparable. As noted, Objeck's *interpreter*-bound benchmarks (e.g. spectralnorm) would run ~30–50% faster on a native build; JIT/`native` code is essentially unaffected by Docker.

| Benchmark | Input | Objeck | Python 3.12 | Ruby 3.2 | LuaJIT 2.1 | Java 21 | Best |
|-----------|-------|--------|-------------|----------|------------|---------|------|
| **nbody** | 50M | 17.70s | 136.91s | 201.22s | 4.30s | **2.30s** | Java |
| **binarytrees** | 17 | 7.79s | 3.51s | 3.49s | 3.34s | **0.24s** | Java |
| **spectralnorm** | 5500 | 43.11s | 125.74s | 85.83s | **1.11s** | 1.17s | LuaJIT |
| **fannkuchredux** | 12 | 30.67s | 415.40s | 1166.46s | 117.07s | **20.60s** | Java |

### Reading the table

- **Java (HotSpot) sets the ceiling — it wins 3 of 4** (nbody, binarytrees, fannkuchredux); LuaJIT takes spectralnorm (1.11s vs Java's 1.17s). A mature tiered JIT plus escape analysis and a generational GC is the bar a younger JIT is measured against.
- **Objeck beats both Python and Ruby on 3 of 4** — by wide margins where the JIT engages: nbody **7.7x** faster than Python / **11.4x** faster than Ruby; fannkuchredux **13.5x** / **38x**; spectralnorm **2.9x** / **2.0x** (interpreter only).
- **Objeck beats LuaJIT on fannkuchredux** (30.67s vs 117.07s, **3.8x faster**) — the integer-array permutation/flip pattern is a poor fit for LuaJIT's tracing JIT, and Objeck's method-JIT handles it well. The new back-edge GC-safepoint work (PR #539) roughly halved Objeck's fannkuch time, closing the gap to Java to ~1.5x.
- **binarytrees is Objeck's weak spot — slower than even the interpreters here.** Allocation/GC throughput dominates; Java's generational GC + escape analysis is **~32x** faster (0.24s vs 7.79s). This is the clearest improvement target (P1 on the roadmap).
- **spectralnorm in the interpreter (43s) understates Objeck.** With `native` the same kernel drops to **0.37s at input 2000** (see micro-benchmarks); the gap is auto-JIT coverage (`DYN_MTHD_CALL`), not raw capability.

### Key Takeaways

1. **Against scripting peers, Objeck is decisively faster.** On JIT-friendly numeric and integer loops it leads Python and Ruby by 3–30x.
2. **Against compiled-class JITs (Java, LuaJIT), Objeck trails on float loops but is competitive — and ahead — on the right integer workload** (fannkuchredux beats LuaJIT).
3. **Allocation throughput is the #1 gap.** binarytrees is the one benchmark where Objeck loses to everything; escape analysis / faster young-gen allocation is the highest-leverage future work.
4. **Auto-JIT coverage gap remains for `DYN_MTHD_CALL`.** spectralnorm runs in the interpreter (43s) but `native` reaches 0.37s (input 2000) — a large speedup currently left on the table for closure/function-ref-heavy code.
5. **Measurement environment matters.** Objeck's interpreter is more sensitive to virtualization than its JIT; native (non-Docker) numbers would be meaningfully better than the Docker numbers shown here.

---

## The `native` Keyword

Methods marked `native` are JIT-compiled to x64 or ARM64 machine code. All other methods run in the interpreter unless auto-JIT compiles them after 10 calls.

| spectralnorm | Input | Time | Speedup |
|-------------|-------|------|---------|
| Interpreter | 5500 | 43.11s | -- |
| With `native` (JIT) | 2000 | **0.37s** | **~100x** (smaller input) |

Methods marked `native` that contain `MTHD_CALL` are JIT-compiled via `ProcessStackCallback`. Auto-JIT now also compiles methods containing `MTHD_CALL` after 10 calls — the same code path, just triggered automatically rather than by the programmer. Closure/function-reference calls (`DYN_MTHD_CALL`) are not yet auto-JIT'd.

---

## Micro-Benchmarks

Targeted benchmarks for specific optimization patterns (`programs/tests/perf/`). Compiled with `-opt s3`, median of 3 runs.

| Benchmark | Target | Time (s) | Peak RSS |
|-----------|--------|----------|----------|
| `bench_loop_invariant` | Loop-invariant expressions (LICM) | 0.24 | 12 MB |
| `bench_cse` | Common subexpression elimination | 0.25 | 11 MB |
| `bench_spectralnorm_native` | Float arrays with `native` JIT (n=2000) | 0.37 | 10 MB |
| `bench_copy_prop` | Variable copy chains | 1.19 | 10 MB |
| `bench_dead_code` | Unreachable assignments | 1.68 | 10 MB |
| `bench_gc_churn` | Rapid short-lived object allocation | 0.64 | 138 MB |
| `bench_strength_ext` | Non-power-of-2 multiply patterns | 2.14 | 10 MB |
| `bench_gc_large_heap` | Large live set, GC sweep time | 0.07 | 58 MB |
| `bench_array_intensive` | Sequential array access patterns | 2.40 | 12 MB |
| `bench_method_dispatch` | Repeated method calls on objects | 2.96 | 11 MB |
| `bench_matrix_multiply` | Nested loop float computation (n=500) | 4.82 | 16 MB |
| `bench_tco` | Tail-recursive accumulator (TCO, n=1M×200) | 0.33 | 10 MB |

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
| v2026.6.0 | May 2026 | Auto-JIT for MTHD_CALL, 15 new interpreter fast-path opcodes, TCO, LICM | **matrix_multiply −14%, dead_code −15%, array_intensive −12%, binarytrees −7%, mandelbrot −6%** |

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

### v2026.6.0 Detail

**Headline: broad 5–15% speedup across all interpreter and JIT benchmarks.**

| Optimization | Category | Impact |
|-------------|----------|--------|
| Auto-JIT for MTHD_CALL | JIT | **−7% binarytrees, −6% mandelbrot** — methods containing method calls now auto-JIT after 10 invocations, same `ProcessStackCallback` path as explicit `native`. Previously blocked due to false positives from library method interactions. |
| 15 new interpreter fast-path inline opcodes | VM | **−10 to −15% micro-benchmarks** — comparisons (`EQL/NEQL/LES/GTR/LES_EQL/GTR_EQL`), logical (`AND/OR`), bitwise (`BIT_AND/OR/XOR`), shifts (`SHL/SHR`), `LOAD_CHAR_LIT` added to the inline switch. Eliminates function-pointer dispatch overhead for the most common interpreter instructions. |
| Tail Call Optimization (TCO) | Compiler (`-opt s1+`) | Self-recursive tail calls rewritten to `POP_INT` + param restores + `JMP` — eliminates call-frame growth. Stack overflow at n=1M without TCO; correct in O(1) stack space with it. |
| Loop-Invariant Code Motion (LICM) | Compiler (`-opt s2+`) | Hoists `arr->Size()` reads and pure arithmetic statements out of loop bodies when inputs are loop-invariant. |

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

## Speedup Roadmap

Prioritized by leverage, grounded in the cross-language run above. The single
clearest signal: **Objeck already beats Python/Ruby on JIT-friendly loops but
loses to everything — including the interpreters — on allocation-bound work
(binarytrees), and trails Java/LuaJIT on float loops.** So the highest-value work
is allocation throughput and float codegen, not more integer-loop tuning.

### P0 — Recover the GC-safepoint regression *(complete — both arches)*

The cooperative stop-the-world work (v2026.6.2) emits an unconditional
`call MemoryManager::SafePoint` at **every JIT label**, which regressed
label-dense integer loops (fannkuchredux ~35s → ~59s; the call/ret + optimization
barrier is paid every loop iteration) while barely touching float/call-dominated
loops (spectralnorm_native +7%).

| Step | Status | Impact |
|------|--------|--------|
| Inline the flag test, call only when a collection is active (`cmp [stw_active],0; je skip`) | **AMD64 done, validated** (branch `perf/jit-safepoint-inline`); ARM64 mirror pending on-device test | fannkuch 71.6s → 56.9s (**−20%**, recovers ~71% of the regression); GC/JIT + multithreaded-STW stress green |
| Cache `&stw_active` in a callee-saved reg at the prologue | **AMD64 done, validated** (R12); **ARM64 done, validated on Apple Silicon** (X19) | AMD64: each label's poll is now a 5-byte `cmp byte [r12],0` (was a 10-byte `movabs` + 3-byte `cmp`); fannkuch-12 38.4s → 34.7s (**−10%**, far less run-to-run variance). ARM64: each poll drops from a 2-instr const-pool address load + `ldarb` to just `ldarb W11,[X19]`; fannkuch-11 2.52s → 2.49s (**~−1%**, min-of-6 — smaller win since the ARM64 address load was already a cheap const-pool `ldr`, not AMD64's 10-byte `movabs`). The fully-packed RED_ZONE frame had no free fixed slot and `RegisterRoot` zeros the local region at entry, so X19 is saved in the one untouched spot: the top 8 bytes of the frame (`final_local_space - 8`, above the zeroed region and below the incoming args). 158/158 ARM64 regression × 3 + `core_thread_gc_stress`/`jit_gc_stress` × 5 clean (no STW deadlock) |
| Emit the poll only at true loop back-edges, not at if/else merges | **AMD64 done, validated**; **ARM64 done, validated on Apple Silicon** | reuses the back-edge pre-scan: a label gets a poll only if it is the target of a backward jump (loop header). AMD64 inlined callees carry no control flow (`CanInlineMethod` rejects `JMP`/`LBL`) and ARM64 has no inlining, so every loop is covered. AMD64 fannkuch-12 34.7s → 32.5s (**−6%** more); 157/158 regression pass with no STW deadlock (threading/GC tests green). ARM64 mirrors the same logic (note: ARM64 JMP operand is target-index + 1) |

### P1 — Allocation & GC throughput *(the #1 cross-language gap)*

binarytrees is the one benchmark Objeck loses to *every* peer (Java 22x, even the
interpreters ~2x). The young-gen bump allocator helped, but per-object allocation
+ a call into the allocator still dominates.

| Opportunity | Category | Expected Impact |
|-------------|----------|----------------|
| **Escape analysis** — stack-allocate objects that don't outlive their frame (tree nodes, short-lived temps) | Compiler/VM | **HIGH** — removes GC pressure entirely for non-escaping values; directly targets the binarytrees loss |
| **Inline the young-gen bump-alloc fast path into JIT'd code** | JIT | **HIGH** — replace the allocator `call` for nursery objects with an inline `atomic_fetch_add` + bounds check |
| **Thread-local allocation buffers (TLAB)** | GC | MED — cut `young_offset` CAS contention under multithreaded allocation |

### P2 — Close JIT coverage gaps

| Opportunity | Category | Expected Impact |
|-------------|----------|----------------|
| **DYN_MTHD_CALL auto-JIT** — closure / function-ref calls | JIT | **HIGH** — spectralnorm goes 43s (interpreter) → 0.37s (`native`, input 2000); this gap keeps float-method code in the interpreter. prgm70/71 segfault patterns need root-cause first |
| **Float JIT codegen + float fast-path opcodes** (`LOAD_FLOAT_LIT`, `ADD/SUB/MUL_FLOAT`) | JIT/VM | **HIGH** — Java and LuaJIT lead specifically on float loops (nbody, spectralnorm); the inline switch is integer-only today |
| **ProcessInlineMethod for MTHD_CALL** — getter/ctor inlining inside JIT'd methods | JIT | MED — eliminates the `ProcessStackCallback` trampoline. Blocked by INSTANCE_MEM offset corruption in constructors; needs frame-layout work |
| **Per-call-site monomorphic virtual dispatch cache** | VM | LOW — store the last (class→method) pointer in the instruction to skip the hash lookup in the common case |

### P3 — Measurement & methodology

| Opportunity | Impact |
|-------------|--------|
| Stand up a **native (non-Docker) cross-language harness** so peer comparisons aren't muddied by the ~30–50% Docker interpreter overhead documented above | gives bare-metal interpreter numbers; the current page is entirely Docker |
| Add **Java + LuaJIT to the gating perf CI** so regressions like the safepoint one are caught automatically | the fannkuch regression shipped unnoticed because nothing compared releases head-to-head |

---

*Last updated: June 17, 2026 -- the entire page (CLBG, micro, and cross-language) was re-measured in one unified Docker run on an AMD Ryzen 9 7950X3D (32 vCPU / 62 GB) with the JIT GC-safepoint fixes merged (PR #539, Objeck v2026.6.2): fannkuchredux dropped 59.4s → 30.7s. Numbers are Docker (interpreter-bound rows ~30–50% slower than a native build); a native re-run is future work. P0 safepoint roadmap: complete on both arches — AMD64 (all three steps) and ARM64 (back-edge + X19 register-cache, validated on Apple Silicon).*
