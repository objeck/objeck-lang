# JIT: what a compiled call costs, and the convention that removes it (F7)

**Status:** phases 1 and 2 implemented, 2026-09-10 (sections 6 and 7); phase 3's first step, the direct native call, implemented on AMD64 the same day (section 8); the rest of phase 3 open. F7 of `JIT_CODEGEN_ASSESSMENT_2026_09.md`
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

### Phase 3: the convention (AMD64 two to three weeks; ARM64 after)

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

