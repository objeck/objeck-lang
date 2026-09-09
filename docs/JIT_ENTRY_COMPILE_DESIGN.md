# JIT: compiling the entry method, and loops on their first call

**Status:** implemented on both backends (the change is in the interpreter's dispatch, so it
applies to AMD64 and ARM64 alike), 2026-09-09, batch 6 part 1. Not an item of
`JIT_CODEGEN_ASSESSMENT_2026_09.md`; found while measuring batch 4.

## 1. The problem

The auto-JIT is call-counted: a method is compiled once it has been called
`JIT_AUTO_THRESHOLD` (ten) times. Three kinds of method never get there:

- **`Main`.** The program's real entry is the loader's synthetic `$Initialization$` routine,
  which runs the class initializers and then calls `Main` exactly once.
- **A thread's `Run`.** The spawned thread's `Execute` enters it directly, once per thread.
- **Any once-called driver**: the method a script's `Main` calls to do the work.

A hot loop written in any of them ran in the interpreter for the life of the program --
roughly 50x slower than the same loop in a helper -- and the documented way out was to mark
a helper `native`, which the batch-4 inliner finding showed the compiler could then paste
back into the interpreted caller. The measurement: a 2e8-iteration loop, 0.13 s in a native
helper, 3.65 s in `Main`, 3.52 s in a thread's `Run`.

## 2. Two rules, one predicate

**`HasLoop(method)`** (`jit_common.h`): the method has a back-edge, a `JMP` whose target
index is not beyond its own. The compiler resolves labels to instruction indices, so this is a
linear scan with no label bookkeeping. A `JMP_TABLE` is not a loop.

1. **Entry compilation** (`StackInterpreter::Execute`). When the interpreter is entered from
   the top -- `jit_called` false, `ip` 0 -- and the method has a loop, `TryAutoJitCompile`
   runs before the first instruction. If it produced native code, the frame is handed to
   `JitRuntime::Execute` and, on return, released the way `ProcessReturn` releases an entry
   frame (`halt`, no frame to pop). A negative status is reported exactly as
   `ProcessJitMethodCall` reports it. A method the JIT rejects (try regions, frame-dependent
   traps) is interpreted as before. This covers a thread's `Run`, and `$Initialization$`
   itself, which has no loop and is skipped.

2. **First-call compilation for loops** (`StackInterpreter::CheckAutoJit`). A callee whose
   call count is zero and which has a loop is compiled at that first call, not the tenth.
   This is what reaches `Main`, and every once-called driver. Straight-line methods keep the
   counted threshold: compiling them early would only trade compile time for nothing.

Both rules are gated by **`JitEagerLoops()`**: the threshold is at or below its default. A
raised threshold means someone is steering the JIT away deliberately -- the LSP server runs
with `OBJECK_JIT_THRESHOLD=999999999` to keep it off, which is not the `DISABLED` sentinel --
and both rules step aside for it. `OBJECK_JIT_DISABLE=1` and `--jit=off` disable them too.

## 3. The bug the first rule exposed

`PatchCallSites` rewrites every `MTHD_CALL` that targets a freshly compiled method to
`MTHD_CALL_JIT` by walking the program's classes. `$Initialization$` belongs to no class, so
its one call to `Main` was never rewritten, and `ProcessMethodCall` dispatched on the opcode
alone: a freshly compiled `Main` was sent to the interpreter for its one and only call, and
the first attempt at rule 2 measured 3.8 s. The dispatch now takes the JIT path when the
callee has native code, whether or not the site was rewritten. `ProcessJitMethodCall`
re-checks `GetNativeCode()` itself, so this cannot send an uncompiled callee to native code.

## 4. What it does not change

- The compiler's inliner still does not inline into `Main`: a loop-free `Main` is still
  interpreted, and inlining a `native` callee into it would hide the callee's request
  (batch 4). With a compiled `Main` that exemption is now only a policy choice.
- `OBJECK_JIT_REPORT=1` names an entry compile: `<method>: compiled on entry (has a loop)`.
- The `# JIT_DISABLE` regression tests set `OBJECK_JIT_DISABLE=1`, so they stay interpreted.

## 5. Verification

- `jit_entry_compiled.obs`: the same loop in a native helper, in `Main`, and in a thread's
  `Run`; each must run within a factor of ten (plus half a second) of the helper. Before:
  0.13 s / 3.65 s / 3.52 s, fail. After: 0.083 s / 0.084 s / 0.129 s, pass. With
  `OBJECK_JIT_THRESHOLD=999999999` it fails again on purpose, both loops interpreted.
- `vm_jit_equiv.obs` byte-identical across `--jit=off`, the default (its `Main` now
  compiled) and `OBJECK_JIT_THRESHOLD=1`; flag tests 22/22.
- The regression suite in both modes: the normal pass is the new coverage, since every
  loop-bearing `Main` in it now runs compiled.
