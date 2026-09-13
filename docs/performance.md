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
| **Compiler** | Objeck **v2026.9.1** (`f6b3b09046`), built from source with the standard libraries rebuilt by that same compiler, `-opt s3` |
| **Methodology** | 3 runs per benchmark, median reported |
| **Date** | **2026-09-12** (unified Docker run) |
| **Previous run** | 2026-06-21 against v2026.6.2 — see Optimization History for what changed between them |

> All tables below come from a **single unified Docker run** (`perf-results/docker/Dockerfile`) on the box above, so the single-language and cross-language numbers are directly comparable. Note Docker's virtualized backend runs Objeck's **interpreter**-bound benchmarks ~30–50% slower than a native (bare-metal/WSL2) build, while JIT/`native` code is nearly unaffected — so the interpreter rows here are conservative.

### CLBG Benchmarks

Classic [Computer Language Benchmarks Game](https://benchmarksgame-team.pages.debian.net/benchmarksgame/) programs compiled with `-opt s3`.

| Benchmark | Input | Time (s) | Peak RSS |
|-----------|-------|----------|----------|
| **mandelbrot** | 4000 | 0.72 | 13 MB |
| **nbody** | 50M | 8.14 | 9 MB |
| **binarytrees** | 17 | 2.14 | 209 MB |
| **fannkuchredux** | 12 | 32.76 | 9 MB |
| **spectralnorm** | 5500 | 2.72 | 10 MB |
| **fasta** | 25M | 7.07 | 9 MB |


> Measured: **v2026.9.1** (`f6b3b09046`), 2026-09-12 unified Docker run on the host above.

**mandelbrot** and **nbody** benefit from `native`-annotated methods that JIT-compile to x64. **binarytrees** benefits from the young-gen bump allocator and auto-JIT for methods containing `MTHD_CALL`. **mandelbrot** is the noisiest row here: two runs on the same build the same afternoon gave 1.00s and 0.72s, so read it as unchanged from June's 0.87s rather than as movement in either direction.

**fasta appears for the first time, at 7.07s.** It had been skipped by the harness in every previous run, June included, and silently: two defects hid it. Four float literals carried a `d` suffix, which Objeck has never accepted, so it failed to compile — and the harness discarded the compiler's stderr, reporting only `SKIP`. Fixing that exposed the second, a VM bug where `Console->WriteBuffer(Char[])` encoded its output twice and corrupted every later write. Both are fixed in v2026.9.1, so there is no June figure to compare against.

**spectralnorm fell from 44.86s to 2.72s (16.5x).** The June page argued its ~45s was a JIT-warmup artifact and that the lever was threshold tuning; that argument is now gone rather than reduced. The v2026.9.1 calling convention (F7) made a closure call cost about what a bound call does, and the default threshold now returns **2.72s** — faster than the **3.3s** June reached by forcing compilation with `OBJECK_JIT_THRESHOLD=1`. There is no warmup gap left to tune.

**binarytrees fell from 8.53s to 2.14s (4.0x)** and is no longer the weak spot the June page described: it now beats CPython, Ruby and LuaJIT on time. Its **209 MB peak RSS** remains an order of magnitude above every other row here, so allocation throughput is still the standing gap — on memory, not on time.

**fannkuchredux did not move** (32.32s → 32.76s), which is worth stating plainly: it is call-bound, so the calling-convention work was expected to help and did not. Its cost is dominated by the permutation/flip inner loop rather than by call overhead.

---

## Cross-Language Comparison

All five languages measured in **one Docker run on identical inputs** (`perf-results/docker/Dockerfile`), so these times are directly comparable to each other. Median of 3 runs.

| | |
|---|---|
| **Objeck** | **v2026.9.1** (`f6b3b09046`), libraries rebuilt by the same compiler, `-opt s3` |
| **Python** | 3.12.3 (CPython) |
| **Ruby** | 3.2.3 |
| **LuaJIT** | 2.1 (tracing JIT) |
| **Java** | OpenJDK 21.0.12 (HotSpot, tiered JIT) |
| **Host** | AMD Ryzen 9 7950X3D (16C/32T), Ubuntu 24.04.4 in Docker (32 vCPU / 62 GB) |

> Same unified Docker run as the single-language tables above, so every number on this page is directly comparable. As noted, Objeck's *interpreter*-bound benchmarks (e.g. spectralnorm) would run ~30–50% faster on a native build; JIT/`native` code is essentially unaffected by Docker.

| Benchmark | Input | Objeck | Python 3.12 | Ruby 3.2 | LuaJIT 2.1 | Java 21 | Best |
|-----------|-------|--------|-------------|----------|------------|---------|------|
| **nbody** | 50M | 8.14s | 134.93s | 220.16s | 4.37s | **2.35s** | Java |
| **binarytrees** | 17 | 2.14s | 3.48s | 3.70s | 3.57s | **0.26s** | Java |
| **spectralnorm** | 5500 | 2.72s | 127.14s | 90.15s | **1.12s** | 1.18s | LuaJIT |
| **fannkuchredux** | 12 | **32.76s** | 430.56s | 1201.74s | 117.99s | **20.70s** | Java |

### Reading the table

- **Java (HotSpot) still sets the ceiling — it wins 3 of 4** (nbody, binarytrees, fannkuchredux); LuaJIT takes spectralnorm (1.12s vs Java's 1.18s). A mature tiered JIT with escape analysis and a generational GC is the bar a younger JIT is measured against.
- **Objeck beats Python and Ruby on all four**, by far wider margins than in June: nbody **16.6x** / **27.0x**, spectralnorm **46.7x** / **33.1x**, fannkuchredux **13.1x** / **36.7x**, binarytrees **1.6x** / **1.7x**.
- **Objeck beats LuaJIT on fannkuchredux** (32.76s vs 117.99s, **3.6x faster**) — the integer-array permutation/flip pattern is a poor fit for LuaJIT's tracing JIT and suits a method JIT well. June measured 3.8x; all three runs agree.
- **binarytrees is no longer Objeck's weak spot on time** — it now leads CPython, Ruby and LuaJIT. Java is still **8.2x** faster (0.26s vs 2.14s), and Objeck's **209 MB** peak RSS against everyone else's single digits is where the remaining gap lives. Allocation throughput stays the clearest improvement target (P1), now argued on memory rather than wall clock.
- **spectralnorm's warmup story is over.** June's 44.86s at the default threshold is now **2.72s** — better than the 3.3s June reached by forcing compilation. The v2026.9.1 calling convention removed the warmup gap rather than narrowing it, so there is no threshold left to tune here.
- **fannkuchredux did not move** (32.32s → 32.76s) despite being call-bound. Its inner loop, not call overhead, is what costs.

### Key Takeaways

1. **Against scripting peers, Objeck is decisively faster** — and by much wider margins than in June. nbody is **16.6x** faster than CPython and **27.0x** faster than Ruby; spectralnorm **46.7x** and **33.1x**; fannkuchredux **13.1x** and **36.7x**.
2. **Objeck beats LuaJIT on fannkuchredux** (32.76s vs 117.99s, **3.6x faster**) — the integer-array permutation/flip pattern suits a method JIT better than a tracing one. Java still leads it by 1.6x.
3. **binarytrees is no longer the weak spot on time.** It now beats CPython (1.6x), Ruby (1.7x) and LuaJIT (1.7x). Java remains 8.2x faster, and Objeck's **209 MB** peak RSS against Java's generational GC is where the gap now lives — allocation throughput is still the highest-leverage work, but the case rests on memory rather than wall clock.
4. **Closure calls are no longer a category of weakness.** The F7 calling convention took spectralnorm from 44.86s to 2.72s at the *default* threshold, beating what June achieved by forcing compilation. The warmup lever the June page described no longer exists.
5. **Float loops remain the gap against compiled-class JITs.** nbody trails LuaJIT by 1.9x and Java by 3.5x; spectralnorm trails both by ~2.4x.
6. **Measurement environment matters.** Objeck's interpreter is more sensitive to virtualization than its JIT; native (non-Docker) numbers would be meaningfully better than the Docker numbers shown here.

---

## The `native` Keyword

Methods marked `native` are JIT-compiled to x64 or ARM64 machine code. All other methods run in the interpreter unless auto-JIT compiles them after 10 calls.

> Measured: **v2026.9.1** (`f6b3b09046`), 2026-09-12 unified Docker run.

| spectralnorm | Input | Time | Notes |
|-------------|-------|------|-------|
| Auto-JIT, default threshold | 5500 | **2.72s** | v2026.6.2 took 44.86s here |
| `bench_spectralnorm_native` (hand-`native`) | 2000 | 0.38s | unchanged from June's 0.37s, as expected |

Methods marked `native` that contain `MTHD_CALL` are JIT-compiled via `ProcessStackCallback`. Auto-JIT also compiles hot methods automatically (default: after 10 calls). Closure/function-reference calls (`DYN_MTHD_CALL`) are auto-JIT'd as well, and since v2026.9.1 a compiled caller enters a compiled callee's native entry directly, with an inline cache at `virtual` and func-ref sites. A closure-heavy kernel therefore reaches native-level speed at the **default** threshold: spectralnorm no longer needs `OBJECK_JIT_THRESHOLD=1` to be fast, and the hand-`native` kernel is no longer meaningfully ahead of it.

---

## Micro-Benchmarks

Targeted benchmarks for specific optimization patterns (`programs/tests/perf/`). Compiled with `-opt s3`, median of 3 runs.

> Measured: **v2026.9.1** (`f6b3b09046`), 2026-09-12 unified Docker run. The June column is the 2026-06-21 run against v2026.6.2.

| Benchmark | Target | June (s) | Now (s) | Change | Peak RSS |
|-----------|--------|---------:|--------:|--------|----------|
| `bench_strength_ext` | Non-power-of-2 multiply patterns | 2.14 | **0.04** | 54x | 9 MB |
| `bench_dead_code` | Unreachable assignments | 1.68 | **0.04** | 42x | 11 MB |
| `bench_copy_prop` | Variable copy chains | 1.19 | **0.03** | 40x | 9 MB |
| `bench_array_intensive` | Sequential array access patterns | 2.40 | **0.08** | 30x | 9 MB |
| `bench_cse` | Common subexpression elimination | 0.25 | **0.01** | 25x | 9 MB |
| `bench_loop_invariant` | Loop-invariant expressions (LICM) | 0.24 | **0.01** | 24x | 11 MB |
| `bench_matrix_multiply` | Nested loop float computation (n=500) | 4.82 | **0.28** | 17x | 15 MB |
| `bench_method_dispatch` | Repeated method calls on objects | 2.96 | **0.29** | 10x | 9 MB |
| `bench_gc_churn` | Rapid short-lived object allocation | 0.64 | **0.15** | 4.3x | 137 MB |
| `bench_gc_large_heap` | Large live set, GC sweep time | 0.07 | **0.02** | 3.5x | 58 MB |
| `bench_tco` | Tail-recursive accumulator (TCO, n=1M×200) | 0.33 | **0.11** | 3.0x | 9 MB |
| `bench_spectralnorm_native` | Float arrays with `native` JIT (n=2000) | 0.37 | 0.38 | unchanged | 9 MB |

The two fastest rows (`bench_cse`, `bench_loop_invariant`) now sit at 0.01 s,
close enough to the harness's resolution that their ratios should be read as
"too fast to distinguish" rather than as precise multiples.
`bench_spectralnorm_native` is the control: it is hand-`native`, so it was
already compiled before this work and comes back unchanged at 0.38 s — which is
what makes the other rows credible.

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
| v2026.8.2 | Aug 2026 | Indexed call results (`GetItems()[0]->Name()`); `obu` updater packaged | Language/tooling, not throughput |
| v2026.8.4 | Aug 2026 | Multithreaded GC fixes; UTF-8 locale correctness | Correctness |
| v2026.9.0 | Sep 2026 | Loopback teardown (`CloseGracefully`), per-thread LSP analysis | Correctness/concurrency, not throughput |
| **v2026.9.1** | **Sep 2026** | **F7 calling convention — a compiled caller builds the callee's frame and enters its native entry directly, with inline caches for `virtual` and func-ref sites; dense `select` → jump table on both backends; `a->Size()` inlined, constant division by multiply, one-instruction array addressing, eight allocatable registers on AMD64** | **bound call 26.5 ns → 5.5 ns; `virtual` call 125 ns → 6.0 ns; `Fib(32)` 0.208 s → 0.043 s (AMD64). M4 Max: call 17.1 ns → 7.4 ns, `Fib(32)` 0.135 s → 0.050 s. Loops 2–8x (array-summing loop 8x, constant division 2.5x)** |

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

### v2026.9.1 Detail (the calling convention)

**Headline: a call between compiled methods no longer crosses the C++ bridge.**
Before this, a JIT'd method calling another JIT'd method went out through an
eleven-argument bridge entry, took a pooled frame and released it. The caller now
builds the callee's frame on its own stack and jumps straight to its native entry.

| Measurement | Before | After |
|---|---|---|
| bound call (AMD64) | 26.5 ns | **5.5 ns** |
| `virtual` call (AMD64) | 125 ns | **6.0 ns** |
| `Fib(32)` (AMD64) | 0.208 s | **0.043 s** |
| call (Apple M4 Max) | 17.1 ns | **7.4 ns** |
| `Fib(32)` (Apple M4 Max) | 0.135 s | **0.050 s** |

`virtual` and func-ref sites carry inline caches keyed on the receiver's class word
(or the func-ref word); a miss falls back to the resolver, and the slow path also
serves a callee not yet compiled, a full call stack and a `Nil` receiver. A dense
integer `select` compiles to a jump table on both backends.

**What this meant for the tables above, and what re-measuring found.** The
2026-09-12 run settled it. `spectralnorm` fell **16.5x** (44.86s to 2.72s) and its
"JIT warmup dominates" narrative is gone: the default threshold now beats what
forcing compilation achieved in June. `binarytrees` fell **4.0x** (8.53s to 2.14s)
and now leads all three scripting/JIT peers on time, leaving its 209 MB peak RSS
as the real gap. `fannkuchredux`, despite being call-bound, did **not** move
(32.32s to 32.76s) -- its permutation/flip inner loop dominates, not call
overhead. The hand-`native` spectralnorm kernel was already compiled and comes
back unchanged (0.37s to 0.38s), which is the run's own sanity check.

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
| ~~**DYN_MTHD_CALL auto-JIT** — closure / function-ref calls~~ | JIT | **DONE** (v2026.6.2). Superseded by v2026.9.1's calling convention, which gives func-ref sites an inline cache and a direct native entry |
| **Float JIT codegen + float fast-path opcodes** (`LOAD_FLOAT_LIT`, `ADD/SUB/MUL_FLOAT`) | JIT/VM | **HIGH** — Java and LuaJIT lead specifically on float loops (nbody, spectralnorm); the inline switch is integer-only today |
| **ProcessInlineMethod for MTHD_CALL** — getter/ctor inlining inside JIT'd methods | JIT | **LOW now** — the trampoline it existed to remove is gone (v2026.9.1); a direct native-entry call is 5.5 ns, so inlining buys far less than when this was written. Still blocked by INSTANCE_MEM offset corruption in constructors |
| **Per-call-site monomorphic virtual dispatch cache** | VM | LOW — store the last (class→method) pointer in the instruction to skip the hash lookup in the common case |
| **Lower the auto-JIT threshold** (currently 10 calls) | VM | MED — now *unblocked* on ARM64: the forced-JIT (`THRESHOLD=1`) correctness fixes above mean cold helpers JIT correctly, so the trigger can drop to compile warm code sooner. Measure the compile-time-vs-runtime trade-off before changing the default; re-validate x64 at the lower threshold first |

### P3 — Measurement & methodology

| Opportunity | Impact |
|-------------|--------|
| ~~**Re-run every table on v2026.9.1**~~ | **DONE** (2026-09-12, `f6b3b09046`, same 7950X3D host). spectralnorm 16.5x, binarytrees 4.0x, nbody 2.3x, micro-benchmarks up to 54x; fannkuchredux flat and mandelbrot noise-dominated. `fasta` measured for the first time |
| Stand up a **native (non-Docker) cross-language harness** so peer comparisons aren't muddied by the ~30–50% Docker interpreter overhead documented above | gives bare-metal interpreter numbers; the current page is entirely Docker |
| ~~**Add `fasta` to the measured set**~~ | **DONE** — both causes fixed in v2026.9.1 (four `d`-suffixed float literals, and `Console->WriteBuffer(Char[])` encoding its output twice), and it is measured above at **7.07s**. It had been skipped silently since before June because the harness discarded the compiler's stderr |
| **A second cross-language table on current language releases** — the run above uses Ubuntu 24.04's Python 3.12.3, Ruby 3.2.3 (pre-YJIT-by-default) and OpenJDK 21 | must be *additive*: swapping runtimes breaks comparison with this page's own history, so it belongs beside the like-for-like table, not instead of it |
| Add **Java + LuaJIT to the gating perf CI** so regressions like the safepoint one are caught automatically | the fannkuch regression shipped unnoticed because nothing compared releases head-to-head |
| ~~Record the **Objeck version beside every table**~~ | **DONE** — every measured table now carries the version and the commit it was taken at. This page went four releases without its numbers being re-measured and nothing in the tables said so; a stamp per table is what makes that visible next time |

---

*Prose and measured tables last updated: **September 12, 2026, 22:22 UTC** — Objeck **v2026.9.1** (`f6b3b09046`), a unified Docker run on an AMD Ryzen 9 7950X3D (32 vCPU / 62 GB visible to Docker; the host has 128 GB). Median of 3 runs. Numbers are Docker: interpreter-bound rows run ~30–50% slower than a native build, while JIT/`native` code is largely unaffected.*

*The previous measured run was June 21, 2026 against v2026.6.2 and is retained only as the "June" column in the micro-benchmark table and as the before-figures quoted in the prose. Headline movement between the two: spectralnorm **16.5x**, binarytrees **4.0x**, nbody **2.3x**, micro-benchmarks up to **54x**; fannkuchredux flat, and mandelbrot noise-dominated rather than moved.*

*All six CLBG benchmarks are measured here; `fasta` joins the set for the first time, having been skipped silently in every previous run.*

*The measured commit is not the release tag, but it is the same engine. `f6b3b09046` precedes `v2026.9.1` (`ca5a3e19b9`) by five commits, and `git diff` between them over `core/vm`, `core/compiler`, `core/shared` and the committed `.obl` set is **empty** — the difference is documentation, examples and release tooling only. So these numbers describe the engine that shipped.*

*P0 safepoint roadmap: complete on both arches — AMD64 (all three steps) and ARM64 (back-edge + X19 register-cache, validated on Apple Silicon). The ARM64 forced-JIT correctness chain (PR #548, #551) is merged and the full ARM64 suite is green at `OBJECK_JIT_THRESHOLD=1`; as of v2026.9.1 every CI leg runs that pass, ARM64 included.*
