# core_thread_gc_stress on windows-arm64 — investigation handoff

Issue: [#1077](https://github.com/objeck/objeck-lang/issues/1077). Read that
first for the measurements. This file is the part that is **not** in the issue:
what has already been built, what has never actually run, and the order to
attack it in.

Written 2026-10-11, after v2026.10.0.

> **Status (checked 2026-11-18): the issue is closed, this file is the playbook if it comes back.**
> #1077 was closed on 2026-10-13 at **0 failures in 1,500 runs** (two 750-run legs on the hosted
> `windows-11-arm` runner, workflow runs 38308666674 and 38308671728, identical trees, each run under
> `av_catch.py`, whose self-check against a deliberate access violation passed before each loop
> started). The rule of three gives a 95% upper bound of 3/1500 = 0.2% on the failure rate of that
> build on that runner; it does **not** say the original 0.8% defect (3 failures in 360 runs on the
> older base) never existed or that it is fixed, only that it was not seen. The two `203
> corruption(s)` failures of 2026-09-11 were real. Everything below is the original handoff text,
> kept as written except where a line is marked **(updated)**; its "has never fired" statements
> describe 2026-10-11, not today. This file is documentation of an investigation, not a test.

---

## Read this before doing anything, or you will redo it

Three things in this directory already exist. Two of them have been re-derived
from scratch at least twice.

**The failure count is already decomposed.** `gc_stress_diag.obs`'s header says
it: each worker's long-lived list is `1 + 200 = 201` nodes, and one corrupted
`b` in the churn loop trips *two* checks (`sum` and `b->V()`), so the
`203 corruption(s)` seen twice on windows-arm64 reads as **one worker's entire
list, plus one churn iteration**. It is not 203 scattered bad values.

**`gc_stress_diag.obs` already records everything needed to confirm or refute
that** — per worker: `walk_nodes`, `walk_bad`, `first_walk_bad`,
`first_walk_pos`, `churn_errors`, `first_churn_iter`, `first_churn_sum`,
`first_churn_v`, with the round and worker id.

**And it has never once reported (as of 2026-10-11; the later 1,500-run close-out recorded no failure of either build, so it still has not).** Across every probe run to date the diag
build failed exactly once — `diag-default` run 20, `0xC0000005`, **empty
output**, 0.7 s against a normal ~3.1 s. It died before printing. Both 203s were
on the `orig` build, which prints only the total. **So the instrumentation is
built and has never fired.** That, not more clean runs, is the gap.

---

## The hypothesis, and what would refute it

For the walk to report all 201 nodes wrong *and still terminate after exactly
201 nodes*, the chain structure has to be intact while every value read is
wrong. That is what a **stale root into a moved-from region** looks like: after
promotion the old copies still point at each other by their old addresses, so
walking a stale `head` traverses the complete old chain and reads 201 values out
of memory that has since been recycled.

It also explains why the issue's two symptoms are probably one defect: whether
the moved-from pages are still mapped and recycled (→ readable wrong values,
`203`) or released (→ `0xC0000005`, no output) is a property of the pages, not
of the bug.

**Sharp prediction.** A diag report from a detected-corruption failure should show

```
walk_bad = 201   first_walk_pos = 0
```

A small `walk_bad` at a random `first_walk_pos` refutes it and says the damage
really is scattered. `first_walk_bad` says what the recycled memory held — if it
is another worker's `@id`, the nursery was reused by a live thread.

**Any future count should decompose as `201a + 2b + c`.** A count like 97 kills
the hypothesis outright. This is cheap to check and worth checking first.

---

## Order of attack

Each phase is useful on its own, so stop wherever the answer arrives.

### Phase 0 — arithmetic on whatever count CI next reports (minutes)

If `core_thread_gc_stress` reddens on windows-arm64 again, check the count
against `201a + 2b + c` before anything else. Costs nothing and may end it.

### Phase 1 — cloud, because the probe already builds with symbols

`probe-gc-stress-arm64.yml` runs **only** on pushes to `probe/**` branches,
never on master, and already builds the Windows ARM64 toolchain the way
`ci-build` does — which means `obr.pdb` exists in the job. **(updated)**
`probe/gc-stress-arm64-oct` was pointed at then-current master on 2026-10-11; in
this checkout (master `3042a96eee`) it is 1,217 commits behind master, so rebase
or recreate the branch before trusting a result from it.

**This is done** (runs 38308666674 and 38308671728 are the result; see the status note above). To repeat it, push a probe branch and read the artifact. The
workflow runs

```
gc_stress_loop.ps1 -Bin ...\deploy-arm64\bin -Mode diag -Runs 300 -Catcher .\av_catch.py
```

`-Mode diag` puts 300 runs on the build that can report and keeps a 75-run
`orig` cell only so a failure can be told apart from "the diag build does not
reproduce it". The `jit1`/`gc1m`/`jitoff` cells are gone on purpose: at ~0.8% a
40-run cell expects 0.7 failures, so 0 against 1 says nothing, and splitting the
budget five ways is what kept every cell underpowered. Narrowing by
configuration is Phase 4, with 500+ per cell — `-Mode survey` still runs the old
split.

`-Catcher` wraps every run in `av_catch.py` (Phase 2). The job checks for Python
**before** the two-hour build, and the loop refuses to start unless the catcher
reports a deliberately-faulted throwaway process — a catcher that runs and
reports nothing is indistinguishable from a run that did not crash, which is the
ambiguity this probe exists to remove.

~63 minutes for 450 runs un-instrumented; 375 under the catcher fits the
180-minute job cap with the build.

### Phase 2 — put a stack on the silent crash — `av_catch.py`

The `0xC0000005` has never had a faulting frame attached to it. This is the
highest-value single step, and the technique is known-good here: it root-caused
the LSP startup crash (PR #717) and #773.

`av_catch.py` drives the Win32 debug API through ctypes — `CreateProcessW` with
`DEBUG_ONLY_THIS_PROCESS`, a `WaitForDebugEvent` loop, and on a **first-chance**
`EXCEPTION_ACCESS_VIOLATION` it reports the faulting address, whether the access
was a read or a write, the address touched, the module and RVA, and a symbol
name. Then it hands the exception back `DBG_EXCEPTION_NOT_HANDLED`, so the
process dies exactly as it would have unobserved. Handles are inherited, so the
program's own stdout still reaches the `.out` file — the diag report must not be
lost to the instrumentation.

Verified on x64 against a deliberate null write:

```
[av] exception 0xC0000005 at 0x00007FF9FD661AE4 (first chance)
[av]   write of 0x0000000000000000
[av]   in VCRUNTIME140.dll+0x1ae4
[av]   symbol: memset+0x144
```

**Module+RVA comes from `VirtualQueryEx` + `GetModuleFileNameExW`, not dbghelp**,
so a missing PDB or a dbghelp failure degrades to `obr.exe+0x1234` — resolvable
offline — instead of to nothing. Verify the PDB matches the binary by hash
before trusting a symbol name.

> **`--stack` is deliberately not implemented, and the CONTEXT offsets in the
> `win-av-catcher` memory note are x64 and WRONG here.** `CONTEXT` for ARM64 is
> a different structure and a different size; do not reuse 1232 / 0x30 / 0x98 /
> 0xF8. A wrong layout yields a plausible-looking garbage address, which is
> worse than no stack at all. `ExceptionAddress` needs no CONTEXT and is exact,
> so the first version takes the exact answer over the richer guess. If a stack
> is genuinely needed later, take the layout from `winnt.h` (`_CONTEXT` under
> `_ARM64_`) and assert `sizeof` before trusting a single field.

### Phase 3 — the WoA box, if the cloud rate is too low

Everything above runs in the cloud. Fire up the box when one of these is true:

- **the cloud will not catch one.** At ~0.8% with debugger overhead, a job may
  not fit enough runs. The box has no 6-hour cap.
- **iteration is the bottleneck.** 30–60 minutes per cloud attempt versus
  minutes locally, once a fix is being tested.
- **you want a second environment.** The rate is demonstrably
  environment-dependent — identical source gave 0/180 and then 3/180 on the
  hosted runner — so different hardware is genuine information either way,
  including a clean 200 runs.

Fastest start, no toolchain needed:
`objeck-windows-arm64_2026.10.0.zip` **is** a native ARM64 build of current
master. Unzip it and point the existing loop at its `bin`:

```powershell
pwsh programs\regression\probe\gc_stress_loop.ps1 -Bin <unzipped>\bin -Runs 100
```

Build locally only when you need symbol names for Phase 2 — the release zip
ships no PDBs.

### Phase 4 — narrow by configuration, but only with real counts

The 2026-09-25 table shows 0 failures under `OBJECK_JIT_THRESHOLD=1` and under
`--jit=off`, and **it does not support the conclusion people draw from it**: at
0.8% a 40-run cell expects 0.7 failures, so 0 against 1 is noise, and
`--jit=off` has 0/40 pooled where ~0.3 were expected. 500+ runs per cell is what
it takes to say anything.

### Phase 5 — prove any fix on the PRE-fix VM

A fix validated only against the fixed build proves nothing at a 1% rate. Build
the pre-fix VM, confirm it still reproduces at the measured rate, then apply the
fix and show the rate collapse. Same discipline as PR #660.

---

## The mechanism lead, and the dead end

Both backends' safepoints refresh **only** `frame->mem[0]` after a park — the
`self` refresh from #746. Anything else a loop holds outside `frame->mem[]`
across a park is not refreshed by anybody.

**Ruled out:** ARM64's `local_reg_cache`. A cached local is read straight from
its register, which looked like the hole, but `case LBL` calls
`FlushLocalCache()` and the poll is emitted at the *next* instruction index, so
the cache is provably empty at the park. Not it.

**Not ruled out:** amd64 pins a loop local in `R15` with explicit
`EmitPinWriteBack` regions; ARM64 has no equivalent. Whether a pinned or cached
**object reference** can outlive a park on either backend is unestablished. This
fits the circumstantial evidence — both commits present when the two 203s
appeared were ARM64 register-allocation changes (`the callee's entry and exit, a
func-ref slot fix`; `X12-X15 join the register pool`). Circumstantial is the
right word: that is two data points on what was being pushed that day.

The test's header lists five already-fixed root-registration and moving-GC bugs.
A sixth of that family fits both symptoms.

---

## Traps

- **Launch from a native PowerShell or cmd, never Git Bash.** Git Bash is
  x64-emulated. On 2026-09-14 a gate launched from it read
  `PROCESSOR_ARCHITECTURE=AMD64`, recorded `tests=SKIPPED` and still ended
  `verdict=PASS` with nothing run.
- **Do not change the `200` in either test.** It is load-bearing for diagnosis:
  the `201`-node list is what makes the failure count mean something. Changing
  it silently changes the signature.
- **Do not give either file the JIT opt-out marker, or quote it in a comment.**
  **(updated)** The runners used to match the marker as a substring, so a comment
  that merely *named* it opted the test out; that is how this test ran interpreted
  in CI from June to September 2026 and #746 went unseen. Since 2026-09-09 the
  marker counts only as a whole line at column 0 and needs a `# reason:` line
  (`run_regression.sh`, `tools/cicd/check_jit_optouts.py`), so a comment can no
  longer opt a test out, but the advice to keep the marker out of these two files
  stands: they must run with the JIT on.
- **`gc_stress_diag.obs` avoids `&` and `|` in `Run` on purpose** — they set
  `HasAndOr`, which reserves a frame slot on ARM64 and would change the very
  frame layout under suspicion. Keep it that way.
- **`probe/gc-stress-arm64` is far behind master** (base `e034fae228`, around
  #780; ~610 commits behind when this was written, 1,840 in the checkout of
  2026-11-18). Its own two runs say nothing about the VM shipping today. Use
  `probe/gc-stress-arm64-oct`, rebased (see above).
- **A clean run is not evidence of a fix.** 0/450 on master leaves a 95% upper
  bound (~0.66%) that overlaps the old estimate; reaching under 0.2% needs
  roughly 1,500 runs (reached, 0/1500, on 2026-10-13: bound 0.2%, still a bound
  on one runner and one build, not a proof).

---

## Limits of the evidence in this file

- Every rate here (about 0.8%, from 3 failures in 360 runs on the old base) comes from the hosted `windows-11-arm` runner, which has four cores; the same source gave 0 of 180 and then 3 of 180 on it, so the rate depends on the runner as much as on the code and cannot be transferred to other ARM64 hardware.
- The `201a + 2b + c` decomposition and the stale-root hypothesis were never confirmed by a diag report, because the diag build did not fail again. They explain the two observed `203` counts; they are not an established root cause.
- The mechanism lead (pinned loop locals and safepoint refresh) is circumstantial, as the section says, and the ARM64 `local_reg_cache` dead end was ruled out by reading the code, not by an experiment.

## What to report back

Whatever happens, put it on #1077 rather than in a session — three probe runs
once sat unread for two weeks, which is why the issue exists at all.

- a diag report line, if one is ever produced — it is the whole point
- a faulting frame, if the catcher lands one
- the raw counts per configuration, even when they are all zero
- the exact commit, because the rate is not stable across them
