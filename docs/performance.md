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
| **Compiler** | Objeck v2026.6.2 + P2 JIT work (`DYN_MTHD_CALL` auto-JIT, interpreter float fast-path, TCO/float/shutdown correctness fixes), built from source, `-opt s3` |
| **Methodology** | 3 runs per benchmark, median reported |
| **Date** | 2026-06-21 (unified Docker run) |

> All tables below come from a **single unified Docker run** (`perf-results/docker/Dockerfile`) on the box above, so the single-language and cross-language numbers are directly comparable. Note Docker's virtualized backend runs Objeck's **interpreter**-bound benchmarks ~30–50% slower than a native (bare-metal/WSL2) build, while JIT/`native` code is nearly unaffected — so the interpreter rows here are conservative.

### CLBG Benchmarks

Classic [Computer Language Benchmarks Game](https://benchmarksgame-team.pages.debian.net/benchmarksgame/) programs compiled with `-opt s3`.

| Benchmark | Input | Time (s) | Peak RSS |
|-----------|-------|----------|----------|
| **mandelbrot** | 4000 | 0.87 | 12 MB |
| **nbody** | 50M | 18.42 | 10 MB |
| **binarytrees** | 17 | 8.53 | 206 MB |
| **fannkuchredux** | 12 | 32.32 | 10 MB |
| **spectralnorm** | 5500 | 44.86 | 11 MB |

**mandelbrot** and **nbody** benefit from `native`-annotated methods that JIT-compile to x64. **binarytrees** benefits from the young-gen bump allocator and auto-JIT for methods containing `MTHD_CALL`. **fannkuchredux** is the headline gain from the new back-edge GC-safepoint work (PR #539). **spectralnorm** is a closure/function-reference kernel: its calls now auto-JIT (`DYN_MTHD_CALL`), but at the default threshold a single short run still pays JIT warmup (the ~45s above). Forcing compilation with `OBJECK_JIT_THRESHOLD=1` runs the same 5500 input in **3.3s (~14x)**, and at n=2000 it reaches **~0.46s** — approaching the hand-`native` kernel (0.37s). The remaining gap is JIT warmup/threshold tuning, not coverage.

---

## Cross-Language Comparison

All five languages measured in **one Docker run on identical inputs** (`perf-results/docker/Dockerfile`), so these times are directly comparable to each other. Median of 3 runs.

| | |
|---|---|
| **Objeck** | v2026.6.2 + P2 JIT work (`DYN_MTHD_CALL` auto-JIT, float fast-path), `-opt s3` |
| **Python** | 3.12.3 (CPython) |
| **Ruby** | 3.2.3 |
| **LuaJIT** | 2.1 (tracing JIT) |
| **Java** | OpenJDK 21.0.11 (HotSpot, tiered JIT) |
| **Host** | AMD Ryzen 9 7950X3D (16C/32T), Ubuntu 24.04.4 in Docker (32 vCPU / 62 GB) |

> Same unified Docker run as the single-language tables above, so every number on this page is directly comparable. As noted, Objeck's *interpreter*-bound benchmarks (e.g. spectralnorm) would run ~30–50% faster on a native build; JIT/`native` code is essentially unaffected by Docker.

| Benchmark | Input | Objeck | Python 3.12 | Ruby 3.2 | LuaJIT 2.1 | Java 21 | Best |
|-----------|-------|--------|-------------|----------|------------|---------|------|
| **nbody** | 50M | 18.42s | 137.70s | 216.06s | 4.57s | **2.38s** | Java |
| **binarytrees** | 17 | 8.53s | 3.72s | 3.73s | 3.86s | **0.52s** | Java |
| **spectralnorm** | 5500 | 44.86s | 131.63s | 89.91s | **1.15s** | 1.20s | LuaJIT |
| **fannkuchredux** | 12 | 32.32s | 432.21s | 1252.25s | 122.52s | **21.30s** | Java |

### Reading the table

- **Java (HotSpot) sets the ceiling — it wins 3 of 4** (nbody, binarytrees, fannkuchredux); LuaJIT takes spectralnorm (1.15s vs Java's 1.20s). A mature tiered JIT plus escape analysis and a generational GC is the bar a younger JIT is measured against.
- **Objeck beats both Python and Ruby on 3 of 4** — by wide margins where the JIT engages: nbody **7.5x** faster than Python / **11.7x** faster than Ruby; fannkuchredux **13.4x** / **38.7x**; spectralnorm **2.9x** / **2.0x** (interpreter only).
- **Objeck beats LuaJIT on fannkuchredux** (32.32s vs 122.52s, **3.8x faster**) — the integer-array permutation/flip pattern is a poor fit for LuaJIT's tracing JIT, and Objeck's method-JIT handles it well. The back-edge GC-safepoint work (PR #539) roughly halved Objeck's fannkuch time, leaving the gap to Java at ~1.5x.
- **binarytrees is Objeck's weak spot — slower than even the interpreters here.** Allocation/GC throughput dominates; Java's generational GC + escape analysis is **~16x** faster (0.52s vs 8.53s; Java's binarytrees has high run-to-run JIT-warmup variance). This is the clearest improvement target (P1 on the roadmap).
- **spectralnorm's ~45s is a JIT-warmup artifact, not raw capability.** Its closure calls (`DYN_MTHD_CALL`) now auto-JIT; the 45s is a single short run dominated by warmup at the default threshold. With `OBJECK_JIT_THRESHOLD=1` the same 5500 run is **3.3s (~14x)** and n=2000 reaches **~0.46s**, approaching the hand-`native` kernel. The remaining lever is auto-JIT threshold tuning, not coverage.

### Key Takeaways

1. **Against scripting peers, Objeck is decisively faster.** On JIT-friendly numeric and integer loops it leads Python and Ruby by 3–30x.
2. **Against compiled-class JITs (Java, LuaJIT), Objeck trails on float loops but is competitive — and ahead — on the right integer workload** (fannkuchredux beats LuaJIT).
3. **Allocation throughput is the #1 gap.** binarytrees is the one benchmark where Objeck loses to everything; escape analysis / faster young-gen allocation is the highest-leverage future work.
4. **`DYN_MTHD_CALL` auto-JIT closed the coverage gap; warmup is the remaining lever.** Closure/function-reference calls now auto-JIT, so spectralnorm at `OBJECK_JIT_THRESHOLD=1` reaches ~0.46s (n=2000), near hand-`native`, and 3.3s at 5500 (~14x vs the default-threshold run). At the default threshold a single short run still pays warmup — the open question is auto-JIT threshold tuning, not coverage.
5. **Measurement environment matters.** Objeck's interpreter is more sensitive to virtualization than its JIT; native (non-Docker) numbers would be meaningfully better than the Docker numbers shown here.

---

## The `native` Keyword

Methods marked `native` are JIT-compiled to x64 or ARM64 machine code. All other methods run in the interpreter unless auto-JIT compiles them after 10 calls.

| spectralnorm | Input | Time | Notes |
|-------------|-------|------|-------|
| Auto-JIT, default threshold | 5500 | 44.86s | single short run — JIT warmup dominates |
| Auto-JIT, `OBJECK_JIT_THRESHOLD=1` | 5500 | **3.3s** | ~14x; closures auto-JIT (`DYN_MTHD_CALL`) |
| Auto-JIT `THRESHOLD=1` / `native` | 2000 | **0.46 / 0.37s** | auto-JIT approaches hand-`native` |

Methods marked `native` that contain `MTHD_CALL` are JIT-compiled via `ProcessStackCallback`. Auto-JIT also compiles hot methods automatically (default: after 10 calls). Closure/function-reference calls (`DYN_MTHD_CALL`) are **now auto-JIT'd** as well, so closure-heavy kernels like spectralnorm reach native-level speed once compiled — the lever is no longer coverage but the auto-JIT threshold (warmup), tunable via `OBJECK_JIT_THRESHOLD` (a single short run at the default threshold doesn't amortize compilation).

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
| v2026.6.2+ | Jun 2026 | ARM64 forced-JIT correctness hardening (PR #548, #551) | Correctness, not speed — clears every ARM64 JIT miscompile that forced JIT exposes, **unblocking a lower auto-JIT threshold** (the auto-JIT trigger is still 10 calls; these fixes let it drop without miscompiling cold helpers). Full ARM64 suite green at `OBJECK_JIT_THRESHOLD=1`. |

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

### v2026.6.2+ Detail (ARM64 forced-JIT correctness)

**Headline: no benchmark moves, but every ARM64 JIT miscompile that forced JIT (`OBJECK_JIT_THRESHOLD=1`) exposed is now fixed — a prerequisite for lowering the auto-JIT threshold below 10.** These were latent because cold helper methods rarely reach the trigger, so they ran interpreted; forcing JIT surfaced them. Found with the JIT's `_DEBUG_JIT_JIT` codegen disassembly and lldb.

| Fix | Category | Bug |
|-----|----------|-----|
| Cached-local float operands in transcendental/round ops (PR #548) | JIT (ARM64) | `ProcessFloatOperation`/`Operation2`/`Round`/`SquareRoot` read a `REG_FLOAT` operand's register bookkeeping as a stack slot → garbage (e.g. `atan` got 0). |
| libc float result dropped when holder ≠ D0 (PR #548) | JIT (ARM64) | The result move used `move_freg_freg`'s GP-bridge (swapped FMOV opcodes), dropping an FP-only `pow`/`sin`/`exp` result. Now a true `fmov Dd,Dn`. |
| Working-stack regs clobbered across inlined float libc calls (PR #548) | JIT (ARM64) | An inlined `Float->Pow`'s `blr` clobbers caller-saved temps; pending working-stack ints are now spilled across the call (`Sum5`/`Int->Pow`). |
| Unary float-op argument dropped when not in D0 (PR #551) | JIT (ARM64) | Same GP-bridge bug on the *argument* load — `exp` inside `1/(1+exp(x))` got a stale arg, diverging LogisticRegression (`ml_phase1`). |
| `imm19` not masked in error-handler branch backpatch (PR #551) | JIT (ARM64) | A backward (negative) div-by-zero / bounds / null-deref branch sign-extended over the `b.cond` opcode → illegal instruction (`ml_gbt` SIGILL). Now masked to `& 0x7FFFF`. |
| Deferred local load stale after an overwriting store (PR #551) | JIT (ARM64) | A TCO'd `return Gcd(b, a%b)` stored `b:=a%b` before the deferred `LOAD b` was consumed, so `a:=a%b` (GCD→0). `ProcessStore` now materializes pending refs to the slot first. |
| `Int->MinSize()` returned `INT64_MAX` (PR #551) | Library | `2->Pow(63)` computes `+2^63` in float and saturates on F2I; fixed to `1 << 63`. (Not a JIT bug — failed interpreted too; was masked by exit-code-based tests that printed `FAIL` without `Runtime->Exit(1)`.) |

---

## What We Tried and Reverted

| Optimization | Result | Why |
|-------------|--------|-----|
| Extended strength reduction (x*3,5,7,9,15) | 0.66x slower | Modern CPUs execute MUL in 3 cycles; shift+add has more dispatch overhead |
| Copy propagation | 0.85x slower | Changed patterns the JIT register allocator expected |
| GC lock-free mark via snapshot | 0.64x slower | Copying entire `allocated_memory` set before each mark phase was O(n) |
| Inline limit 512 | 0.91x slower | Exceeded JIT register allocator capacity |
| Auto-JIT MTHD_CALL via interpreter trampoline | 0.5x slower | Per-call trampoline overhead exceeded interpreter cost. Fixed by adding direct JIT-to-JIT calling. |
| ProcessInlineMethod for MTHD_CALL | Miscompiles broadly | Wiring `ProcessInlineMethod` into the MTHD_CALL path fails 36 regression tests. Constructors lose the implicit new-instance result (popped into INSTANCE_MEM, never pushed back) — excluding them clears 24. But the rest persist **even when restricted to pure, instance-memory-free leaf callees**: the inliner's register/working-stack handling miscompiles small methods inlined into high-register-pressure callers (ML/network/collection code). The shared register allocator and working stack don't compose correctly across the inline boundary. Needs a rewrite of `ProcessInlineMethod`, not an incremental fix; left disabled (dead code). Direct JIT-to-JIT calling already removes most call overhead, so the payoff is small. |
| Monomorphic per-call-site dispatch cache | Net regression | Caching `(call-site, receiver-class) → method` on each `StackInstr` to skip `ResolveVirtualMethod`'s map lookup. Measured: monomorphic virtual 3.35s→3.53s, non-virtual 2.75s→2.83s, bimorphic flat — all worse or equal. The extra `StackInstr` fields (+50% size) cost more in dcache pressure than the saved lookup, and `virtual_methods` is already a hashed `unordered_map`. Reverted. |

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
| **Lower the auto-JIT threshold** (currently 10 calls) | VM | MED — now *unblocked* on ARM64: the forced-JIT (`THRESHOLD=1`) correctness fixes above mean cold helpers JIT correctly, so the trigger can drop to compile warm code sooner. Measure the compile-time-vs-runtime trade-off before changing the default; re-validate x64 at the lower threshold first |

### P3 — Measurement & methodology

| Opportunity | Impact |
|-------------|--------|
| Stand up a **native (non-Docker) cross-language harness** so peer comparisons aren't muddied by the ~30–50% Docker interpreter overhead documented above | gives bare-metal interpreter numbers; the current page is entirely Docker |
| Add **Java + LuaJIT to the gating perf CI** so regressions like the safepoint one are caught automatically | the fannkuch regression shipped unnoticed because nothing compared releases head-to-head |

---

*Last updated: June 18, 2026 -- benchmark tables are the unified Docker run on an AMD Ryzen 9 7950X3D (32 vCPU / 62 GB) with the JIT GC-safepoint fixes merged (PR #539, Objeck v2026.6.2): fannkuchredux dropped 59.4s → 30.7s. Numbers are Docker (interpreter-bound rows ~30–50% slower than a native build); a native re-run is future work. P0 safepoint roadmap: complete on both arches — AMD64 (all three steps) and ARM64 (back-edge + X19 register-cache, validated on Apple Silicon). Jun 18: a chain of ARM64 forced-JIT correctness fixes merged (PR #548, #551) — the full ARM64 suite is now green at `OBJECK_JIT_THRESHOLD=1`, unblocking a future auto-JIT threshold reduction (see Optimization History).*
