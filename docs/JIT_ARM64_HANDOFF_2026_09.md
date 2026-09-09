# macOS handoff: the ARM64 JIT track (2026-09-09)

**Audience:** the next working session on the macOS ARM64 machine (maintainer plus agent).
**Why:** batches 1-5 of `JIT_CODEGEN_ASSESSMENT_2026_09.md` were built on a Windows x64 box,
where the ARM64 backend could only be cross-compiled; its runtime check was CI's three ARM64
legs, thirty minutes a round trip with a failure artifact as the only debugger. Everything
still open on the assessment's list is ARM64-heavy, and on the Mac the backend builds, runs and
sits under `lldb` in minutes.

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

What that leaves is the pattern, not the bug: **27 regression tests still carry `# JIT_DISABLE`**
(`grep -l '# JIT_DISABLE' programs/regression/*.obs`), each opting a test out of JIT coverage.
Two of them turned out to mask a real, since-fixed miscompile. The rest have never been audited.
The Mac can work through them the way #741 did -- drop the marker, run under
`OBJECK_JIT_THRESHOLD=1`, and either the test passes (the marker was stale, remove it) or it
exposes a live miscompile to chase under `lldb`. Each marker should end up with a stated reason
or a compiled twin; a JIT'd path with no coverage is where the next #722 hides. `jit_arm_a64.cpp`
is the place to look when one fails: the AMD64 `IMUL` operand-order bug of #721 and the stored
float compare of #660 were the same shape -- a caller-saved register that one backend spills
across a call and the other does not.

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

### 4d. F7, a native calling convention between compiled methods

Weeks, not days. Design document first, with the ABI (arguments in registers, VM operand stack
synced only at callbacks), the interaction with `RegisterRoot`/`CheckJitRoots`, and how a
callee that falls back to the interpreter is entered.

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
