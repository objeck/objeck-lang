# JIT: what a compiled call costs, and the convention that removes it (F7)

**Status:** complete on AMD64 (sections 6 to 11: the bridge, the callee's entry and exit, the direct native call, inline caches for `virtual` and func-ref calls, the register-argument entry). ARM64: phase 1 (the bridge) and phase 2 (the callee's entry and exit, section 12) done on 2026-09-10; phase 3 and the native entry are open, and section 12 says where to start. F7 of `JIT_CODEGEN_ASSESSMENT_2026_09.md`
("every method entry/exit rebuilds the VM operand-stack view, and JIT-to-JIT calls still marshal
arguments through the VM stack"), measured here for the first time, on Windows x64 at master
`26784fe1ab`. The fixture is `programs/tests/jit_call_probe.obs`.

## 1. The measurement

Five kernels, 20M iterations (`VirtualCall` 2M, `Fib(32)` is about 7M calls), medians of three,
`--jit=off` against the default JIT. Every kernel is a once-called method with a loop, so the
default threshold compiles it on entry; the callees compile after ten calls.

| kernel | what the loop does | interpreter | JIT | JIT vs interpreter |
|---|---|---|---|---|
| `InlinedCall` | `Small(acc, i)`, which `obc` inlines: no call | 0.769 s | 0.015 s | 51x |
| `RealCall` | `a->Add(i)`, a field-touching callee the inliner refuses | 1.495 s | 0.531 s | **2.8x** |
| `NoCall` | the body of `Add` written inline | 0.478 s | 0.011 s | 43x |
| `VirtualCall` | `s->Area(i)` through a `virtual` declaration | 0.133 s | 0.252 s | **0.5x** |
| `Fib(32)` | recursion, which is never inlined | 0.384 s | 0.207 s | 1.9x |

`RealCall` minus `NoCall` is the call:

| | per call |
|---|---|
| interpreted caller, interpreted callee | 51 ns |
| compiled caller, compiled callee | 26 ns |
| compiled caller, `virtual` callee | 126 ns (the interpreter does it in 66 ns) |

So the JIT halves a call, and everything else it does to a loop is worth forty times more. A
call-bound method -- a recursive function, a loop over an accessor, a visitor, anything
object-oriented -- gets 2-3x from the JIT where a loop of arithmetic gets 30-80x. And a
compiled caller of a `virtual` method is slower than the interpreter, on every call.

The assessment's `CallLoop` kernel showed none of this because `obc` inlined its callee; that is
what `InlinedCall` reproduces above, and why the fixture's other callees are shapes the inliner
refuses (`CanInlineMethod`: a field access, recursion, a `virtual` target).

## 2. Where the 26 ns go

From the `_DEBUG_JIT` listing of `r := a->Add(i)` (AMD64, Windows). The callee's own work is
fourteen instructions. Around them:

**The call site, about 40 emitted instructions.** `FlushLocalCache`, spill of any live
non-pinned temporaries to the `TMP_REG` slots; the operand-stack top computed from the frame's
`OP_STACK` and `STACK_POS` slots (five instructions), then each argument pushed with a reload of
`STACK_POS`, a store, an `inc [stack_pos]` and an `add` (five per argument); four argument
registers loaded with the opcode, the `StackInstr*`, `CLS_ID` and `MTHD_ID`; six frame slots
pushed as stack arguments, the shadow space, a `movabs` and an indirect `call`; afterwards the
`INSTANCE_MEM` reload from `frame->mem[0]` (three) and the return value popped from the operand
stack (seven: the same top-of-stack computation again).

**The bridge, `JitStackCallback`, C++.** A `switch` on the opcode; `GetClass()->GetMethod()`;
the `native_code` acquire load; the auto-JIT count check; the instance popped; the
`CALL_STACK_SIZE` check; `GetStackFrame`: a critical section, a pop from the shared frame cache,
six field writes, leave; the call-stack store, a release fence, the increment;
`JitRuntime::Execute`: an eleven-argument call, five of them through the stack; then the
decrement and `ReleaseStackFrame`: the critical section again, a 768-byte `memset` of the frame's
`mem` (`LOCAL_SIZE`, which a compiled callee used one word of), the push, leave; the status
check.

**The callee's entry and exit, about 90 executed instructions.** The prologue: frame setup, five
pushes, `R12` and its alignment filler, then 96 bytes of `XMM10`-`XMM15` saved with six `movdqu`
-- for a method that never touches a float; `R12` re-materialized; the four register arguments
spilled to their slots; `RegisterRoot`: the native local area's address stored through the
`jit_mem` pointer, `jit_offset` stored, and a `loop`-instruction zeroing loop over eleven words
(the declared locals plus the ten spill slots); `ProcessParameters`: eight instructions per
parameter to pop it from the operand stack into its slot (the top-of-stack computation once
more); at `RTRN` nine instructions to push the result onto the operand stack; the epilogue: the
six `movdqu` restores, the pops, `ret`.

About 300 instructions and two lock acquisitions per call, for fourteen of work. ARM64 is the
same shape: `ProcessStackCallback` marshals through `X0`-`X7` plus two stack slots, the prologue
stores eleven incoming arguments and eight `D` registers, `RegisterRoot` runs the same zeroing
loop.

**The 126 ns of a `virtual` call.** The bridge looks the callee up by the call site's operands,
which name the `virtual` declaration (`Shape:Area`), not the override. That method has no
native code and never will -- the auto-JIT counts only concrete targets, in `CheckAutoJit` --
so the bridge takes its other path: it constructs a `StackInterpreter` on the caller's call
stack (a heap allocation and `AddPdaMethodRoot` under the global `pda_frame_lock`), and
`Execute`s the caller's `MTHD_CALL` instruction in the interpreter, which acquires a frame for
the *caller*, resolves the override through `ResolveVirtualMethod`, and finds it compiled, so it
acquires a second frame and enters native code through `ProcessJitMethodCall`; on return the
interpreter object's destructor takes the lock again to unregister, and frees. Two frames, a
root registration and a heap round trip per call. It is also the only thing keeping the direct
path from ever calling a `virtual` declaration's empty body: nothing checks `IsVirtual()` there.

## 3. The design, in three phases

Each phase is a PR against `master`, measured on the fixture before and after, and useful on
its own. The first two are days; the third is the convention itself.

### Phase 1: the bridge (C++ only, both backends, about two days) -- done, section 6

1a. **Resolve `virtual` callees in the bridge and take the direct path.** When
`callee->IsVirtual()`, resolve through the receiver's class (`MemoryManager::GetClass(inst)`,
then the per-class virtual-method cache `ResolveVirtualMethod` already fills; factor that lookup
out of the interpreter so both share it), and continue with the concrete method. The `virtual`
case also becomes an explicit check rather than an accident of counting. Expected: 126 ns to
about 26 ns per virtual call, and no case where compiled code is slower than interpreted.

1b. **A frame pool without the lock or the memset.** A thread-local free list (frames are
acquired and released on the same thread; a global list backs the first fill), and zeroing on
acquire sized to the user: an interpreted callee needs its declared slots
(`GetNumberDeclarations()` words plus the instance word), a compiled callee needs `mem[0]` only,
its locals being native-stack slots that `RegisterRoot` zeroes. The interpreter's contract --
which slots it reads before writing -- is checked before the size is trusted. Expected: 5-8 ns.

1c. **A direct-call bridge entry for statically resolved sites.** For `MTHD_CALL` whose callee
is non-virtual, the emitter passes the `StackMethod*` as an immediate to a
`JitDirectCall(callee, inst, stacks...)` entry: no opcode `switch`, no class/method lookup. The
auto-JIT count stays in the trampoline path, which uncompiled callees still take. Expected: 2 ns.

### Phase 2: the callee's entry and exit (codegen; AMD64 here, ARM64 on the Mac; about three days) -- done on AMD64, section 7

2a. **Save `XMM10`-`XMM15` only in methods that use the float pool.** The pre-scan knows; a
flag on the compile selects the prologue and epilogue variant. Twelve `movdqu` and 192 bytes of
traffic gone from every integer method. Windows only by construction; the ARM64 `D8`-`D15`
stores and loads are the analogue.

2b. **`RegisterRoot`'s zeroing.** The `loop` instruction is microcoded; eleven iterations cost
more than the whole body. Straight `mov qword [reg+k], 0` for up to sixteen words, `rep stosq`
above that (ARM64: `stp xzr, xzr` pairs).

2c. **Keep the operand-stack pointers in registers across `ProcessParameters` and
`ProcessReturn`.** The top-of-stack address is recomputed from the frame per parameter; computed
once, each parameter is a load and a store, and the return push is four instructions instead of
nine.

Expected: the callee's overhead from about 90 instructions to about 45.

### Phase 3: the convention (AMD64 two to three weeks; ARM64 after) -- done on AMD64, sections 8 to 11

**The call site.** For a `MTHD_CALL` whose callee is non-virtual:

```
mov  rax, [&callee->native_code]     ; the atomic field's address is an immediate;
test rax, rax                        ; a plain load is an acquire on x86, ldar on ARM64
jz   bridge                          ; not compiled (yet): today's path, which counts
cmp  dword [rbp+CALL_STACK_POS], CALL_STACK_SIZE
jge  bridge                          ; the depth check the bridge does today
<args>                               ; self in RCX/RDI/X0, then the ABI's integer and
                                     ; float registers, overflow on the stack
lea  r10, [rbp]                      ; the caller's frame as context
call [rax + NativeCode::code]        ; or the code pointer cached on StackMethod: one load fewer
mov  rdx, [rbp+MEM]; mov rdx,[rdx]; mov [rbp+INSTANCE_MEM], rdx    ; the reload rule, unchanged
```

The caller's live temporaries spill to the `TMP_REG` slots around the call exactly as around a
callback; they lie inside `jit_offset`, so the collector scans them. The pinned loop locals
(`R13`-`R15`, `XMM6`-`XMM9`; ARM64 `X19`+ and `D8`-`D15` when the ARM64 pins land) are
callee-saved and the callee's prologue preserves them, so a pinned loop calls without spilling.

**The callee's second entry.** Each compiled method gets two prologues over one body: the
existing *bridge entry* (operand-stack arguments, status in `RAX`, result pushed to the operand
stack; how the interpreter and the bridge enter it) and a *native entry* that builds the same
frame layout but takes `OP_STACK`, `STACK_POS`, `CALL_STACK` and `CALL_STACK_POS` from the
caller's context pointer (they are per-thread constants), stores the register arguments straight
into the zeroed local slots, and records `ENTRY_KIND = native` in a new frame slot. It pushes
the method's `StackFrame` itself: a per-thread array `frames[CALL_STACK_SIZE]` indexed by
`call_stack_pos`, so the push is inline stores (`method`, `mem[0] = self`, `jit_mem`,
`jit_offset`, `call_stack[pos] = frame`, then `pos++` -- the slot store before the increment, as
`PushFrame` orders them, with `stlr` on ARM64). Code size grows by the second prologue, about
fifty instructions per method.

**Return.** `RTRN` tests `ENTRY_KIND`: native returns the value in `RAX`/`XMM0`, decrements
`call_stack_pos`, and skips the operand-stack push and the status; bridge does what it does
today. The caller's `ProcessReturnParameters` takes the register instead of popping.

**Errors.** The nil, bounds and division stubs check `ENTRY_KIND`; for a native entry they call
`JitCompiler::JitDirectCallError(status, method)`, which reports the way the bridge's
JIT-to-JIT path reports today and exits. Today's path also exits without recovery on that
route, and methods with try regions are not compiled, so nothing observable changes.

**What the collector sees.** The callee's frame is on the call stack, with `jit_mem` and
`jit_offset` set, before any instruction that can park; the arguments go from registers into
zeroed slots inside that area; the caller's temporaries are in its own scanned slots; `self` for
both is in `frame->mem[0]`. Every rule from #746 holds: the call is not a park point, the
callee's loops poll as before, and both sides reload `INSTANCE_MEM` after anything that can
park -- the caller after the call (the callee may have promoted the caller's `self`), the
callee after its callbacks and safepoints. `CheckJitRoots` and `OBJECK_GC_TRACE` need no change:
the frame they read is the same struct with the same fields.

**Auto-JIT.** A native site that finds no code falls to the bridge, whose trampoline counts and
compiles; the next call finds the pointer. No `PatchCallSites` involvement for compiled callers,
no opcode rewrite: the site patches itself by reading the field.

**Not direct, still through the bridge:** `DYN_MTHD_CALL` (the target is a runtime word; a
table from the packed ids to `StackMethod*` would make it direct later), `virtual` callees (1a
makes them a bridge call; an inline cache keyed on the receiver's class is the follow-up), and
callees the JIT rejects.

Expected: a call at 4-6 ns; `Fib(32)` from 0.21 s to about 0.04 s; `RealCall` from 0.53 s to
about 0.10 s.

## 4. Verification, per phase

- The fixture, both modes, before and after, in the PR body.
- `vm_jit_equiv.obs` probes, byte-identical across `--jit=off`, the default and
  `OBJECK_JIT_THRESHOLD=1`: live temporaries across a call (spill and restore); float arguments
  and returns; more than four integer arguments, and mixed; a call from a pinned loop (integer
  and float pins survive); a young receiver promoted during the callee (`--gc-threshold` small,
  the callee allocates); recursion to depth 300 (the overflow message, unchanged); a callee that
  divides by zero (the message, unchanged); `virtual` and non-virtual targets mixed on one
  receiver; a func-ref call; a `Nil` receiver.
- `OBJECK_JIT_REPORT=1` on the fixture: every kernel and callee compiled, no fallback.
- Both regression passes on an untouched tree.
- `core_thread_gc_stress` compiled and pinned to four cores (the #746 recipe), since frame
  registration order against the collector is the risk in phase 3.
- The `_DEBUG_JIT` listing of `Add`, read once per phase, and the instruction count recorded.

## 5. Seen on the way, not F7

- An `Int` field store pays the write barrier's fast-path test (four instructions: load the
  header, mask, compare, branch). `STOR_CLS_INST_INT_VAR` cannot tell an integer from a
  reference, but the class's declarations can (`INT_PARM` against `OBJ_PARM`), and the emitter
  has the class. Separate, small.
- `StackFrame::jit_inst_mem` is declared and never used.
- The direct path's protection against calling a `virtual` declaration's empty body is that
  abstract methods are never counted. 1a makes it a check.
- `ResolveVirtualMethod` cached an inherited override on the class the name walk ended at,
  not on the receiver's class that `GetVirtualMethod` is asked about, so a receiver whose
  override lives on a parent missed the cache on every call. Fixed with 1a, since the walk
  moved into a shared function. The cache itself (`AddVirutalMethod`, an `unordered_map`
  insert) is not thread-safe, on the interpreter's path as much as the bridge's; a miss is
  once per (class, site), so the window is small, and it is not addressed here.

## 6. Phase 1, implemented (2026-09-10)

What landed, in `core/vm/interpreter.{h,cpp}` and `core/vm/arch/jit/`:

- **1a.** `JitStackCallback` resolves a `virtual` callee through the receiver's class
  (`StackInterpreter::ResolveVirtualTarget`, factored out of the interpreter's cold path)
  before deciding how to call it, and `CallCompiled` refuses a `virtual` declaration
  outright. A Nil receiver still falls through to the interpreter, which reports it.
- **1b.** The frame pool is a `thread_local` free list; no critical section. A frame is
  zeroed on acquire to `mem_size + 2` words (the instance word, the and/or slot and the
  method's declared local space -- what `StackMethod::NewMemory` allocates and what
  `LOAD_LOCL_*` can address) instead of the whole `LOCAL_SIZE` buffer on release.
  `Clear()` and the `FRAME_CACHE_SIZE` prefill are gone; a thread fills 64 frames at a
  time on demand and frees them when it ends.
- **1c.** `JitCompiler::JitDirectCall`: the emitters pass the `StackMethod*` in place of the
  opcode for a `MTHD_CALL` bound at compile time, on both backends, so the bridge neither
  switches nor looks up. The register layout is `JitStackCallback`'s, so the emitted call
  sequence is unchanged apart from two immediates.

Measured against master `2cf5500722` built with the same MSBuild invocation, run
alternately on the same box, medians of three (`programs/tests/jit_call_probe.obs`):

| kernel | master, JIT | phase 1, JIT | master, interpreter | phase 1, interpreter |
|---|---|---|---|---|
| `InlinedCall` (no call) | 0.015 s | 0.015 s | 0.735 s | 0.619 s |
| `RealCall` | 0.533 s | **0.340 s** | 1.621 s | **1.144 s** |
| `NoCall` | 0.011 s | 0.011 s | 0.467 s | 0.382 s |
| `VirtualCall` (2M) | 0.250 s | **0.040 s** | 0.139 s | **0.096 s** |
| `Fib(32)` | 0.221 s | **0.126 s** | 0.387 s | **0.249 s** |

Per call, each binary's `RealCall` minus its own `NoCall`:

| | master | phase 1 |
|---|---|---|
| compiled caller, compiled callee | 26 ns | **16.5 ns** |
| compiled caller, `virtual` callee | 125 ns | **20 ns** |
| interpreted caller, interpreted callee | 58 ns | **38 ns** |

The interpreter gains too, because its calls cross the same pool. (The no-call interpreted
kernels also moved by about 15% between the two builds; that is code layout, not this
change, and it is why the per-call figures are differences within one binary.)

Verified: the flag tests 22/22, with `vm_jit_equiv.obs` byte-identical across `--jit=off`,
the default and `OBJECK_JIT_THRESHOLD=1` including three new probes (a mixed
virtual/non-virtual receiver set with an inherited override; a method that declares locals
without assigning them, called right after one that dirtied twelve; recursion past the
pool's refill size); the regression suite in both modes, 224 passed, 3 skipped, 0 failed
each. ARM64 is the same C++ plus a two-immediate change in its emitter; its runtime check
is CI's three ARM64 legs.

Phase 2 followed the same day (section 7), and the first step of phase 3 after it (section 8).

## 7. Phase 2, implemented on AMD64 (2026-09-10)

What landed, in `core/vm/arch/jit/amd64/jit_amd_lp64.{h,cpp}`:

- **2a.** The prologue's `XMM10`-`XMM15` save and the epilogue's restore are emitted as
  before, and `Compile()` patches both into a two-byte jump over themselves when the method
  never took a register from the XMM pool (`xmm_pool_used`, set in `GetXmmRegister`, the only
  source of those registers). Each `RTRN` emits its own epilogue, so a method with several
  returns has several restore blocks; all of them are recorded and patched with the one save
  -- the first build patched only the last one and an early return then ran a restore for a
  save that never happened, with the stack pointer 96 bytes off. Windows only; POSIX has no
  block.
- **2b.** `RegisterRoot` zeroes the frame with straight `mov qword [reg+k], 0` stores for up to
  24 words; the microcoded `LOOP` stays for larger frames.
- **2c.** The operand-stack pointer arithmetic is hoisted: `ProcessParameters` computes the
  top once and reads each argument at a displacement below it, dropping them all with one
  subtraction (eight instructions per parameter to three, plus four of setup);
  `ProcessReturn` stores at a running displacement and bumps the count once (it reloaded the
  count pointer, incremented and advanced the base per value); the result pops load through
  a new scaled-index encoder (`move_base_index_reg`, `move_base_index_xreg`) instead of
  shift-and-add, seven instructions to five.

From the tracing listing of `r := a->Add(i)`: the callee executes about 60 instructions
outside its 13-instruction body where it executed about 95, and the call site 30 where it
executed 39. Alternated with the same master and phase 1 binaries as section 6, medians of
three:

| kernel | master | phase 1 | phase 2 |
|---|---|---|---|
| `RealCall` | 0.546 s | 0.337 s | **0.299 s** |
| `VirtualCall` (2M) | 0.251 s | 0.039 s | **0.036 s** |
| `Fib(32)` | 0.208 s | 0.124 s | **0.115 s** |
| compiled call, per call | 26.7 ns | 16.3 ns | **14.4 ns** |

Two nanoseconds for a third fewer instructions: what is left of a call is the C++ bridge --
the frame from the pool, the eleven-argument entry, the release -- and the operand-stack
traffic itself, which is phase 3's business. The interpreter is untouched by this phase.

Verified: the flag tests 22/22, `vm_jit_equiv.obs` byte-identical across the three modes; the
regression suite in both modes (see the PR); a one-method probe with an early return, which
the first build crashed on. The suite also caught a bug that predates this work: the char
store's `R8`-`R15` path released its element register twice, reachable whenever the element
address landed in an extended register and made common by the new result pop's allocation
order (fixed alongside, with a probe that stores constant characters and bytes right after
call results). ARM64 is untouched: its `D8`-`D15` saves, zeroing loop and
operand-stack sequences are the same shape and the same change, on the Mac.

## 8. Phase 3, first step: the direct native call (AMD64, 2026-09-10)

Section 3's phase 3 has two parts: removing the C++ bridge from a compiled-to-compiled call,
and passing arguments in registers. The first is done, and it is where the time was; the
second is measured below as what remains.

**What a bound call emits now** (`EmitNativeCallFastPath`, in `ProcessStackCallback`). The
arguments and the receiver go onto the operand stack as before and live temporaries spill to
their `TMP` slots as for a callback. Then:

- `rax = callee->native_entry` (a new word on `StackMethod`, published with `native_code`);
  null means "not compiled yet" and the code falls to the bridge sequence, which counts the
  call and compiles the callee in time. No call-site patching: the next call reads the word.
- The call stack's depth is checked inline; a full stack falls to the bridge, which reports it.
- The receiver is popped. An area is reserved below the stack pointer holding the callee's
  `StackFrame` record and its two-word `mem` (`self`, 0), the seven stack arguments the
  bridge entry expects (Windows; five on POSIX), and the shadow space. The record is filled
  as `GetStackFrame` fills a pool frame (`method`, `mem`, `ip = -1`, `jit_called`, the JIT
  fields zero) and pushed on the call stack, slot before count as `PushFrame` orders them.
- The register arguments are what the bridge passes: the callee's class and method ids and
  its class memory are constants of the callee, the receiver is in its register. `call rax`.
- A negative status goes to `JitNativeCallError`, which reports as the bridge did and exits.
  Otherwise the record is popped, the area freed, and the code joins the bridge path's tail:
  the spilled registers come back, `INSTANCE_MEM` is reloaded from `frame->mem[0]`, the result
  is popped.

The callee is unchanged: the same prologue, the same `RegisterRoot` writing `jit_mem` and
`jit_offset` through the pointers it was given (now into the record on the caller's stack),
the same `RTRN`. The collector scans the record like any bridge frame; the record lives
exactly as long as the call. Nothing between the push and the callee's `RegisterRoot` can
park, so the record is never scanned half-built -- the bridge had the same window.
`virtual` callees and func-ref calls still take the bridge (an inline cache is the follow-up).

Two regression tests cover the two exits: `jit_native_call_error.obs` (a directly called
callee dereferences Nil; the message names callee and caller) and
`jit_native_call_depth.obs` (recursion past the call stack's limit, refused by the bridge).

Alternated with the same master, phase 1 and phase 2 binaries, medians of three:

| kernel | master | phase 1 | phase 2 | phase 3, step 1 |
|---|---|---|---|---|
| `RealCall` (20M calls) | 0.542 s | 0.345 s | 0.315 s | **0.151 s** |
| `Fib(32)` | 0.208 s | 0.125 s | 0.115 s | **0.071 s** |
| `VirtualCall` (2M, still the bridge) | 0.257 s | 0.040 s | 0.037 s | 0.036 s |
| compiled call, per call | 26.5 ns | 16.6 ns | 15.2 ns | **7.0 ns** |

The call-bound loop that the JIT sped up 2.8x on master is now sped up 7.5x (interpreter
1.14 s against 0.151 s); the same loop without the call is 43x. What is left per call is the
operand-stack traffic (four stores and two loads for one argument and its receiver, the
result's store and load) and the callee's own entry and exit (section 7), about 50
instructions on each side: the register-argument entry of section 3 is the next step, and
`virtual` calls through an inline cache the one after.

Verified: the flag tests 22/22, `vm_jit_equiv.obs` byte-identical across the three modes;
the two new tests; the regression suite in both modes (see the PR). The POSIX variant of the
sequence (System V registers, five stack arguments, no shadow space) is built and run in
WSL. ARM64 keeps the bridge until the same is done there.

## 9. Phase 3, second step: an inline cache for `virtual` calls (AMD64, 2026-09-10)

A `virtual` call site cannot bind its callee at compile time: the target depends on the
receiver's class. Section 8 left those calls on the bridge at 18 ns. Each such site now owns a
small record set (`JitVirtualSite`, on the caller's `NativeCode`): up to four records of
receiver class, resolved target, its entry address, class memory and ids, plus the word
`current` that points at the record for the class seen last.

**What the site emits.** The receiver is read from the top of the operand stack; a Nil
receiver or a non-object (the header's type word is not `NIL_TYPE`) goes to the bridge, which
reports it. Its class word (`SIZE_OR_CLS`) is compared with `current->cls`: a hit loads the
entry from the record and runs section 8's body with the record in `RBX` (callee-saved, so
the body and the callee both leave it alone) supplying the target, its ids and class memory
where the bound call used immediates. A miss calls `JitResolveVirtualSite`, which resolves the
override through the receiver's class as the interpreter does, fills a record for it if the
target has native code and the site has a record free, publishes it as `current` and returns
it, so the check repeats and hits; a null return -- no native code yet, a non-object, or a
site that has seen more classes than it holds -- takes the bridge, which resolves and counts
as before.

**Consistency.** A record is written once, under the site's spin flag, and published only by
the `current` word (a release store; the load in compiled code is an aligned word read,
acquire on x86), so a hit reads one consistent record. A class that returns after another has
displaced it gets its existing record republished rather than a new one, so an alternating
site never grows; a site that exhausts its four records is megamorphic and stays on the
bridge. The resolver runs on the calling thread without allocating or parking.

Alternated with the step-1 binary, medians of three:

| kernel | step 1 | step 2 |
|---|---|---|
| `VirtualCall` (2M calls, monomorphic) | 0.036 s | **0.015 s** |
| `RealCall`, `Fib(32)` | 0.151 s, 0.072 s | unchanged |
| virtual call, per call | 18 ns | **7.5 ns** |

A `virtual` call now costs what a bound one does. `vm_jit_equiv.obs` gains a site that sees
six classes in rotation beside a monomorphic one (records filled, reused as the class
alternates, then exhausted), byte-identical across the three modes.

## 10. Phase 3, third step: the same cache for func-ref calls (AMD64, 2026-09-10)

A func-ref call (`f(x)` where `f` is a function reference or a closure) names its target
at run time by the packed word on the operand stack (class id and method id). The site
keeps the same inline cache as a `virtual` site, keyed by that word instead of the
receiver's class: the word is read from the top of the operand stack, compared with the
current record's key, and on a hit the record's entry runs through section 8's body with
one difference, the pop takes two words (the func-ref word and the instance below it, which
may be Nil for a plain function and is passed through as the callee's `self`). A miss calls
`JitResolveFuncRefSite`, which looks the target up by the word's ids and fills a record
through the shared `FillSiteRecord`. The patched opcode `DYN_MTHD_CALL_JIT`, which the
interpreter writes at a site when a callee with matching operands compiles, is the same
call; the emitter had no case for it and a method holding one failed to compile.

Measured on a kernel of five million iterations, each a bound call to a helper that makes
one func-ref call (the helper kept out of the inliner, see below), medians of three:

| kernel | step 2 | step 3 |
|---|---|---|
| one reference throughout | 0.181 s | **0.108 s** |
| two references alternating | 0.181 s | **0.133 s** |

About 15 ns per func-ref call, the bridge's share, are gone; the alternating site pays a
short resolver call each time its key changes, and never grows.

Found on the way: a method that calls a func-ref **parameter** goes wrong once the compiler
inlines it into a caller other than `Main` -- the interpreter loops forever, the JIT returns
garbage, on master too (#763). The suite never saw it because the one test with that shape
keeps its loop in `Main`, which the inliner leaves alone. The fixture's probe keeps its
helper non-inlinable with a second `return`.

## 11. The register-argument entry (AMD64, 2026-09-10)

Section 3's last item, built after sections 8 to 10 had taken the bridge out of the call.
The first version of this section argued it was not worth its risk -- the estimate was one
to two nanoseconds of the seven -- and that estimate was right for a bound call and wrong
for the two cases that matter most, recursion and func-ref calls (the table at the end).

**Two entries over one body.** A compiled method now begins with two prologues
(`EmitBridgePrologue`, `EmitNativePrologue`) that meet at `RegisterRoot`. The *bridge entry*
at offset 0 is unchanged as an interface: `JitRuntime::Execute` calls it with the eleven
values of `jit_fun_ptr`, the arguments on the operand stack and a frame record the
interpreter made. The *native entry* is what a compiled caller calls: `self` and the
caller's frame pointer in the first two argument registers (`RCX`/`RDX`; `RDI`/`RSI`), the
arguments in the caller's outgoing area, and nothing else -- the method's ids and class
memory are its own constants, the operand stack and call stack pointers are copied from
the caller's frame (they are per-thread constants), and the frame record is built in the
callee's own frame. `StackMethod::native_entry`, which section 8 introduced as the address a
compiled caller reads, is this entry now (null for a method that has none, so every native
site falls to the bridge for it); `NativeCode::code` stays the bridge entry.

**Where the arguments go, and why not registers.** Section 3 sketched the ABI's integer and
float registers with overflow on the stack. What was built puts every argument in memory:
the caller's outgoing area, reserved once per method below its prologue's stack pointer
(`out_area`, sized for the widest site), holds a callee's register homes and stack slots
where the bridge entry has them and then the arguments, the receiver and (a func-ref call)
the func-ref word, in operand-stack order. From the callee, that is a fixed offset
(`NATIVE_ARGS` from its frame pointer plus its parameter words), so `ProcessParameters` is one
sequence for both entries: each prologue sets a `top` register -- the operand stack's top,
or the end of the area -- and the loads below it are the same code. The callee stores every
parameter into a frame slot anyway; a store the callee's load picks up by forwarding costs
about what a register move does, and one parameter path serves both entries with no
per-type register assignment, no overflow rules and no second `ProcessParameters` over the
same instructions (which would have had to reproduce the first one's register state). The
bridge prologue drops the arguments from the operand stack's count before `RegisterRoot`,
which is safe because nothing between the drop and the stores can park.

**The frame record.** A block below the method's locals (`rec_base`: the `StackFrame`, its
two `mem` words `(self, 0)`, and one word saying which entry was taken) rather than the
per-thread array section 3 proposed. The native prologue fills it as section 8's caller did
on its own stack, points `JIT_MEM`, `JIT_OFFSET` and `FRAME_MEM` into it, and pushes it on the
call stack (the slot store before the increment, as `PushFrame` orders them). The collector
sees what it saw in section 8: a record with `jit_mem` set by `RegisterRoot` before any
instruction that can park, `self` in `mem[0]`, the locals walked by declaration. The copies
in the outgoing area are dead once the callee has stored its slots, and no safepoint lies
between the caller's stores and the callee's; the slow path copies them onto the operand
stack, where they are roots as before.

**Return.** `RTRN` puts the value in `XMM0` -- the `Int`'s bits or the `Float` -- while the
working stack still holds it, then tests the entry kind: the native exit pops the record and
returns; the bridge exit pushes the value on the operand stack as before. A value the
working stack never held is one a callback left on the operand stack -- `Runtime->Copy` is
`CPY_CHAR_ARY` and a return, so its result sits where the bridge exit wants it -- and the
native exit pops that into `XMM0` instead (the first build did not, and every `SubString`
came back `Nil` once its callers were compiled: the fixture's `CopyResults` probe). `RAX`
keeps the status on both exits, so the guard stubs are unchanged and a native caller tests
it after the call, going to `JitNativeCallError` for a negative one. The caller `movq`s the
result into a pool register. A method whose result is a func-ref, two words, has the bridge
entry only, and a site whose callee returns one takes the bridge.

**The call site** (`EmitNativeCallSite`). The values go into the outgoing area
(`MarshalOutArgs`) and off the working stack; then the entry -- from the method's word for a
bound callee, from the site's inline cache for a `virtual` or func-ref one, with the
receiver's class word or the func-ref word now read from the area rather than the operand
stack -- the depth check, `self` and the frame pointer into registers, the call, the status
test. The slow path copies the area onto the operand stack, runs the bridge sequence and
pops the result into `XMM0`, so both paths join with the value in the same place. The
sections 8 to 10 machinery that built the callee's record on the caller's stack and passed
the bridge entry's eleven values is gone; an inline-cache record keeps its key, entry and
target only.

Alternated with the section 10 binary, medians of three, Windows x64:

| kernel | section 10 | section 11 |
|---|---|---|
| `RealCall` (20M bound calls) | 0.150 s | **0.121 s** |
| `VirtualCall` (2M calls, monomorphic) | 0.016 s | **0.012 s** |
| `Fib(32)` (7M recursive calls) | 0.072 s | **0.043 s** |
| func-ref, one reference (5M) | 0.156 s | **0.066 s** |
| func-ref, two alternating (5M) | 0.153 s | **0.079 s** |
| bound call, per call (less the body) | 7.0 ns | **5.5 ns** |
| virtual call, per call | 8.0 ns | **6.0 ns** |

A bound call in a loop gains the estimated nanosecond and a half. Recursion and func-ref
calls gain far more because a call's cost there is latency, not instruction count: the
arguments and the result no longer make a round trip through the operand stack (a store, a
count update and a dependent load each way), and the callee's entry is a run of stores
into its own frame with nothing to wait for. `Fib(32)` stands at 0.043 s against 0.208 s
on master before this design.

Verification: `vm_jit_equiv.obs` gains probes for every value shape across a native call
(`Float` arguments and results as literals, locals and registers; a call with no arguments;
eight arguments; a `Nil` result; receivers that live only in the callee's slots while it
allocates; a func-ref result, which keeps the bridge), byte-identical across the three
modes and on the section 10 binary; `OBJECK_JIT_REPORT=1` on the fixture shows no fallback;
the two exits' tests from section 8 pass; both regression passes on Windows and Linux.

What is left of F7 is on ARM64: sections 7 to 11 on the Mac (the backend has the bridge
entry only, and publishes no native entry). On AMD64 the next gain is the callee's own
prologue -- the pushes, the two frame slots' worth of stores and the ten spill slots'
zeroing that a leaf method never uses -- and the caller's spills around a call.

## 12. Phase 2 on ARM64 (2026-09-10)

The Mac's first day on this design. Measured first, as section 4d of
`JIT_ARM64_HANDOFF_2026_09.md` asked: `jit_call_probe.obs` on an Apple M4 Max at
`94fed85ae5` (master with #767), `obc`/`obr` from `deploy_macos_arm64.sh`, medians of three.

| kernel | interpreter | JIT | JIT vs interpreter |
|---|---|---|---|
| `InlinedCall` (no call) | 0.617 s | 0.019 s | 32x |
| `RealCall` (20M bound calls) | 0.984 s | 0.365 s | **2.7x** |
| `NoCall` | 0.403 s | 0.011 s | 37x |
| `VirtualCall` (2M) | 0.077 s | 0.042 s | 1.8x |
| `Fib(32)` | 0.223 s | 0.135 s | 1.7x |

A compiled-to-compiled call cost 17.7 ns (`RealCall` less `NoCall`, over 20M), an interpreted
one 29 ns, a `virtual` one from compiled code about 20 ns: section 1's shape with section 6's
bridge in place, on a faster core. As on AMD64, the call-bound loop gains a fraction of what
the same loop gains without the call.

What landed, in `core/vm/arch/jit/arm64/jit_arm_a64.{h,cpp}`: section 7's three changes in
the same order, each alternated with master, medians of three. Master's own medians drifted
between 0.353 s and 0.365 s on `RealCall` across the session, so each step is read against its
own alternation; the column below is the last one.

| kernel | master | top pointer and pops | straight-store zeroing | `D8`-`D15` on demand |
|---|---|---|---|---|
| `RealCall` (20M) | 0.363 s | 0.340 s | 0.310 s | **0.279 s** |
| `Fib(32)` | 0.135 s | 0.126 s | 0.114 s | **0.103 s** |
| `VirtualCall` (2M, the bridge) | 0.042 s | 0.040 s | 0.036 s | **0.033 s** |
| compiled call, per call | 17.1 ns | 16.5 ns | 15.0 ns | **13.4 ns** |

- **The operand-stack arithmetic, once per sequence.** `ProcessParameters` loads the stack
  pointer and count once, computes `top = op_stack + count * 8`, drops the count, and reads
  each argument at a displacement below `top` (`ldur`, whose nine-bit displacement runs out at
  32 words; `top` steps down by 256 past that, and `jit_entry_shapes.obs` has a 37-word list
  with a func-ref whose two words straddle the step). It was two loads and
  `dec`/`ldr`/`lsl`/`add`/`ldr` per parameter. `ProcessReturn` stores at a running displacement
  from `top` and bumps the count once. The result pops are `count -= 1` then
  `ldr Xd, [op_stack, Xcount, lsl #3]` (`ldr_base_index_reg`, `ldr_base_index_freg`), six
  instructions where there were nine.
- **The zeroing.** `RegisterRoot` zeroed the frame with a five-instruction loop, one word per
  trip: for the probe's `Acc:Add`, nineteen trips and about 135 instructions, two thirds of
  everything the callee executed outside its body. It is `stp xzr, xzr` pairs from `SP` now
  for up to 32 words of locals (`EmitZeroWords`), a three-instruction loop above that (the
  entry-shapes test's 36-local method). The loop's range had also run through the `D8`-`D15`
  save slots the prologue had just filled, so every compiled method handed zeros back in its
  caller's callee-saved float registers -- unnoticed because no C++ caller keeps a value there
  across the call. The two ranges zeroed now, the six spill slots and the locals, leave the save
  slots between them alone.
- **`D8`-`D15` on demand.** The pool hands out `D0`-`D7` first (not `D15` first, as the
  handoff said) and reaches the callee-saved eight only with nine floats live; `GetFpRegister`
  notes when it does, and `Compile()` turns the prologue's save block and every epilogue's
  restore block into `b +8` over themselves when it never did (`fp_callee_saved_used`,
  `fp_save_index`, `fp_restore_indices`; every `RTRN` emits its own epilogue, so every block is
  recorded, the lesson of section 7's first version). The entry-shapes test's ten-float product
  keeps the save.
- **The frame-size immediate, exactly.** `Prolog` and `Epilog` ORed `final_local_space << 10`
  onto templates whose immediate field already held 96, so a frame whose size had bits 5 or 6
  clear was over-allocated by up to 96 bytes and one past 4 KB overflowed into the shift bit.
  `EmitFrameAdjust` computes the field and takes a size past 4095 through `X11` (the
  extended-register `sub`/`add` on `sp`). Harmless until now; the native entry's outgoing area,
  an `SP`-relative offset computed from the size, would not have been.

From the tracing listing of `Acc:Add`, the probe's callee: about 200 instructions outside its
body on master, about 55 now. What is left of a call on ARM64 is the C++ bridge and the
operand-stack traffic itself -- phase 3's business, as on AMD64 after section 7.

**Found on the way, fixed in the same PR.** A func-ref local's slot. `ProcessIndices` reserves
two words for a func-ref declaration but gave the slot the offset of the pair's *upper* word,
while `ProcessStore` and `ProcessLoad` write and read `[offset]` and `[offset + 8]` and the
collector walks the pair from the offset it was given: the reference's second word landed in the
next declaration's slot, and the collector read the pair one word low. A func-ref parameter
followed by any other local had that local replaced by the reference's closure word (0 for a
plain function reference), and a collection during the method read a zero as the reference's
method id. Hidden because the fixtures' func-ref parameters were last in their lists or inlined
away. The offset is the pair's lowest word now, as on AMD64; `jit_entry_shapes.obs` (`Applied`,
`Cross`) fails on the old layout with every method compiled and passes interpreted.

**Verification.** The flag tests 22/22 (`vm_jit_equiv.obs` byte-identical across the three
modes; from an agent shell they need `LC_ALL=en_US.UTF-8`, see the handoff);
`OBJECK_JIT_REPORT=1` on the fixture names only `Arith:Mix`, which falls back on master too;
`jit_entry_shapes.obs` byte-identical across the modes with no fallback; `jit_native_call_error`,
`jit_native_call_depth`, `jit_frame_unreferenced_local`, `inline_funcref_param`,
`jit_virtual_equals`, `jit_gc_safepoint`, `jit_float_mem_ops` and `core_thread_gc_stress`
compiled; both regression passes, 229 passed, 3 skipped, 0 failed each.

**Two things the day showed about the ARM64 backend, for what comes next.** Its pool is eight
general registers, `X0`-`X7` (`X9`-`X15` are commented out in `Compile()`; the handoff's
"fifteen" counted them), and it has no spilling: the bytecode pushes every term of a left-nested
chain before the first add, so an expression with more than eight live temporaries falls back
to the interpreter whole -- the entry-shapes test's sums are four-term statements for that
reason, and `Arith:Mix` (chained calls) is the fixture's standing fallback. `X12`-`X15` into the
pool (F4 on ARM64) is a small change with the shape of #733 and the Linux x64 pool. Then phase
3: the native entry and call site, section 4d step 2 of the handoff.

**Done the same night** ([#770](https://github.com/objeck/objeck-lang/pull/770)): `X12`-`X15` joined the pool, handed out after `X0`-`X7`, so
nothing changes for a method that fits in eight and a ninth to twelfth temporary no longer
sends the method to the interpreter. The entry-shapes test's sums are ten-term statements
again and compile; master's VM reports `Wide` falling back on them. The call probe is
unchanged; the six loop kernels of `jit_probe.obs` and the call probe are unchanged within noise. `Arith:Mix`, the fixture's one standing fallback, was a float
live across a libc call, which the libc helper refused; [#771](https://github.com/objeck/objeck-lang/pull/771) parks such a float in a free
callee-saved `D8`-`D15` register for the call, and the fixture reports no fallback on ARM64 at all.
