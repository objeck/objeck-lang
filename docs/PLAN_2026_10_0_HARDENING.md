# v2026.10.0 hardening plan: compiler, GC, language

Status: **revision 2** (2026-09-15), after six independent reviews of revision 1 and the v2026.9.4 hotfix. Base: master `7a658f2407` (v2026.9.4).

Goal: raise the three weakest areas of the quality review. They are compiler correctness (B-), garbage collector and concurrency (B-), and language rough edges (B). Ecosystem and sustainability are a separate track. The maintainer's direction: solid and tested, not fast and buggy.

Maintainer decisions already taken (D1-D8, all as recommended):
- **D1 shifts:** counts masked to 0-63.
- **D2 division:** `INT64_MIN / -1 = INT64_MIN` and `% -1 = 0`, no trap.
- **D3 overflow:** documented wrapping.
- **D4 enums:** interpolation prints the integer value.
- **D5 lambdas:** `@field`/`self` inside a lambda deferred to the next release.
- **D6 interpolation:** a lambda inside `{$ }` gets a clear error.
- **D7 version:** bump first.
- **D8 CI:** a nightly workflow.

---

## 1. What changed from revision 1, and why

| Revision 1 said | Revision 2 says | Evidence |
|---|---|---|
| Phases 2, 3 and 4 run in parallel | **Two serialized lanes** (compiler, GC/VM). At most **one risky merge per nightly window** | `context.cpp` (10.7k lines, 28 commits since June) and `memory.cpp` (29 commits) are touched by most items. G2 and G3 rewrite the same allocation fast paths. A red nightly must point at one change |
| Gate on "7 clean nightlies" | **Gate on work done** | Rule of three: 0 failures in 7 nights bounds the nightly rate only below ~43%. At CI's ~5% flake rate, P(7 green in a row) is only 70% |
| "200 runs on the Mac and Surface" | **200 runs per fixture on hosted `windows-11-arm`, `ubuntu-24.04-arm`, `macos-15`**. The M4 Max is corroboration | The Surface was offline for most of the 9.3 gate. Hosted arm64 is where #816 reproduced |
| Build the fuzzer, then settle integer semantics | **Integer semantics first**, because generated programs cannot avoid overflow without distorting what they test | Csmith and YARPGen: the UB-avoidance wrappers themselves hide bugs |
| Consts members become `Int` (L1) | **Fix the string path instead.** Enum and consts types act as `Int` in interpolation and concat | Library APIs take consts-typed parameters (`sdl2.obs:7174 CopyEx(..., flip : RendererFlip)`); retyping would break them |
| Removing `* 2` saves ~30% | **~4% peak RSS.** The saving is fewer minor GCs. The memory win is the nursery size | `* 2` dates from the 2010 import's 4-byte-int VM, stale since `5c5d7b2a8e` (May 2022). The nursery fills to 128 MB regardless of object size |
| Adaptive nursery grows when survival is low | **`--nursery` flag + a size sweep; the default comes from data.** Adaptive growth only on *high* survival, next release | binarytrees has 1.4% survival, so the rev-1 rule would grow straight back to 128 MB |
| 5% perf budget, median of 5 | **Per-platform baselines, ≥15 interleaved runs, pinned CCD, Mann-Whitney p<0.01**. Noisy rows advisory | mandelbrot: 17% run-to-run range |
| Bump first, no maintenance path | **Bump first and cut `release/2026.9.x` from `v2026.9.4`** | A 9.x hotfix (as 9.4 was) must stay possible |
| (not in revision 1) | **`.obl` protocol** (section 4) | Windows legs test against committed `.obl`. With a fixed version number, a stale `.obl` never errors, it is just wrong |

## 2. Found since revision 1 (all verified)

Shipped in v2026.9.4:
- s2/s3 division miscompiles (strength reduction; peephole pattern 5)
- `obc` crash folding `INT64_MIN / -1`
- deserializer Nil-element corruption

Still open, and scheduled below:

| # | Defect | Status |
|---|---|---|
| C1 | `obc` crashes (0xC0000005) on **every bare lambda with parameters**: `v->Filter(\(x) => ...)`, `Map`, typed `\(Int) ~ Int : (a) =>`. Cause: `ParseLambda` reads scanner token index ≥ `LOOK_AHEAD` (3) → nullptr (`parser.cpp:1366,1376`). In every release since v2026.6.3 (`923d217015`) | root-caused, 13-line fix validated on a scratch build |
| C2 | Lambda nested in a lambda crashes `obc`: `BuildLambdaFunction` nulls the enclosing lambda's capture state instead of restoring it (`context.cpp:1406-1431` → null deref `1100`) | same patch |
| C3 | A captured program-class object cannot have methods called in a lambda (`InvalidStatic`, `context.cpp:7946-7966`) | root cause known |
| C4 | Enum and consts values fail in `"{$x}"` and in `+` concat (`context.cpp:8690, 8767, 8878`); `e->As(Int)->PrintLine()` chained fails "Cannot cast a Nil return value" | root cause known |
| C5 | Bare-lambda wrap unsupported in ternaries and multi-argument calls, with misleading errors | reproduced |
| C6 | Calling a captured FuncRef inside a lambda: "Undefined function/method call" | reproduced |
| S1 | `>>>` saturates (returns 0 for counts ≥64 or <0), contradicting D1; `unsigned_ops.obs:51-53` asserts it | measured |
| S2 | ARM64 JIT shift immediates use `abs(value)` unmasked: `x << -1` becomes `x << 1`; `>> 128` corrupts the opcode (`jit_arm_a64.cpp` `shl_imm_reg`/`shr_imm_reg`) | code reading |
| S3 | x64 `MIN / -1` at run time dies with 0xC0000095 and no message (interpreter and JIT); both JIT constant folders do the division in C++ | measured |
| S4 | Divide-by-zero text differs between interpreter and JIT; `vm_error_exit` exits -1 interpreted, 1 under `--jit=1` | measured |
| G11 | A closure capturing an **enum array** is corrupted under allocation pressure (bad=6-16 per run, JIT and interpreter); the `INT_PARM` vs `INT_ARY_PARM` typo at `intermediate.cpp:945, 6934` is part of it | reproduced, not fully root-caused |
| G12 | **Real bug.** Minor GC skips closure capture blocks: the capture store's barrier dirties the `BYTE_ARY_TYPE` block, `ScanDirtyObject` handles only `NIL_TYPE`/`INT_TYPE` (`memory.cpp:1942, 1993`), and a holder's `FUNC_PARM` case descends only when the closure is young (`1950-1956`), which it never is. A capturing closure stored in an old object loses its young captures at the next minor GC | confirmed by the heap-verifier review; fixture to write |
| G13 | **Real bug.** `CPY_CHAR_STR_ARYS` (`common.cpp:3018-3021`) stores young Strings into an old array with no barrier; the array's only root is the op stack, which a minor GC marks but does not trace into (`memory.cpp:2438-2441`) | confirmed by the heap-verifier review; the first probe stored the array to a local before a GC could run |
| G14 | Native libraries can store a young object they received as an argument into an array or object with no barrier; `force_old` covers only objects they allocate (`memory.h:513-528`) | audit needed |
| G15 | Promotion can throw `bad_alloc` from `old_generation.insert` / `promoted_objects.push_back` inside stop-the-world while holding both GC locks, leaving other threads parked and printing "virtual machine: out of memory" (the #816 message shape) | code reading |
| T1 | Negative tests pass on the wrong error: `bad_undefined_var` gets "Invalid operation '+'", three literal tests get a generic "unexpected token"; markers match anywhere in the file (#729's shape) | measured |

---

## 3. Phases

```
Phase 0  bump 2026.10.0 · release/2026.9.x branch · baselines · milestone
   |
Phase 1  foundations: integer semantics · differential regression · negative-test messages
   |      heap verifier · runner VM-args · nightly (dispatch first)
   |
   +--> compiler lane  (serialized)            GC/VM lane  (serialized)
   |    C1+C2 → C4 → S4 → C3 → C5/C6 → L6       stats → hardening → G11-G13 → *2 → nursery sweep
   |    fuzzer (swarm, JIT coverage, reducer)   GC fuzz mode (threads)
   |
Phase 5  release gates → tag → sign → publish
```

### Phase 0: bump and baselines

- [ ] **0.1** `bump-version` to 2026.10.0; create `release/2026.9.x` from `v2026.9.4` with a cherry-pick-only policy.
- [ ] **0.2** Create a `v2026.10.0` GitHub milestone with one issue per item (C1-C6, S1-S4, G2-G13, T1, the fuzzer, the verifier, the nightly). This doc links to the issues; progress lives there, not in checkboxes edited from parallel PRs.
- [ ] **0.3** Baselines, raw samples committed to `perf-results/baseline-2026.10.0/`:
  - **Where:** Docker Linux x64, Windows native x64, macOS arm64 (M4 Max).
  - **What:** CLBG times plus peak RSS (Linux `/usr/bin/time -v`; Windows `PeakWorkingSet64` read after exit), commit charge, and `runtime.gc.pause.max_us` / `promoted.total`.
  - **Separately:** a CI-measured perf-gate baseline, so perf-gate can fail on gross (≥15%) regressions.
- [ ] **0.4** Decide #820 (`FixupSelf`): fold it into the GC lane's hardening step, or close it.

**Exit:** bump merged with CI green on all five legs; the milestone exists; baselines committed.

### Phase 1: foundations

**1.1 Integer semantics (D1-D3), one PR.** Add one shared inline header under `core/shared` (wrap add/sub/mul, masked shl/sar/shr, wrapping div/mod) used by every site:
- the interpreter: the inline switch *and* the helpers (`interpreter.cpp:274-293, 372-383, 838-1064`);
- `obc`'s `CalculateIntFold`;
- `JitAmd64::ProcessIntFold` and `JitArm64::ProcessIntFold`;
- the ARM64 immediate shift encoders (S2);
- x64 `div_imm_reg`: `neg`/`xor` for a literal -1;
- x64 `div_reg_reg`/`div_mem_reg`: a slow path taken on divisor 0 or -1, which also unifies the S4 messages;
- `Int->ShiftRightUnsigned` (S1): mask it too. Update `unsigned_ops.obs` and the docs.

Spec text for `docs/FEATURES.md` "Integer arithmetic":
- **Width:** `Int` is 64-bit two's-complement; `Byte` and `Char` widen to `Int`.
- **Overflow:** `+ - *` wrap and never trap.
- **Shifts:** use `n and 63`. `>>` sign-fills, `>>>` zero-fills.
- **Division:** `/` truncates toward zero; `%` takes the dividend's sign.
- **Traps:** a zero divisor is the only integer trap; `MIN / -1 = MIN` and `MIN % -1 = 0`.
- **Consistency:** identical in the interpreter, both JITs and at every `-opt` level.

**Accept:** a measured table (literal and runtime operands, counts -1/64/128) matches in all four modes (s0/s3 × `--jit=off`/`--jit=1`) on x64 and arm64.

**1.2 Differential regression** (`programs/regression/run_differential.py`):
- **Configurations:** s0/off (reference), s3/off, s3/default, s3/jit1, plus s0/jit1 on rotation.
- **Oracle:** stdout plus zero/non-zero exit status (not the exact code, see S4). Parallel with `-j`; socket tests serial.
- **Markers, linted like `JIT_DISABLE`:**
  - `# DIFF_CONFIGS:` (e.g. `tco_receiver` needs s3);
  - `# DIFF_REQUIRES_JIT` (`jit_entry_compiled`, `jit_call_overhead`);
  - `# NONDETERMINISTIC_OUTPUT` with a `# reason:` line, capped at 5% of tests.
- **Timing tests** send timings to stderr so stdout stays comparable (8 tests today).
- **Measured cost:** 252 s wall for the full suite × 7 runs, 3 workers, 7950X3D.

**1.3 Negative tests name their error (T1).**
- `# EXPECT_COMPILE_ERROR: <substring>`, anchored to the start of a line, in `run_regression.sh`/`.cmd`, `gen_manifest.py` and `check_test_exit_codes.py`.
- Migrate the 19 existing tests; fix or accept each wrong-message case explicitly.

**1.4 Runner VM arguments and a nursery knob.**
- `OBJECK_VM_ARGS` in both runners, so the suite can run with `--gc-threshold=64k` (there is no environment variable for it today).
- `--nursery=<size>` / `OBJECK_NURSERY`, read before `Initialize`; the 128 MB region stays reserved. Needed now, not in the GC lane: at `--gc-threshold=64k` nursery-full collections go major (`memory.cpp:523`), so minor GCs barely run. Stress therefore runs in two modes: **minor stress** (for example `OBJECK_NURSERY=256k`, default threshold) and **major stress** (`--gc-threshold=64k`).
- Libraries stay at s3 (`lang.obl` at s2); the s0 differential covers test code only.

**1.5 Heap verifier** (`OBJECK_GC_VERIFY`, read once in `Initialize`). It runs inside stop-the-world, never from mark threads, and never while holding `allocated_lock` (the ABBA order at `memory.cpp:1032-1044`). It checks only what the GC can type: **never** op stacks, JIT temp windows or pending-thread roots, which legitimately hold stale words, and not `INT_TYPE` array elements (`Int[]` and object arrays share a type), which get warnings only.

Invariants:
- **A1 checkmark (before a minor GC, after the STW handshake, before `ScanDirtyObject`):** the young objects reachable by a full typed trace from the collector's roots, kept in a side set and not in `MARKED_FLAG`, must all be marked after the minor mark phase. One-directional; it needs no list of write paths and catches missing barriers, `ScanDirtyObject` gaps (G12) and STW gaps.
- **A2:** an old object whose typed field (`OBJ_PARM`, `FUNC_PARM` closure pointer, array field) holds a nursery pointer must have `GC_RSET_BIT`, and be in `dirty_list` unless it overflowed.
- **B1 headers (after any collection):** `TYPE` valid; a `NIL_TYPE` class pointer is a member of the set built from `GetClasses()`; word 0 **≥** the required size (reused cache blocks keep their own size); mark and rset bits clear, old bit set; array extents inside the block.
- **B2 typed edges:** each typed slot is 0 or an `old_generation` member of the matching `TYPE`, descending into closure captures through the holder's declarations. This is the check that catches #816 (`@right = 1`). After a major GC, scoped to reachable objects.
- **B3:** no typed reference (heap, class statics, typed frame slots, captures) points into the raw nursery range `[young_region, young_region + young_region_size)`. `IsYoung` cannot be used, because `young_offset` is 0 after collection.
- **B4:** typed frame slots are 0 or old-gen members (frames are zeroed at entry on the interpreter and both JITs).
- **C (inside fixup):** a young object start with a valid class that `ForwardedAddr` cannot forward is a missed root; abort.
- **Blind spot, documented:** an object reference kept in an `INT_PARM` field, and fixup rewriting an Int array element that happens to equal a promoted object's address (`memory.cpp:2069-2077`).

Cost tiers:
- **T0**, every collection when verify is on: A2 over `dirty_list`; B2/B3 over promoted objects, dirty objects, statics and typed roots. O(promoted + dirty + roots).
- **T1**, `OBJECK_GC_VERIFY=N`: the first 8 collections, then every Nth, the full B1-B4 walk. If verification exceeds ~20% of wall time, N doubles and says so.
- **T2**, `OBJECK_GC_VERIFY=checkmark`: A1, in stress fixtures and the GC fuzz mode only.

Proof it works, on the release binary:
- **Runtime fault injection** (`OBJECK_GC_VERIFY_INJECT=field|barrier|mark`, active only with verify on): OR 1 into a promoted object's field, skip one `WriteBarrier`, OR the mark bit into an interior nursery word. Each must abort on all five legs.
- **Replay G12**, the live closure bug: a deterministic x64 fixture with a small nursery. A1 fires before the fix and passes after it.
- **Replay #816:** on the verifier branch, reverse-apply only 5bf8bb286b's three `CheckObject` hunks, and run `map_insert_stress_816_mt` (branch `probe/816-p3`) with `OBJECK_JIT_THRESHOLD=1 --gc-threshold=64k` ~100 times on the ARM64 legs. B2 aborts before `Map:Find` crashes. ARM64-only; x64 was 0/100.
- **Replay a removed barrier** (`interpreter.cpp:533`): A1 and A2 fire. **Replay the #574 STW revert** (`0a12e591d`) under `core_thread_gc_stress`: A1 fires.

**1.6 Nightly workflow** (`nightly-hardening.yml`).
- `workflow_dispatch` first, to **measure** cost per leg. Then a schedule off the hour.
- Reuses the latest green master `ci-build` artifacts (saves 8-12 min per leg and tests the binaries CI tested).
- `actions/cache/restore` only, never save: the repo cache is already over its 10 GB cap.
- A `concurrency:` group and explicit `timeout-minutes`.
- **Triage into three classes:**
  - infra: comment only;
  - known: a `known.json` signature (leg + config + message regex), summary only;
  - new: its own issue with seed, reduced program and artifacts.
- The tracking issue closes itself on the next green run.

**Phase 1 exits, split so the lanes are not blocked on everything:**
- 1.1 + 1.2 + 1.3 unblock the **compiler lane**;
- 1.4 + 1.5 (with its #816 proof) unblock the **GC lane**;
- 1.6 is required before any lane's exit gate counts.

### Compiler lane (serialized; each item is one PR with a test that fails on v2026.9.4)

1. **C1 + C2** lambda crashes.
   - `LOOK_AHEAD` 16 plus a bounds check in `ParseLambda`; save and restore capture state in `BuildLambdaFunction`.
   - Tests: `Filter`/`Map` with `\(x) =>`, nested bare-in-bare and typed-in-typed lambdas, a typed `\(Int) ~ Int : (a) =>`, a capture after a nested lambda.
   - Measured: no compile-time cost (a 401-class source took 26.5 s vs 26.6 s).
2. **C4** enum/consts in interpolation and concat print the integer.
   - Tests: program and library enums and consts, `{$e}`, `+`, a format specifier, a library consts parameter call (`CopyEx(..., RendererFlip->...)`), and the chained `e->As(Int)->PrintLine()`.
3. **S4** remainder: one divide-by-zero message; `vm_error_exit` exit code consistent.
4. **C3** method calls on captured program objects.
   - `InvalidStatic` accepts `entry->IsClosureEntry()` (`context.cpp:7953, 7960`).
   - Tests: instance and virtual calls, mutation through the closure, a stored closure, the capture promoted by a minor GC under `--jit=1 --gc-threshold=64k` with the verifier on, capture-by-copy semantics, a loop variable.
   - `docs/FEATURES.md` documents capture-by-copy.
5. **C5 / C6** lambda contexts.
   - Ternary and multi-argument calls give a clear error suggesting `FuncRef->New(...)`, or get supported if small.
   - Calling a captured FuncRef is fixed, or documented.
6. **D6** the scanner emits "lambdas are not allowed inside string interpolation; assign it to a local first" (`scanner.cpp:789-822`).
7. **Fuzzer** (`tools/fuzz/`, Python standard library, typed AST):
   - **Features:** swarm-sampled per program from F1 ints/loops/arrays, F2 floats, F3 calls/virtual, F4 select/strings, F5 closures, F6 allocation graphs. The seed records the subset.
   - **JIT limits:** stays under them (~80 local slots, flat expressions ≤8 operands). Parses `OBJECK_JIT_REPORT` stderr, and fails the run if fewer than 90% of methods compiled under `--jit=1`.
   - **Opcode coverage:** `obc -asm` histogram versus each backend's JIT whitelist.
   - **Digests:** one Int digest line per function; each function called ≥10 times; no time or random calls.
   - **Reducer:** shrinks the generator's choice sequence, so every reduction stays a valid program. The interestingness test requires compiles at s0 and s3, a clean s0/off run, time ≤2× the original, and the same output partition.
   - **Dedup:** signature = output partition + first differing digest line (crashes: error text + top frame). Failures ordered furthest-point-first; per-pass disable knobs for bisection.
   - **EMI mode:** dead-code variants of regression tests, guarded on a value read from `args`.

**Compiler lane exit gate:**
- ≥30,000 generated programs × the configuration set per architecture, with **0 new s3-default miscompile signatures**, and no new signature in the last 50% of total fuzzing CPU time.
- ≥90% JIT-compiled fraction on every leg.
- A panel of 5 seeded mutants (folder, strength reduction, one per JIT backend, write barrier), each caught within the budget.
- LSP, debugger and DAP tests green (`libobjk_diags` embeds the compiler).

### GC/VM lane (serialized)

1. **Stats (G9).**
   - Add `runtime.memory.peak`, and `OBJECK_GC_STATS=1` for an exit summary: minor and major counts, pause percentiles *including* the stop-the-world handshake and the minor dirty-list scan, promoted bytes, peak RSS.
   - Reported real bytes stay **separate** from the GC trigger counters (switching triggers to real bytes would fire major GCs ~4× sooner).
2. **G12 / G13 barrier bugs, each with a fixture that fails first.**
   - G12: `ScanDirtyObject`'s `FUNC_PARM` case descends into the closure's declarations regardless of generation, mirroring `FixupMemory` (`memory.cpp:2025-2032`).
   - G13: `WriteBarrier(array)` after the `CPY_CHAR_STR_ARYS` loop.
   - G14: audit native-library stores; add a barrier callback in `VMContext` if any library stores a received young object.
3. **Hardening (G5, G6, G15).**
   - G5: check a conservative candidate's class pointer against the class-pointer set (shared with verifier B1) before dereferencing it (`memory.h:253`).
   - G6: a failed promotion `calloc` is a fatal out-of-memory error inside the loop. Not copying leaves every reference dangling once the nursery resets (`memory.cpp:1183`), so no fixup can recover it.
   - G15: reserve `old_generation` and `promoted_objects` capacity before the sweep, or catch `bad_alloc` inside `CollectMemory` and exit fatally, never unwinding out of stop-the-world.
   - #820 lands as separate hardening, not as the G6 fix: drop "Fixes #816" from its body, gate its trace on `IsYoungObjectStart` (after G5), and abort under verify when a young object start is left unforwarded (invariant C).
4. **G10** #816 reproducer as a regression test, labelled an ARM64 gate (x64 is 0/100), plus minor-GC variants using the nursery knob, and a 200-run loop in the nightly.
5. **G11** root-cause the enum-array capture corruption, including the `INT_PARM` typo at `intermediate.cpp:945, 6934`.
6. **G7** remove the dead `_GC_SERIAL` code (the macro is never defined anywhere), as a mechanical PR after the verifier lands. **G8** adaptive-heap counters consecutive, shrink floored at `MEM_START_MAX`, `arch/README.md` corrected.
7. **G2** exact object size, in all three sites in one commit (`memory.cpp:467`, `memory.h:257`, `jit_amd_lp64.cpp:1426`).
   - First a guard-word run: func-ref last field, Float last field, zero-field classes, library-class parents, closures, a serialization round trip with a trailing func-ref, native `lib_api` writes, `--jit=1`, old-gen objects.
   - If a guard trips and cannot be fixed safely, `* 2` stays and G2 moves to the next release.
8. **Nursery sizing (G3)**, using the Phase 1.4 knob.
   - Sweep 16/32/64/128 MB on the three baselines: peak RSS, time, minor/major counts, promotions, pause percentiles. Pick the default from the data (expected 32-64 MB).
   - Windows: reserve 128 MB, commit on growth. A failed commit stays small and collects, never crashes.
   - The limit changes only inside stop-the-world when `young_offset` resets.
   - `OBJECK_NURSERY=128m` stays available through the release as a kill switch.
9. **G4** promotion via `GetMemory` as a *throughput* item. Rewrite word[0] after the copy; take the cache lock once per batch. Not counted toward the memory gate.
10. **GC fuzz mode (threads):** per-thread allocation graphs and checksums, verifier on at T2.
- **Next release:** escape analysis; replacing `old_generation`'s `unordered_set` (~13-15 MB and an O(old-gen) walk per minor GC on binarytrees); adaptive nursery growth on high survival; `@field`/`self` in lambdas (D5).

**GC/VM lane exit gate:**
- Full regression under the verifier in **both** stress modes (minor: small nursery; major: `--gc-threshold=64k`), default and `--jit=1`, green on all five legs.
- Every stress fixture: **200 runs, 0 failures** on hosted `windows-11-arm`, `ubuntu-24.04-arm`, `macos-15` and x64, **each fixture first shown to fail on its pre-fix binary**. Upper bound ≈1.5% per run at 95%, which would have caught #816 (~2.5%) with 99.4% probability.
- Peak RSS on binarytrees reduced per platform versus baseline. The target is set from the nursery sweep and stated per OS; a phase that misses it reports why and ships only the steps that helped.
- No benchmark slower beyond the statistical gate (>5% median **and** p<0.01, ≥15 interleaved runs, pinned CCD). mandelbrot advisory.

### Phase 5: release gates

- [ ] Both lanes' exit gates passed. Anything unfinished is **reverted** or behind a named runtime flag listed in the release notes, never silently disabled.
- [ ] `.obl` regenerated after the last compiler merge (section 4), and the staleness check clean.
- [ ] Five back-to-back `workflow_dispatch` nightly runs on the frozen release branch: no new signatures.
- [ ] `verify_platform` windows-x64 and linux-x64 at the tag commit, **never both at once on the same machine**. CI's ARM64 and macOS legs, plus hosted-runner loops, stand in for real hardware unless the Mac or Surface is available.
- [ ] Clean-machine installs: CI macOS `.pkg` test, per-distro containers, a clean Windows MSI install.
- [ ] Performance page re-measured (unified Docker run) **including memory**, with version and commit stamps.
- [ ] Release notes curated; dry run; tag; maintainer signing; post-release gates 1-6; playground deploy; objeck.org upload.

---

## 4. Working protocol

- **Library bytecode (`.obl`).**
  - Branches that change `core/compiler/**` or `lib_src/**` never commit `.obl`.
  - The linux-x64 CI leg reports `git diff --stat core/lib/*.obl` after `update_version.sh`: report-only on PRs, required at the release candidate.
  - After each batch of compiler merges the controller commits one regeneration (WSL `update_version.sh` + full `deploy_windows.cmd`, which also rebuilds the diags DLL).
  - Never resolve an `.obl` conflict by picking a side. Revert = regenerate.
- **Merge rule.** At most one VM/JIT/GC-risky merge per nightly window. A regression attributed to a merged PR within 24 h is reverted (plus regeneration) and fixed forward on a branch.
- **Who merges.** The controller merges test, infra and fix PRs once gated. The maintainer approves semantic or footprint PRs (1.1, C4, C3, G2, nursery default) in **one batched weekly message**.
- **Tests.** Every fix gets a test that fails on the previous release's binary, checked before it counts. A test that skips never counts as a pass.
- **Messages to the maintainer:** the weekly batch, signing, or a critical issue.

## 5. Risks

| Risk | Mitigation |
|---|---|
| The fuzzer floods the lane with findings | Triage by signature; s3-default miscompiles block, s0-only and crash-on-invalid may be deferred with sign-off. Scope is cut, never the gate |
| The verifier is too slow for the full nightly | `OBJECK_GC_VERIFY=N` sampling; every collection only in stress fixtures |
| Exact object size exposes a hidden overrun | The guard-word run comes first; if unfixable, `* 2` stays |
| A smaller nursery costs throughput | The sweep measures it; the statistical gate decides; the `OBJECK_NURSERY=128m` kill switch |
| Integer semantics change breaks user code | Only undefined or crashing cases change; compatibility search found nothing using counts outside 0-63 except the `>>>` tests |
| Schedule slips | Roughly 20 risky PRs at one per night is 4-6 weeks after Phase 1. A slipped item moves to the next release; the version number stays 2026.10.0 |
