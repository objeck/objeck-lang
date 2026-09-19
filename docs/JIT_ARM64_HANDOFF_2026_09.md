# macOS handoff: the ARM64 JIT track (2026-09-09)

> **Status, 2026-09-19.** Open: **4a** (F3, loop locals in `X20`-`X27`), **4b** (`D8`-`D15`
> float pins) and **4c** (`HasAndOr` narrowing in the compiler). None of the three is in the
> tree: `jit_arm_a64.cpp` has no pin planner, and `&`/`|` anywhere still sets the flag. Done:
> **4d**, F7 on ARM64 ([#768](https://github.com/objeck/objeck-lang/pull/768),
> [#776](https://github.com/objeck/objeck-lang/pull/776)); F4, twelve pool registers
> ([#770](https://github.com/objeck/objeck-lang/pull/770)); the ARM64 baseline 3b asked for
> (the assessment's section 5a); and the v2026.9.1 release section 6 planned, with 9.2 to 9.5
> since. Current below: the check-in block's corrections, next steps, Windows ARM64 notes and
> traps; sections 2, 4a-4c, 5 and 7; and the traps in 4d. The rest is history, marked as such.

**Audience:** the next working session on the macOS ARM64 machine (maintainer plus agent).
**Why:** batches 1-5 of `JIT_CODEGEN_ASSESSMENT_2026_09.md` were built on a Windows x64 box,
where the ARM64 backend could only be cross-compiled; its runtime check was CI's three ARM64
legs, thirty minutes a round trip with a failure artifact as the only debugger. Everything
still open on the assessment's list is ARM64-heavy, and on the Mac the backend builds, runs and
sits under `lldb` in minutes.

## Check in here first (written 2026-09-10 night, on the Mac)

*History, trimmed 2026-09-19.* This block opened with a table of the tree on the night of
2026-09-10/11 (master `433cf916cd`, the v2026.9.1 release commit) and the PC's three requests
of 2026-09-11. Every item landed or was settled: the two F7 steps, the two ARM64 bugs the
entry-shapes test found, F4 ([#770](https://github.com/objeck/objeck-lang/pull/770)), the libc
float parking ([#771](https://github.com/objeck/objeck-lang/pull/771)), the constant-character
store ([#781](https://github.com/objeck/objeck-lang/pull/781)), the locale and exit-status
fixes ([#772](https://github.com/objeck/objeck-lang/pull/772),
[#775](https://github.com/objeck/objeck-lang/pull/775),
[#778](https://github.com/objeck/objeck-lang/pull/778)) and the every-method-compiled pass on
every CI leg ([#784](https://github.com/objeck/objeck-lang/pull/784)); the two windows-arm64
`core_thread_gc_stress` failures did not reproduce in 180 runs on the hosted runner. Git
history has the table, and `docs/HANDOFF_2026_09_10.md`, the day's full record. What follows
is still current.

**Corrections to what is written below.** The pool was eight general registers, `X0`-`X7`, not
fifteen (`X9`-`X15` were commented out in `Compile()`); it is twelve now, `X0`-`X7` and
`X12`-`X15`, and `X9`-`X11` are scratch. The float
pool hands out `D0` first, not `D15`, so `D8`-`D15` are touched only with nine floats live. The
backend does not spill: an expression with more than twelve live temporaries falls back to the
interpreter whole, and the bytecode pushes every term of a chain before the first add, so a
thirteen-term sum is such an expression.

**Next on the Mac, in this order.**

1. 4a (loop locals in `X20`+) and 4b (`D8`-`D15` pins), by the numbers; 4d is done ([#776](https://github.com/objeck/objeck-lang/pull/776)).
2. The callee's native prologue, about fifty instructions and mostly stores (design section
   13), is where the next nanosecond of a call is.

**Windows ARM64, as of tonight.** Three things for whoever next touches that leg.

- *A `core_thread_gc_stress` failure, once.* The windows-arm64 leg of #768's CI reported "203
  corruption(s)" at `68946d87ec`, a docs-only commit whose code the next push ran green. It was
  the first failure of that test on any ARM64 leg in the forty-two samples before it. On the Mac
  it did not reproduce in 144 stress runs (with every method compiled, a 1 MB GC threshold, four
  instances at a time, on the VMs from before #768, from master and from #771), nor on the
  native-entry VM (48 runs with 0 corruptions). If it recurs, reproduce it on Windows ARM64 or on a
  four-core Linux ARM64 box before suspecting anything specific; #746 needed four pinned cores
  to show at all.
- *What the native entry does for it.* A `long` is four bytes there, so a `StackFrame`'s `ip`
  and `jit_offset` and the call-stack position are loaded and stored by their size
  (`load_long`, `store_long`); `X18`, the platform register, is never touched; `x16`/`x17` are
  written just before their use.
- *Stack probing.* A frame is allocated with one `sub sp` and never probed. Windows expects a
  frame larger than a page to touch each page in order, and this was true before tonight; the
  frame grew by 112 bytes plus the outgoing area, so a method needs about 450 locals to
  cross a page.

**Traps the Mac added to the list.** From an agent shell with no `LANG`, Python coerces the
locale and passes `LC_CTYPE=C.UTF-8` to every child; macOS's C library reports that as the
composite `C/C.UTF-8/C/C/C/C`, which libc++ cannot construct a `std::locale` from, and `obr`
exited at startup with a `collate_byname` message, so `run_vm_flag_tests.py` reported seven
failures that were not the VM's; the workaround was `LC_ALL=en_US.UTF-8`. Fixed in [#772](https://github.com/objeck/objeck-lang/pull/772):
`posix_main.cpp` (the entry Xcode builds; `SetEnv` in `vm.cpp` is Windows-only) builds the
console locale through a helper that falls back to one libc++ can construct, and the flag
tests cover it, so no variable is needed from any shell. `obc` needs `OBJECK_LIB_PATH` at the
deploy `lib` when run from outside the tree. A
tracing VM: in `core/vm`, `xcodebuild -project xcode/VM.xcodeproj build
GCC_PREPROCESSOR_DEFINITIONS='$(inherited) _DEBUG_JIT_JIT' SYMROOT=<dir> OBJROOT=<dir>`, which
leaves the deploy tree alone; its listing omits emitters that print under `_DEBUG_JIT` only
(`add_shifted_reg_reg`) and prints per call at run time, so trace a small program. A master VM
for alternated timings comes from a `git worktree` of master built the same way. `zsh` treats a
bare `====` as a command; separate output with `printf`.

## 1. Where the tree was on 2026-09-09 (history)

The batches this track started from, all merged.

| item | state |
|---|---|
| batch 1 (#731) | `Size()` inlined on both backends |
| batch 2 (#732) | division by a constant is a multiply (magic numbers), both backends |
| batch 3 (#733) | eight-register AMD64 pool (`R8`-`R11`), `XMM10`-`XMM15` saved on Windows, `OBJECK_JIT_REPORT=1` |
| batch 4 (#735) | F6 short-circuit conditions (compiler); F3 loop locals in `R13`-`R15` (AMD64 only); six fixes: the loop-header safepoint poll that never fired, magic division with the dividend in `RDX`, 16-bit movers without REX, the `HasAndOr` slot-0 contract (ARM64 collector walk), an ARM64 float-register leak per fused compare, and the s3 inliner pasting `native` methods into interpreted callers (the "thread runs 55x slower" mystery) |
| batch 5 (#738) | F8 `select` jump tables on both backends |

Design and rationale live in `docs/JIT_LOOP_LOCALS_DESIGN.md` (F3, with section 6 listing what
each fix taught), `docs/JIT_SELECT_TABLES_DESIGN.md` (F8) and the assessment's section 8.

## 2. The macOS loop

Full build, both regression passes, the flag tests (this is what CI's macos-arm64 leg runs):

```bash
git checkout master && git pull
cd core/release && ./deploy_macos_arm64.sh
cd ../../programs/regression && ./run_regression.sh arm64
OBJECK_JIT_THRESHOLD=1 ./run_regression.sh arm64
python3 run_vm_flag_tests.py ../../core/release/deploy/bin
```

Inner loop for a VM-only change (the JIT lives in the VM; `deploy_macos_arm64.sh` does exactly
this for `obr`, the Xcode project is the macOS build, `core/vm/make/Makefile.arm64` is Linux):

```bash
cd core/vm && xcodebuild -project xcode/VM.xcodeproj build && cp xcode/build/Release/obr ../release/deploy/bin
```

After a **compiler** change, rebuild the standard library before trusting any run -- CI's POSIX legs
rebuild it and a local run with old libraries proves nothing (batch 4's ARM64 crash hid behind
exactly that on Windows):

```bash
cd core/compiler && bash update_version.sh arm64 && cp ../lib/*.obl ../release/deploy/lib/
```

Do not commit the thirty rebuilt `.obl` unless `lang.obs` changed (`git checkout -- core/lib`);
when it did, `core/lib/lang.obl` must be committed or windows-x64 goes red alone.

The gate every batch has passed, in this order: `vm_jit_equiv.obs` byte-identical between
`--jit=off` and `OBJECK_JIT_THRESHOLD=1` (the flag tests do this); `OBJECK_JIT_THRESHOLD=1
OBJECK_JIT_REPORT=1` on the fixture with no method reporting a fallback; both regression passes;
then CI on a PR against `master` (a stacked PR gets no build legs).

Knobs: `OBJECK_JIT_REPORT=1` (what compiled, what fell back and why, pinned loops),
`OBJECK_JIT_THRESHOLD=1` (compile everything on first call) or `=999999999` (interpret
everything, `native` excepted), `OBJECK_GC_TRACE=1` (every JIT frame and slot the collector
scans), `OBJECK_JIT_PIN_MAX=n` / `OBJECK_JIT_PIN_SKIP=n` (AMD64 pinning, for bisecting), `--jit=off`,
`obc -asm` (a `.obm` bytecode listing next to the `.obe`), `obc -opt s0..s3`.

Debugging a JIT crash on the Mac: `lldb -- core/release/deploy/bin/obr prog.obe`, `run`, then
`bt`, `register read`, `disassemble -s '$pc-64' -c 48` (JIT pages disassemble like any other);
a crash in the collector's root scan wants `OBJECK_GC_TRACE=1` first; CI's macOS crash reports
are in the failure artifact's `crash-reports/*.ips`, local ones in `~/Library/Logs/DiagnosticReports`.

## 3. First things on the Mac (history: both done)

### 3a. Issue #722 is fixed -- but 27 tests still opt out of the JIT

*Done: #745 merged; three opt-outs remain, each with a stated reason (`check_jit_optouts.py`).*

`http_persistence_test.obs` and `https_persistence_test.obs` used to carry `# JIT_DISABLE`
because on ARM64 the JIT miscompiled `String->Equals` inside a virtual request-handler callback
(`ProcessGet` on an `HttpRequestHandler` subclass): the compare of the returned `Bool` went
wrong only under the JIT, only on ARM64 -- the code path every Objeck web server runs, compiled
by default. Batch 4 fixed it (one of the safepoint poll, the slot-0 contract, or the
float-register leak). Verified by probe PR #741, now on master: both tests route by
`String->Equals` again with their markers removed, the reduction fixture `jit_virtual_equals.obs`
was added, and all three ARM64 legs are green. Issue #722 is closed.

What that leaves is the pattern, not the bug -- and the pattern turned out to be worse than a
few stale opt-outs. Both regression runners matched `# JIT_DISABLE` as a *substring*, so five
tests written to catch JIT bugs (`jit_autojit_race`, `jit_concurrent_compile`,
`jit_float_mem_ops`, `core_thread_gc_stress`, `jit_virtual_equals`), whose comments said "do NOT
add a `# JIT_DISABLE` marker", ran with the JIT **off** on every leg for as long as that sentence
had existed. PR #745 fixes the runners (whole-line on POSIX, begin-anchored on Windows, where
`findstr` cannot end-anchor an LF-ended line), drops the twenty opt-outs that never said why --
every one of the twenty-two real directives passes compiled on AMD64 -- keeps the two with a
reason (`interp_float_fastpath`, `bad_runtime_stack`) and states it, and adds
`tools/cicd/check_jit_optouts.py` to CI so a directive without a `# reason:` line, or prose that
quotes the marker, fails the build. Its ARM64 legs are the other half of the audit: twenty-five
tests run compiled there for the first time, and a failure is a #722-class finding to chase on
the Mac (`jit_arm_a64.cpp`; the `IMUL` operand-order bug of #721 and the stored float compare of
#660 were the same shape -- a caller-saved register one backend spills across a call and the
other does not).

### 3b. The ARM64 baseline

*Done:* the assessment's section 5a has the six kernels of `programs/tests/jit_probe.obs`,
interpreted and compiled, measured on an Apple M4 Max at `f9d777fc41` (2026-09-09), and section
5b explains why the fixture's kernels are no longer `native`. Every later ARM64 number is read
against that table.

## 4. Next batches, in order

### 4a. F3 on ARM64 -- the headline item's other half

Design: `JIT_LOOP_LOCALS_DESIGN.md` section 3.2, registers `X20`-`X27` (`X19` holds `&stw_active`;
the pool is `X0`-`X7` and `X12`-`X15`; `X9`/`X10` are scratch). Port the AMD64 pieces, all in
`jit_amd_lp64.cpp`, into `jit_arm_a64.cpp`:

1. `PlanPinRegions()`: merge `detected_loops` into regions; eligibility (no jump from outside into
   the interior, no interior jump before the header, and -- since batch 5 -- a `JMP_TABLE`'s edges
   must not cross the region at all); candidates from the `StackDclr` types with the `HasAndOr`
   base of 1; weights `1 << min(depth*2, 12)`; top `K`. Wire `OBJECK_JIT_PIN_MAX` / `_SKIP`
   through `JitEnvFlag` (`jit_common.h`).
2. `EmitPinEntry` at the loop header, which on ARM64 is the same place as the safepoint poll
   (the top of `ProcessInstructions`, keyed on the target instruction): loads first, then
   `loop_offset = code_index`, then the poll, so the back-edge lands past the loads and still polls.
3. Pinned paths in `ProcessLoad`/`ProcessStore`/`ProcessCopy` for `LOCL` int/char slots.
4. Region-end write-back after the dispatch switch; `EmitPinExitStubs` after the body (write-back
   then `b` to the real target; the ARM64 fixup pass patches word offsets, `src = first - 1`,
   `dest = operand - 1`); the fixup loop redirects exit jumps to their stubs and interior jumps to
   the header to `loop_offset`.
5. Prologue/epilogue: `stp`/`ldp` the pinning registers in pairs beside the `X19` save, only when
   the method pins.
6. ARM64 has no JIT-level inlining, so the `is_inlining` guards of the AMD64 version disappear.

The contract that bit batch 4: the frame layout is agreed with the collector (`CheckJitRoots`
walks ARM64 frames front-to-back and does `mem++` when `HasAndOr` is set), so slot 0 is reserved
whenever the flag is set, regardless of use -- `ProcessIndices` already does this on ARM64.
Object, array and function slots are never pinned; they move.

Tests already in place: the fixture's `Loops` (eight-local loop, break/continue, nesting,
do/while, calls and allocations in the loop, early return, join and floats, gcd), `Narrow`,
`Selects->PinnedBreak`; `jit_gc_safepoint.obs`, `jit_branch_shapes.obs`, `jit_native_inline.obs`.
Expect `Locals` and `ArraySum` to move the way AMD64 did (0.027 to 0.020 s, 0.017 to 0.014 s on
x64) and record the ARM64 before/after in the assessment.

### 4b. Floats in callee-saved registers

`D8`-`D15` on ARM64, `XMM6`-`XMM9` on Windows (design section 5): the same planner with a float
candidate class, `REG_FLOAT` pinned paths, and the `stp`/`ldp` of the D registers next to the
existing callee-saved FP save slots in the ARM64 prologue.

### 4c. `HasAndOr` narrowing (compiler)

After F6 most conditions no longer touch slot 0, but the compiler still sets the flag for any
`&`/`|`, so most methods reserve a slot they never use. Set it only for value-context connectives
(`EmitAndOr`), ternaries, `select` and lambdas. Safe because the flag *is* the contract: both
JITs reserve the slot exactly when it is set and the collector skips it exactly when it is set.
Rebuild the libraries and run the ARM64 suite with `OBJECK_GC_TRACE=1` handy.

### 4d. F7 on ARM64 -- done

Both steps are merged. Step 1, the callee's entry and exit (the design's section 12), in
[#768](https://github.com/objeck/objeck-lang/pull/768): a compiled call 17.1 ns to 13.4 ns,
`RealCall` 0.363 s to 0.279 s, `Fib(32)` 0.135 s to 0.103 s, with the prologue's frame-size
immediate computed exactly. Step 2, the native entry and call site (section 13), in
[#776](https://github.com/objeck/objeck-lang/pull/776): a compiled call 13.8 ns to 7.4 ns,
`Fib(32)` 0.107 s to 0.050 s. `JIT_CALLING_CONVENTION_DESIGN.md` describes what was built; the
step-by-step plan that stood here is in git history. The next nanosecond of a call is in the
callee's native prologue ("Next on the Mac", above).

**Traps the AMD64 work found, all of which apply here.** Every `RTRN` emits its own epilogue
(patch every one). A callback-left return value at `RTRN`: a `Runtime->Copy` result sits on the
operand stack, not the working stack, and the native exit must pop it into `D0` (the AMD64 first
build missed this and every `SubString` came back `Nil`). The frame is laid out
from the declarations, not the references (#761, already on ARM64). A native call site
reads the receiver from the outgoing area, not the operand stack, so the `Nil` and
non-object checks move with it. `R11`/`X` scratch use in a call site is safe only after
every live value is spilled, which `ProcessStackCallback` does before anything else. The
first native prologue's `top` register must be held across both prologues so the one
`ProcessParameters` sees the same register from either entry.

## 5. Rules of the road

- Merges happen only on the maintainer's explicit word ("merge N when green"). The gate: the five
  build legs, `Tools (formatter, LSP, VS Code extension)` and `CI Status` all pass and nothing
  else fails; then `gh pr merge N --merge --delete-branch`. `gh pr merge --auto` merges at once
  here; never use it.
- PRs target `master`; a stacked PR gets no build legs.
- Design paragraph first; fixture probes that fail before the change; both regression passes;
  one commit per concern (fix, test, docs); a `CHANGELOG.md` entry per user-visible change under
  the next release's section.
- Never a bare `git stash`, never a force-push of public history, never credential material in a
  file, a transcript or a workflow, never signing in a workflow.
- `.obs` files show as binary in `git diff`; use `git diff --text`.
- The agent cannot run `gh pr close`/`reopen` in auto mode; ask the maintainer.

## 6. Releasing from the Mac

*History: v2026.9.1, the release this section planned, shipped on 2026-09-12, and 9.2 to 9.5
followed.* One constraint outlives it. macOS signing and notarization happen in CI, but Windows
MSI signing needs the SafeNet eToken on the Windows box (`tools/cicd/sign_release.cmd`, which
rewrites the MSIs and regenerates `SHA256SUMS`), so a release tagged from the Mac is complete
only once that step has run there.

## 7. Map

- JIT: `core/vm/arch/jit/amd64/jit_amd_lp64.{h,cpp}`, `core/vm/arch/jit/arm64/jit_arm_a64.{h,cpp}`,
  shared `core/vm/arch/jit/jit_common.{h,cpp}`; the collector's root walk and `OBJECK_GC_TRACE` in
  `core/vm/arch/memory.cpp`; opcode handlers in `core/vm/dispatch.cpp`; `core/vm/loader.cpp`.
- Compiler: `core/compiler/intermediate.cpp` (`EmitBranch`, `EmitSelectJumpTable`),
  `core/compiler/optimization.cpp` (`CanInlineMethod`), `core/compiler/emit.h`.
- Docs: `docs/JIT_CODEGEN_ASSESSMENT_2026_09.md`, `docs/JIT_LOOP_LOCALS_DESIGN.md`,
  `docs/JIT_SELECT_TABLES_DESIGN.md`, `docs/JIT_CALLING_CONVENTION_DESIGN.md`,
  `core/vm/arch/jit/README.md`, `CHANGELOG.md`.
- Tests: `programs/regression/vm_jit_equiv.obs` with `run_vm_flag_tests.py`,
  `jit_gc_safepoint.obs`, `jit_branch_shapes.obs`, `jit_native_inline.obs`,
  `core_bool_short_circuit.obs`; kernels in `programs/tests/jit_probe.obs`.
