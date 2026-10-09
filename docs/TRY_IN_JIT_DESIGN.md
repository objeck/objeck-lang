# `Try()` inside compiled code — design

Status: **design only, nothing built.** For
[#925](https://github.com/objeck/objeck-lang/issues/925), which asks for a design
before an implementation. Written 2026-10-08 against
`core/vm/arch/jit/jit_common.cpp`, `core/vm/interpreter.h` and both backends.

## The problem, stated as a user sees it

`Try()` — and `?->`, which is `Try()` on a chain — recovers from every runtime
error in interpreted code. #900 made that true for an invalid cast and a full
call stack. Inside JIT-compiled code both still end the program.

So **whether a guarded chain recovers depends on whether the failing method
happens to have been compiled**, which depends on how many times it has been
called. The same program, run twice, can recover once and abort once. That is the
part worth fixing: not that a case is missing, but that the language's error
semantics are not stable.

`programs/regression/try_recovers_cast_and_depth.obs` documents this and opts out
of the every-method-compiled pass for it. Flipping that opt-out is the acceptance
test for this work.

## What already exists, and is the reason this is tractable

A status protocol is **already in place** for one of the three paths, and the
design below extends it rather than inventing anything.

`JitRuntime::Execute` returns a `long`. A JIT guard stub sets a negative code and
returns it through the method's epilogue:

| status | meaning |
|---|---|
| -1 | dereference of a `Nil` instance |
| -2 | array index out of bounds |
| -4 | divide by zero |

The interpreter's bridge reads that status and, since #900, recovers from it —
which is why the test's own note says *"A compiled callee that reports a guard
stub's status does recover, since its frame has already returned."*

So compiled code can already report an error to the runtime and have a `Try()`
catch it. Two paths simply do not use that mechanism, and one cannot reach the
handler stack at all.

## The three gaps

### 1. A callback cannot see the handler stack

`try_handler_stack`, `try_handler_stack_pos`, `try_handler_call_stack_pos`,
`try_handler_pos` and `try_recovery_ip` are **instance fields** on
`StackInterpreter` (`core/vm/interpreter.h:112-119`). `PushTryHandler`,
`PopTryHandler` and `TryErrorRecovery` are instance methods.

`JitStackCallback` is `static` and receives `instr`, `cls_id`, `mthd_id`, `inst`,
`op_stack`, `stack_pos`, `call_stack`, `call_stack_pos`, `ip` — no interpreter.
`OBJ_INST_CAST` builds a *temporary* `StackInterpreter` purely to print a stack
listing, which is itself the clearest evidence the real one is unreachable.

**Two options.**

**(a) Make the handler stack `thread_local`.** A `Try()` is inherently
per-thread: a handler pushed on one thread must never catch an error on another,
and the current per-instance stack gets that right only because each thread runs
its own interpreter. Moving it to a `thread_local` struct keeps that property and
makes it reachable from any code on the thread, including a static callback.

**(b) Pass the interpreter through the callback.** Requires a new argument in the
emitted call sequence on **both** backends — amd64 and ARM64 — and a matching
change wherever the callback is emitted.

**Take (a).** It changes no generated code, which is the expensive and least
verifiable part of this work.

#### A thread DOES hold several interpreters, and that is a trap before it is a feature

This was the design's first open question, and it is now answered rather than
left for whoever implements it. `jit_common.cpp` constructs a
`Runtime::StackInterpreter` at **three** sites — lines 346, 404 and 462 — on the
same thread as the interpreter already executing below. One of them is inside
`StackCallbackBody` itself.

So a thread-local handler stack *would* be visible from the callback, which is
precisely what gap 1 needs. But **every one of the four constructors sets
`try_handler_pos = 0`** (`interpreter.h` lines 613, 634, 669, 706). With the
stack thread-local, constructing one of those temporaries would **wipe the live
handler stack** — a `Try()` active below would be destroyed the moment compiled
code took any callback, turning a working recovery into an abort. That is
strictly worse than today, and it would look like an unrelated intermittent
failure depending on which methods had been compiled.

So option (a) requires, as a non-optional part of the same change: the
thread-local is initialised **once per thread**, at the points a thread begins
executing Objeck code (`Execute`'s top-level entry and `AsyncMethodCall`), and
the four constructors stop zeroing it. That is a small edit, but it is the whole
reason this question had to be answered first — the obvious implementation of (a)
is a regression.

A cheap guard while implementing: assert in the two-argument constructor that the
thread-local is either empty or untouched, so a reset reintroduced later fails
loudly rather than becoming an intermittent abort.

The remaining cost is that the state moves off the object, so three things need
checking rather than assuming:

- **the debugger** (`_DEBUGGER` builds hold a `Debugger*` beside these fields)
  reads interpreter state; confirm it does not read the handler stack through an
  interpreter instance it does not own.
- **the collector** walks frames during promotion, and `TryErrorRecovery` pops
  frames and calls `ReleaseStackFrame`. Thread-local storage does not change
  that, but the MT-GC has a history here (`gc_multithread_fixes`,
  `mt_gc_residual_crash`), so the recovery path should be exercised under
  `--nursery` pressure with several threads, not only single-threaded.
(The third thing on this list was "establish whether a thread can hold two
interpreters". It can, it matters, and the answer is above.)

### 2. A callback cannot report an error

`StackCallbackBody` returns `void`, and so does `JitStackCallback`. `OBJ_INST_CAST`
therefore has nothing to return and calls `VmExit(1)`
(`jit_common.cpp:792-806`). The same shape appears at the call-stack bounds check
(`jit_common.cpp:199-202`).

**Give the callback a status, and reuse the guard-stub epilogue.**

1. `StackCallbackBody` returns `long` — `0` for success, a negative code from the
   table above for a failure. `OBJ_INST_CAST` returns a new code rather than
   exiting. `JitStackCallback` propagates it, including from its three `catch`
   clauses, which today call `BridgeExceptionExit`.
2. The emitted call site tests the returned status and, when negative, jumps to
   **the same epilogue a guard stub jumps to**. Nothing new is needed downstream:
   the method already returns that status, and the bridge already recovers from
   it.

Step 2 is the only generated-code change, and it is the same shape on both
backends: a compare against zero and a branch to an existing label. That is
deliberately the smallest possible footprint in the part that cannot be tested
locally on ARM64.

`BridgeExceptionExit` keeps its role for a genuine C++ exception escaping the
bridge, which is not a recoverable Objeck error and should not become one.

### 3. A JIT-to-JIT call does not propagate its callee's status

`EmitNativeCallSite` (and the bridge path at `jit_common.cpp:223`) calls
`JitNativeCallError` on a negative status, and that function **does not return** —
it prints and exits. So a compiled method calling a compiled method that fails
cannot recover even though the failing frame has already returned and the handler
is intact.

**Store the callee's status and jump to the caller's error epilogue**, so it
bubbles outward one compiled frame at a time until it reaches the bridge, where
recovery already happens. `JitNativeCallError` becomes the *unguarded* reporter —
called when the status reaches the top with no handler — rather than the
universal one.

Both backends. The amd64 side is verifiable here; the ARM64 side is not (see
below).

## What this does not attempt

**Compiled recursion that overruns the native C stack.** `#925` lists it, and the
status protocol above does not reach it: the process dies in the OS, with no
frame left to return a status from. The bounds check at
`jit_common.cpp:199` fires only for the *interpreter's* `call_stack[]`, and
compiled frames consume the real C stack long before 256 of them exist.
`bad_runtime_stack.obs` already documents this and opts out for it.

Recovering it needs a guard the compiled prologue emits — a stack-limit test per
frame — which is a different piece of work with a per-call cost, and should be
measured before being proposed. **This design covers the cast and the JIT-to-JIT
status; it leaves compiled recursion explicitly out, and the test's opt-out note
should keep saying so rather than being quietly reworded.**

## Verification plan

The acceptance test is flipping `try_recovers_cast_and_depth.obs`'s JIT opt-out —
but only for the cast half. Since the recursion half stays unrecoverable, the test
should be **split**, not partially flipped: a cast test that runs in every pass,
and a recursion test that keeps the opt-out with the reason it has now. A
partially-flipped single test would pass while covering less than it appears to.

Then, in order of what each actually establishes:

1. the cast recovers with every method compiled, which is what #925 asks for
2. the cast still **aborts** unguarded, with the same message and exit 1 —
   recovery must not become silent swallowing
3. a JIT-to-JIT call whose callee hits a guard stub recovers at the caller's
   handler, not just at the bridge
4. the above under `--nursery` pressure with several threads, because
   `TryErrorRecovery` pops frames and releases them while the collector may be
   walking them
5. `run_differential.py` across s0/s3 × JIT, since the point is that the two
   agree

**ARM64 cannot be verified here.** There is no ARM64 machine on this side, so the
backend half of gap 3 is CI-only — the `windows-11-arm` and `linux-arm64` legs. A
status propagation that is wrong there fails as an abort where a recovery was
expected, which the split cast test above would catch on those legs. Do not land
the ARM64 change without seeing those legs green; "it compiles" has been worth
little here before (`gl_macos_strict_glsl_linker`).

## Order of work

1. ~~establish whether one thread can hold two `StackInterpreter` instances~~ —
   **answered: it does, at three sites in `jit_common.cpp`, and all four
   constructors zero the handler stack.** See the trap above; the move to
   thread-local is only safe together with making initialisation per-thread.
2. move the handler stack to thread-local, initialise it once per thread, stop
   zeroing it in the constructors. No behaviour change, suite green, and the
   assert suggested above in place.
3. give `StackCallbackBody` a status; `OBJ_INST_CAST` returns instead of exiting
4. emit the status test at the callback site, **amd64 only**, and split the test
5. the same on ARM64, landing only on green CI legs
6. `EmitNativeCallSite` propagation, amd64 then ARM64
7. update `FEATURES.md`, `architecture.md` and the test notes, which all currently
   state the limitation as permanent

Step 2 is worth doing on its own: it is a prerequisite, it carries no
user-visible change, and it is where the one genuine hazard in this design lives.
Everything after it is additive.
