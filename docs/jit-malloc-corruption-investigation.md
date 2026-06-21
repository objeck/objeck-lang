# JIT heap corruption ("BUG IN CLIENT OF LIBMALLOC: memory corruption of free block") — investigation handoff

Status: **OPEN** — root cause not yet isolated. This is a *separate* bug from the compiler
comparison-opcode fix (`fix/compiler-cmp-cast-type`); that fix does **not** resolve this one.

## Symptom

`obr` running an SDL game (repro: `programs/deploy/2d_game_13.obs`) aborts with
`BUG IN CLIENT OF LIBMALLOC: memory corruption of free block` (SIGTRAP), or occasionally
`EXC_BAD_ACCESS`/SIGBUS with `PC = 0x3F947AE147AE147B` (the IEEE-754 bits of the double `0.02`,
i.e. a float value used as a code/vtable pointer). The crash is always *detected* late — in the SDL
event pump / `SDL_RenderPresent` / `SDL_DestroyTexture`, i.e. the first heavy malloc activity after
the heap was already corrupted. The corrupting write happens earlier.

Likely **backend-independent** (reported on both Apple Silicon and Windows/AMD64), so suspect shared
JIT logic rather than one backend's codegen.

## Headless repro (no window interaction needed)

Shrink the young gen so GC + allocation churn is constant, and force-JIT every method:

1. `core/vm/arch/memory.h`: `#define YOUNG_REGION_SIZE (32 * 1024)` (was 128 MB).
2. Build `VM-A64` (or the AMD64 VM).
3. From `programs/deploy/`: `OBJECK_JIT_THRESHOLD=1 obr 2d_game_13.obe` — crashes within a few of
   ~10 runs (exit 133/134/138/139). `OBJECK_JIT_DISABLE=1` never crashes.

Revert `YOUNG_REGION_SIZE` to 128 MB afterward.

## Established (high confidence)

- **JIT-only.** `OBJECK_JIT_DISABLE=1` is always clean; JIT on crashes. So it's JIT codegen / a
  JIT-runtime interaction, not game logic, the SDL bindings, or audio (still crashes with sound off).
- **Not the GC.** Three controlled rebuilds each still crashed: (a) force a full old-gen scan in the
  minor GC (bypass the dirty list / write-barrier), (b) disable the young generation entirely (no
  moving), (c) leak-on-sweep (never free/reuse). So not moving, not the remembered set, not UAF.
- **Not the normal heap stores.** A JIT-emitted validator checked, for every instance-var store and
  every array-element access, that the target stays within the object's allocation
  (`raw_mem[0]` size). After fixing a clobber bug in the validator itself, it **never fired**.
- **Not `call_stack`/`op_stack` overflow or stack-pointer drift** (guards at the JIT-to-JIT push and
  at the `JitStackCallback` boundary never fired; `OP_STACK_SIZE` 768→65536 didn't help).
- **Not the float-calc bail / no compile-time stack imbalance** on ARM64 (a method that finishes
  compilation with a non-empty working stack was checked for — never fired; the game never hit the
  `ProcessFloatCalculation` bail headless).
- **Wild-pointer store, not a contiguous overflow.** `libgmalloc` (guard page per allocation) does
  *not* catch it — the write lands in some *other* live allocation, far from any object end.
- Objeck objects are `calloc`-backed (`core/vm/arch/memory.cpp` `GetMemory`), so a wild Objeck store
  smashes a system-malloc block. **Allocation density controls detectability**, which is why the
  stress repro is needed — it's not a GC bug.

## Update (2026-06-21): it's a wrong-VALUE store (type confusion), not a wild base

A stricter validator was tried: a runtime check before every `move_reg_mem`/`move_freg_mem`
(base != SP) that aborts if the base is an **implausible pointer** (not 8-byte aligned, `< 0x1000`,
or `>= 0x0000_8000_0000_0000` — the float-bit-pattern range) **or** an out-of-bounds offset into a
live old-gen object. It **never fired**, yet the game still crashed (mostly SIGBUS/SIGSEGV now).

So the corrupting store has a **valid base and a valid in-bounds offset** — what's wrong is the
**VALUE**: a float (e.g. the literal `0.02` = `0x3F947AE147AE147B`) is written where a pointer/int
belongs. The renderer later follows that field as a pointer and jumps to `0.02` (the SIGBUS PC).
This is a **type confusion on the working stack** — a float-typed value consumed where an
int/object-reference value is expected — not a wild base and not a wrong address.

Also ruled out this pass: a *persistent* working-stack imbalance. A check at the end of
`ProcessInstructions` (compile finished with `compile_success` but a non-empty working stack) never
fired, so the stack is balanced at method boundaries — any type confusion is transient/mid-method.

## Best remaining hypothesis

A **float value reaching an int/object store** via a register/working-stack **type mismatch** in
codegen. Leads to chase next:
- the known-buggy `move_freg_freg` GP-bridge (reads `X{src}` not `D{src}`) and every caller that
  still relies on it — a float bridged through the wrong GP register lands as a bogus int/pointer;
- any path where an `IMM_FLOAT`/`REG_FLOAT`/`MEM_FLOAT` working-stack entry is consumed by an
  integer/reference store (`ProcessStore`, `ProcessStoreIntElement`, parameter marshalling) without
  an `I2F`/`F2I` conversion;
- whether the compiler comparison/cast fix (`fix/compiler-cmp-cast-type`) removes one source of such
  mismatches but not all (the game still corrupted with the *recompiled* bytecode in a 32 KB-young
  stress run, so at least one more source remains).

## How to catch it next

Instrument the **value** (not the base) at integer/reference stores: pass the stored value to a
validator and assert that a store targeting a **reference-typed field/element** holds a plausible
pointer or null — abort otherwise, printing the JIT method name (track it via a save/restored global
set in `ProcessJitMethodCall` and the JIT-to-JIT path). The field/element type is known at codegen
time from the instruction/declarations, so the check can be limited to reference stores to avoid
false positives on legitimate large-int values.

## Suggested next step

Extend the JIT store validator (a runtime check emitted before each `move_reg_mem`/`move_freg_mem`
whose base != SP) to **abort on a base that is neither a live heap object nor inside a known-valid
buffer**:

- Register the valid ranges at runtime: the `op_stack`/`call_stack` buffers and the executing
  thread's stack bounds.
- In the validator: if `base` is non-null, not in `old_generation`, not in the young region, and not
  within any registered buffer/stack range → it's wild → `abort()` with the method name (track the
  current JIT method via a save/restored global set in `ProcessJitMethodCall` and the JIT-to-JIT path).

Instrumentation gotchas already hit (so the next attempt doesn't repeat them):
- A JIT-emitted validator call must not use `move_imm_reg` for a 64-bit address (its constant-pool
  fallback emits `ldr xN,[sp,#INT_CONSTS]`, invalid while SP is shifted by a save block) nor
  `call_reg` (spills LR to a frame temp slot). Use raw `movz/movk` + raw `blr`, and preserve **NZCV**
  (the call clobbers flags; a store can sit between a compare and its branch).
- Do not instrument emitters that use **hardcoded** (non-backpatched) relative branch offsets — e.g.
  `RegisterRoot`'s frame-zeroing loop uses `b -6`; inserting a stub inside it desyncs the offset.
  Guard those with a flag.
- When passing two registers as call args, load both from the **saved** stack slots, not the live
  registers (e.g. `mov x0, base; mov x1, off` clobbers `off` if `off == x0`).
