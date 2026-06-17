# ARM64 GC-Safepoint "Step 2" — Handoff (build & validate on Apple Silicon)

Pending ARM64 JIT work: **cache `&stw_active` in a callee-saved register** so the
per-loop-header GC-safepoint poll skips re-materializing the 64-bit flag address
each time. This is "step 2" of the P0 GC-safepoint roadmap in
[`docs/performance.md`](performance.md).

- **AMD64** has all three steps (inline flag test → cache addr in **R12** →
  poll only at loop back-edges), validated; shipped in **PR #539** (merged).
- **ARM64** has steps **1 and 3** (validated on Apple Silicon). Step 2 below is
  the only remaining piece.

## Why this must be done on the device

It needs a **new stack-frame slot** and **cannot be runtime-tested on x86-64** —
a wrong frame offset silently corrupts the stack on real ARM64 hardware. The
marginal benefit is small (after step 3 the poll only runs at loop headers), so
it was intentionally deferred to a box where it can be built and run.

## The change

Files: `core/vm/arch/jit/arm64/jit_arm_a64.h` and `.../jit_arm_a64.cpp`.
X28 is callee-saved and used by neither the GP register pool nor the aux pool.

### 1. `jit_arm_a64.h` — reserve a slot, grow the red zone

After the `SAVE_D15` define:

```c
#define SAVE_D15 248
#define SAVE_X28 256      // NEW: callee-saved GP slot for cached &stw_active
#define RED_ZONE 272      // was 256 (+16 for the new slot; stays 16-byte aligned)
```

### 2. `Prolog()` — save x28, then load the flag address into it

Append the `str x28` word to the `setup_code[]` array, after the `str d15` entry:

```c
    0xFD007FEF, // str d15, [sp, #248]
    0xF90083FC  // str x28, [sp, #256]   <-- ADD (save callee-saved x28)
```

Then, after the array copy loop (just before `Prolog()`'s closing brace):

```c
  // cache &stw_active in x28 for the per-loop-header safepoint poll
  move_imm_reg((size_t)MemoryManager::StwActiveAddr(), X28);
```

### 3. `Epilog()` — restore x28

In `teardown_code[]`, immediately before the `add_offset` (`add sp, sp, #...`) entry:

```c
    0xFD407FEF, // ldr d15, [sp, #248]
    0xF94083FC, // ldr x28, [sp, #256]   <-- ADD (restore x28)
    add_offset, // add sp, sp, #final_local_space
```

### 4. `EmitJitSafePoint()` — read the flag through the cached register

Drop the per-poll address load and point the `ldarb` at X28:

```c
  // remove:
  //   move_imm_reg((size_t)MemoryManager::StwActiveAddr(), X10);
  //   AddMachineCode(0x08DFFC00 | ((X10 & 0x1F) << 5) | (X11 & 0x1F));
  // replace with:
  AddMachineCode(0x08DFFC00 | ((X28 & 0x1F) << 5) | (X11 & 0x1F)); // ldarb W11, [X28]
```

### Encodings (for reference)

| Instruction | Word |
|---|---|
| `str x28, [sp, #256]` | `0xF90083FC` |
| `ldr x28, [sp, #256]` | `0xF94083FC` |
| `ldarb w11, [x28]`    | `0x08DFFF8B` |

## Build & validate (on the macOS / ARM64 box)

```bash
git checkout master && git pull origin master
# apply the edits above, then build:
cd core/release && bash deploy_macos_arm64.sh        # or your usual arm64 build
# correctness + no STW deadlock (a missed loop safepoint HANGS, not fails):
cd ../../programs/regression && bash run_regression.sh   # all pass except known core_opencv
# perf: compile fannkuchredux -opt s3 and compare runtime before/after (expect a small gain)
```

**What to watch:** if `obr` crashes on *any* JIT'd program, it's the frame
reservation — confirm the prologue `sub sp` reserves past offset 256 and that
locals (allocated starting at `RED_ZONE`) don't collide with the new slot. If
regression is green with no hangs, the safepoints are still being reached.

## After it passes

1. Flip the ARM64 step-2 row in `docs/performance.md` (P0 table) from
   *deferred* to *validated on Apple Silicon*.
2. Commit and open a PR (steps 1 + 3 already merged in PR #539).
