# macOS handoff: the ARM64 JIT track (2026-09-09)

**Audience:** the next working session on the macOS ARM64 machine (maintainer plus agent).
**Why:** batches 1-5 of `JIT_CODEGEN_ASSESSMENT_2026_09.md` were built on a Windows x64 box,
where the ARM64 backend could only be cross-compiled; its runtime check was CI's three ARM64
legs, thirty minutes a round trip with a failure artifact as the only debugger. Everything
still open on the assessment's list is ARM64-heavy, and on the Mac the backend builds, runs and
sits under `lldb` in minutes.

## Check in here first (written 2026-09-10 evening, on the Windows box)

Sections 1 and 3 below describe the tree as it was on 2026-09-09; this block is what changed
since and where to start. `docs/HANDOFF_2026_09_10.md` has the day's full record.

| item | now |
|---|---|
| `master` | `6340a4cc6d` after [#765](https://github.com/objeck/objeck-lang/pull/765); [#766](https://github.com/objeck/objeck-lang/pull/766) (the compiler fix for #763, the Linux x64 register pool, and section 4d below) merges on top when its legs are green |
| F7 on AMD64 | complete: the design's sections 6 to 11 (`JIT_CALLING_CONVENTION_DESIGN.md`). A bound compiled call 26.5 ns to 5.5 ns, a virtual one 125 ns to 6.0 ns, `Fib(32)` 0.208 s to 0.043 s. ARM64 has phase 1 only |
| ARM64 backend | untouched since #761 (frame laid out by declaration, both backends). Its pool is fifteen general and fifteen float registers, so the Linux x64 pool change has no ARM64 counterpart |
| compiler | #763 fixed: a method with a func-ref parameter can be inlined again, so fixtures no longer need a second `return` to keep such helpers out of the inliner |
| deploy trees | the Windows box holds #766's binaries; the Mac builds its own (section 2) |

**First hour on the Mac, in this order.**

1. The macOS loop of section 2 on `master`: deploy, both regression passes, the flag tests.
   Everything since 2026-09-09 was verified on Windows x64 and Linux x64 only; CI's
   macos-arm64 leg is the sole ARM64 check so far.
2. Time `programs/tests/jit_call_probe.obs` (bound, virtual and recursive calls, section 4d's
   "measure first") and `programs/tests/jit_probe.obs` (section 3b) on the deploy `obr`, and put
   the numbers in the two design documents' tables beside the AMD64 columns.
3. Do 4d's step 1 (the callee's entry and exit, three small commits) before anything else: it is
   a day's work with the AMD64 file as the template, and each piece is measured by the probe.
4. Then choose by the numbers between 4a (loop locals in `X19`+ registers) and 4d's step 2 (the
   native entry). On AMD64 the call work was worth more than the loop work
   (`Fib(32)` 4.8x against 2-8x on loop kernels), but ARM64's wider pool may change the balance.

**Rules that bit on the way here, all in `HANDOFF_2026_09_10.md` too.** Run the regression
suite only in `programs/regression` itself (a copy of the directory misreports the nineteen
`bad_*` tests). Rebuild the deploy tree with the deploy script before a suite run: a compiler
from a plain `make` in `core/compiler` produced an `obc` whose error paths printed nothing.
`OBJECK_JIT_REPORT=1` on the fixture after every backend change. A value a callback leaves
on the operand stack (`Runtime->Copy`) is the return value when the working stack is empty
at `RTRN`; the AMD64 native exit learned that the hard way (fixture probe `CopyResults`).

## 1. Where the tree is

| item | state |
|---|---|
| `master` | `7072bcedad`, batch 4 merged; `ci-build` green; maintainer tested ARM64 on macOS |
| batch 1 (#731) | `Size()` inlined on both backends |
| batch 2 (#732) | division by a constant is a multiply (magic numbers), both backends |
| batch 3 (#733) | eight-register AMD64 pool (`R8`-`R11`), `XMM10`-`XMM15` saved on Windows, `OBJECK_JIT_REPORT=1` |
| batch 4 (#735) | F6 short-circuit conditions (compiler); F3 loop locals in `R13`-`R15` (AMD64 only); six fixes: the loop-header safepoint poll that never fired, magic division with the dividend in `RDX`, 16-bit movers without REX, the `HasAndOr` slot-0 contract (ARM64 collector walk), an ARM64 float-register leak per fused compare, and the s3 inliner pasting `native` methods into interpreted callers (the "thread runs 55x slower" mystery) |
| batch 5 (#738) | F8 `select` jump tables on both backends; green on all 16 checks, **merge on the maintainer's word** |
| #736, #737 | CI bisect drafts from batch 4, done; close them (the agent cannot close PRs in auto mode) |
| v2026.9.1 | not tagged; `CHANGELOG.md` carries every batch; README/readme.html/readme.txt/docs/web flip at release time (see section 6) |

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

## 3. First things on the Mac

### 3a. Issue #722 is fixed -- but 27 tests still opt out of the JIT

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

Section 5 of the assessment still says the ARM64 kernel timings are unmeasured. Before touching
the backend, time `programs/tests/jit_probe.obs` (the six kernels of section 1, plus the follow-ups)
interpreted and compiled, and add the table to the assessment. Every later number needs it.

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

### 4d. F7 on ARM64: the bridge is there, the rest is not (updated 2026-09-10)

On AMD64 the whole of `JIT_CALLING_CONVENTION_DESIGN.md` is built and merged (sections 6 to
11: the bridge, the callee's entry and exit, the direct native call, inline caches for
`virtual` and func-ref sites, and the register-argument entry). A bound compiled call went
from 26.5 ns to 5.5 ns, a virtual one from 125 ns to 6.0 ns, `Fib(32)` from 0.208 s to
0.043 s. ARM64 has phase 1 only: `ProcessStackCallback` goes through `JitDirectCall` with
the callee in `X0`, the bridge resolves `virtual` callees and pools frames, and
`NativeCode::native_entry` is null on this backend (nothing reads it). Everything below is
the AMD64 file (`jit_amd_lp64.cpp`) translated; keep it open beside `jit_arm_a64.cpp`.

**Measure first.** `programs/tests/jit_call_probe.obs` has never run on the Mac. Its five
kernels (`InlinedCall`, `RealCall`, `NoCall`, `VirtualCall`, `Fib(32)`) give the per-call
cost as `RealCall` minus `NoCall`; record them in the design's section 1 beside the AMD64
column before touching anything, then after each step.

**Step 1, the callee's entry and exit (the design's phase 2).** Three changes, each a
commit with its probe timings:

- `ProcessParameters` reloads `OP_STACK` and `OP_STACK_POS` for every parameter and does
  `dec; ldr; lsl; add; ldr` per argument. Compute `top = op_stack + count * 8` once, read
  each argument at `[top, #-8*w]` (`ldur` for the negative displacement, which the signed
  helper already emits), and drop them all with one `sub` on the count. `ProcessReturn` is
  the mirror: a running displacement from `top` and one `add` on the count. The result pops
  (`ProcessIntCallParameter`, `ProcessFloatCallParameter`, `ProcessFunctionCallParameter`)
  become `count -= 1; ldr Xd, [op_stack, Xcount, lsl #3]`.
- `RegisterRoot` zeroes `[TMP_X0, TMP_X0 + offset)` with a five-instruction loop; unroll it
  into straight `str xzr` (or `stp xzr, xzr`) for frames up to about 24 words, as AMD64 did.
- The prologue saves `D8`-`D15` unconditionally (eight `str`) and the epilogue restores them.
  `aval_fregs` hands out `D15` first, so a method that takes no float register never touches
  them: record the save block's index in `Prolog`, every restore block's index in `Epilog`
  (every `RTRN` emits its own epilogue -- the AMD64 first version patched only the last one
  and crashed every early return), and once the body is emitted turn each block into a
  branch over itself when the FP pool was never used (`xmm_pool_used` on AMD64).

**Before step 2: the prologue's frame-size immediate.** `Prolog` and `Epilog` build their
`sub sp, sp, #imm` and `add sp, sp, #imm` by ORing `final_local_space << 10` onto a template
whose immediate field already holds 96 (`0xd10183ff`, `0x910183ff`): a frame whose size has
bits 5 or 6 clear is over-allocated by 32 to 96 bytes. Harmless so far, since the epilogue
mirrors it and the three stack arguments are read before the `sub`, but anything that computes
an `SP`-relative offset from `final_local_space` -- the outgoing area below -- lands in the
wrong place on such a frame. Compute the immediate exactly (clear the field, then OR), keep the
12-bit range check, and add a register form (`sub sp, sp, xN`) for frames past 4 KB.

**Step 2, the native entry and the call site (sections 8 to 11 in one go).** Section 8's
first shape -- the callee's record built on the caller's stack, the bridge entry's eleven
values passed by hand -- was superseded by section 11 and is gone from AMD64; build section
11's shape directly:

- *Two prologues over one body.* `EmitBridgePrologue` is today's prologue (`X0`-`X7` and
  the three stack values into the fixed slots) plus `top = op_stack + count * 8` and the
  count drop, then a branch to the join. `EmitNativePrologue` fills the same slots itself:
  `CLS_ID`, `MTHD_ID`, `CLASS_MEM` as immediates, `INSTANCE_MEM` from `X0`, and `OP_STACK`,
  `OP_STACK_POS`, `CALL_STACK`, `CALL_STACK_POS` copied from the caller's frame through
  `X1`. Both end with `top` set and meet at `RegisterRoot`; `ProcessParameters` takes `top`
  and runs once. The bridge entry stays at offset 0 for `JitRuntime::Execute`; the native
  entry's offset goes into `NativeCode` (add the parameter to the ARM64 constructor, which
  sets `native_entry` null today) and `SetNativeCode` publishes it.
- *The arguments.* AMD64 puts them at a fixed offset from the callee's frame pointer because
  its frame pointer is the caller's stack pointer plus 16. ARM64 frames are `SP`-relative with
  the fixed slots at `[SP, #0..256)`, so use a third register instead: `X2 = &args` (the
  end of the caller's outgoing area, `top` for the callee), `X0 = self`, `X1 = caller SP`
  (the context the four stack pointers are copied from). The caller reserves its outgoing
  area above its locals -- `final_local_space = local_space + RED_ZONE + out_area`, sized
  by a pre-scan of its call sites, with the `X19` save slot moving up with it -- and writes
  the arguments, the receiver and (a func-ref call) the func-ref word there in
  operand-stack order. `RegisterRoot`'s zeroing and the collector's `offset` are computed
  from `local_space` before the area is added, so neither sees it.
- *The frame record.* A block in the callee's frame, also above the locals and outside the
  scanned region: the `StackFrame`, its two `mem` words `(self, 0)`, and the entry-kind
  word. The native prologue fills it (`method` immediate, `mem = &block.mem`, `ip = -1`,
  `jit_called = 0`, `jit_mem = 0`, `jit_offset = 0`, `jit_inst_mem = 0`), points `JIT_MEM`
  and `JIT_OFFSET` at its fields -- the ARM64 callback path derives `frame->mem` from
  `JIT_MEM` by `offsetof` arithmetic, which keeps working -- and pushes it:
  `call_stack[pos] = &block` with `stlr`, then `pos++`, the order `PushFrame` uses. Nothing
  between the push and `RegisterRoot` can park.
- *Return.* `RTRN` moves the value into `D0` while the working stack still holds it
  (`fmov d0, xN` for an `Int`, `fmov d0, dN` for a `Float`; `0x9E670000 | (Rn << 5)` and
  the `FMOV (register)` encoding), then tests the entry-kind word: the native exit pops the
  record (`pos--`) and returns; the bridge exit is today's `ProcessReturn`. **A value the
  working stack does not hold** -- `Runtime->Copy` is `CPY_CHAR_ARY` and a return, so its
  result sits on the operand stack -- must be popped into `D0` by the native exit; the AMD64
  first build missed this and every `SubString` came back `Nil` once its callers compiled.
  `X0` keeps the status on both exits, so the guard stubs are unchanged. A method whose
  result is a func-ref, two words, keeps the bridge entry only.

- *The call site* (`EmitNativeCallSite`). Marshal the values into the outgoing area and off
  the working stack (`MarshalOutArgs`, the shape of `ProcessReturn` with the area as the
  base); the entry from the method's word (`ldar` -- ARM64 needs the acquire that x86 gives
  a plain load; the same for a site's `current` record) or from the site's inline cache
  (`JitVirtualSite`, `JitResolveVirtualSite`, `JitResolveFuncRefSite`, `FillSiteRecord` are
  in `common.h` and `jit_common.cpp`, shared; the receiver's class word or the func-ref word
  against the record's key); the depth check against `CALL_STACK_SIZE`; `X0`, `X1`, `X2`;
  `blr`; a negative status (`tbnz x0, #63`) to a block that calls `JitNativeCallError`
  (shared) with the status, the callee, and the caller's ids. The slow path copies the
  area onto the operand stack, runs today's bridge sequence and pops the result into `D0`,
  so both paths join with the value in one place; the result then goes to a pool register
  (`fmov xN, d0`). The slow path serves a callee not compiled yet (the trampoline counts and
  compiles it), a full call stack, a `Nil` receiver, and a cache miss the resolver cannot fill.

**What to verify, in this order.** `vm_jit_equiv.obs` byte-identical across `--jit=off`, the
default and `OBJECK_JIT_THRESHOLD=1` (its `Calls` probes cover bound, virtual, func-ref,
deep, allocating, `Float`, wide, `Nil`-result, callback-result and inlined-reference calls);
`OBJECK_JIT_REPORT=1` on it for fallbacks; `jit_native_call_error.obs` and
`jit_native_call_depth.obs` (the two exits), `jit_frame_unreferenced_local.obs`,
`inline_funcref_param.obs`; both regression passes with `./run_regression.sh arm64`;
`core_thread_gc_stress` compiled and pinned to a few cores, since the record's registration
order against the collector is the risk. Read one `_DEBUG_JIT_JIT` listing per step.

**Traps the AMD64 work found, all of which apply here.** Every `RTRN` emits its own epilogue
(patch every one). A callback-left return value at `RTRN` (above). The frame is laid out
from the declarations, not the references (#761, already on ARM64). A native call site
reads the receiver from the outgoing area, not the operand stack, so the `Nil` and
non-object checks move with it. `R11`/`X` scratch use in a call site is safe only after
every live value is spilled, which `ProcessStackCallback` does before anything else. The
first native prologue's `top` register must be held across both prologues so the one
`ProcessParameters` sees the same register from either entry.

**The register pool on ARM64 needs nothing.** It has fifteen general registers (`X0`-`X7`,
`X9`-`X15`) and fifteen float ones; the gap was the Linux x64 backend, whose four pool
registers and three aux ones let a method with a dozen locals fall to the interpreter --
closed on 2026-09-10 by giving it Windows' eight (`R8`-`R11` join the pool and are no
longer pushed).

## 5. Rules of the road

- Merges happen only on the maintainer's explicit word ("merge N when green"). The gate: the five
  build legs, `Tools (formatter, LSP, VS Code extension)` and `CI Status` all pass and nothing
  else fails; then `gh pr merge N --merge --delete-branch`. `gh pr merge --auto` merges at once
  here; never use it.
- PRs target `master`; a stacked PR gets no build legs.
- Design paragraph first; fixture probes that fail before the change; both regression passes;
  one commit per concern (fix, test, docs); a `CHANGELOG.md` entry per user-visible change under
  the v2026.9.1 section.
- Never a bare `git stash`, never a force-push of public history, never credential material in a
  file, a transcript or a workflow, never signing in a workflow.
- `.obs` files show as binary in `git diff`; use `git diff --text`.
- The agent cannot run `gh pr close`/`reopen` in auto mode; ask the maintainer.

## 6. Release v2026.9.1: what the Mac can do and what it cannot

`/release` is cloud-only and runs from any machine: the pre-flight gates
(`tools/cicd/check_release_config.sh`, the `api.zip` stamp), `update-docs` from the CHANGELOG
(README "What's New", `docs/readme.html`, `docs/readme.txt`, `docs/web/`, and the README badge,
Quick Start URLs and checkmark, which stay on the published version until the tag exists), the
tag, then GitHub Actions builds and publishes; macOS signing and notarization happen in CI.

Windows MSI signing does not: it needs the SafeNet token on the Windows box
(`tools/cicd/sign_release.cmd`, which rewrites the MSIs so `SHA256SUMS` is regenerated). A release
started from the Mac is complete only after that step runs there, so either tag when the Windows
box is reachable or accept unsigned MSIs until it is; the README no longer promises signing.

## 7. Map

- JIT: `core/vm/arch/jit/amd64/jit_amd_lp64.{h,cpp}`, `core/vm/arch/jit/arm64/jit_arm_a64.{h,cpp}`,
  shared `core/vm/arch/jit/jit_common.{h,cpp}`; the collector's root walk and `OBJECK_GC_TRACE` in
  `core/vm/arch/memory.cpp`; opcode handlers in `core/vm/dispatch.cpp`; `core/vm/loader.cpp`.
- Compiler: `core/compiler/intermediate.cpp` (`EmitBranch`, `EmitSelectJumpTable`),
  `core/compiler/optimization.cpp` (`CanInlineMethod`), `core/compiler/emit.h`.
- Docs: `docs/JIT_CODEGEN_ASSESSMENT_2026_09.md`, `docs/JIT_LOOP_LOCALS_DESIGN.md`,
  `docs/JIT_SELECT_TABLES_DESIGN.md`, `core/vm/arch/jit/README.md`, `CHANGELOG.md`.
- Tests: `programs/regression/vm_jit_equiv.obs` with `run_vm_flag_tests.py`,
  `jit_gc_safepoint.obs`, `jit_branch_shapes.obs`, `jit_native_inline.obs`,
  `core_bool_short_circuit.obs`; kernels in `programs/tests/jit_probe.obs`.
