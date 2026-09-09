## JIT Compilers
Translates VM bytecode into native machine code at runtime.

### Design
The JIT favors translation *speed* over deep code optimization — the goal is to get hot methods running natively with minimal compile latency, not to rival an ahead-of-time optimizer.

Each compiler iterates over a method's bytecode once, tracking state with a working stack of `RegInstr` values plus per-translation metadata. Method/function locals live directly in **processor stack slots** (addressed off `RBP` on AMD64, `SP` on ARM64), so there are no push/pop sequences for variable access. The design follows an **accumulator model**: intermediate values flow through a small set of registers, which keeps register pressure low and the allocator simple.

Beyond the straight translation, the compiler eliminates redundant moves, folds constant expressions, applies strength reduction (power-of-two `/` and `%` become shifts + bias), and uses target-specific instructions (`cmov` on x86_64, conditional/`csel`-style forms on ARM64). Machine code is emitted inline for runtime safety checks — `Nil` dereferences, array-bounds checks, and divide-by-zero — so the common path never re-enters the interpreter just to validate.

JIT'ed code can call back into interpreted code whenever it hits an operation it doesn't natively handle (see **Callback Bridge** below).

### Two front-ends, one strategy
There are two independent back-ends sharing the common driver in `jit_common.{h,cpp}`:

| | AMD64 (`amd64/jit_amd_lp64.cpp`) | ARM64 (`arm64/jit_arm_a64.cpp`) |
|---|---|---|
| Instruction gating | **whitelist** — `CanJitInstruction()` opts each opcode in | **whitelist** — `CanJitInstruction()`, same shape; accepts three fewer opcodes (`NEW_FUNC_INST`, `TRY_START`, `TRY_END`) |
| Local register cache | yes | yes |
| Direct JIT→JIT calls | yes | yes |
| Method inlining | **no** — `ProcessInlineMethod` exists but has no caller (`is_inlining` is never true); `CanInlineMethod` still runs and every compiled frame reserves stack for inlining that never happens | no (all `MTHD_CALL` go through the callback) |
| Division strength reduction | yes | no |
| Loop detection (backward-jump scan) | yes | yes (scans backward jumps in the pre-scan; keeps no `detected_loops` list) |
| `JMP_TABLE` native codegen | **no** | **no** — `JMP_TABLE` is in neither whitelist, so a method containing a `select` is not compiled at all |

Both back-ends share the safety pre-scans: **frame-dependent-trap rejection** and **operand-kind compile guards** (below).

### Auto-JIT lifecycle
Methods start interpreted. Every call increments a counter; once it crosses `JIT_AUTO_THRESHOLD` (default **10**, see `jit_common.h`) the method is compiled on its next entry. After a successful compile, every `MTHD_CALL` site that targets it is patched to `MTHD_CALL_JIT` (`PatchCallSites()`), so the interpreter's fast path — and other JIT'ed callers — dispatch straight to native code.

```mermaid
flowchart LR
    A[Method called] --> B{native code<br/>exists?}
    B -- yes --> NAT[Run native code]
    B -- no --> C[Interpret + bump<br/>call counter]
    C --> D{counter ><br/>threshold?}
    D -- no --> C
    D -- yes --> E[CanJitInstruction<br/>pre-scan]
    E -- rejected --> C
    E -- ok --> F[Compile to native]
    F --> G[PatchCallSites:<br/>MTHD_CALL -> MTHD_CALL_JIT]
    G --> NAT
    NAT -.JIT-to-JIT.-> NAT
```

**Tunables** (`GetJitAutoThreshold()`; the environment is read once, and a command-line override set through `vm_options.h` takes precedence over it):
- `--jit=off` / `OBJECK_JIT_DISABLE=1` — turn auto-JIT off entirely (threshold → `LONG_MAX`).
- `--jit=<calls>` / `OBJECK_JIT_THRESHOLD=N` — custom positive call-count threshold. LSP/long-lived hosts set this very high to avoid compiling transient code.
- The override slot lives in `vm_options.h` rather than here because `obd` is built with `_NO_JIT` and compiles `interpreter.cpp` without this header in scope.

### Key gains
- **Local variable register cache** — values stored to a local are kept live in their register (`local_reg_cache` / `local_xreg_cache`); a later load of the same slot reuses the register instead of reloading from the stack. The cache is flushed at control flow and before any callback (`FlushLocalCache()`), since the callee may mutate memory.
- **Direct JIT→JIT calling** — when a JIT'ed method calls another method that already has native code, it executes it directly via `JitRuntime::Execute()` instead of trampolining through the interpreter. The callee's `StackFrame` is still registered on the call stack so the GC can see it. Negative return status surfaces a diagnosable error (`-1` nil deref, `-2/-3` bounds, `-4` div-by-zero) instead of a silent crash.
- **Method inlining (AMD64) — not active.** `ProcessInlineMethod()` is complete but never called (see the note at its `MTHD_CALL` site: constructor `INSTANCE_MEM` offsets need investigation). `CanInlineMethod()` / `ComputeInlineLocalSpace()` still run at compile time only to size `extra_inline_space`, so every compiled frame pays for a feature that is off. Either finish it or delete it; this README used to describe it as working.
- **`JMP_TABLE` is not JIT-compiled.** Neither backend whitelists it, so any method containing a `select` runs in the interpreter. This README previously claimed native jump-table codegen on both; it does not exist.

### Safety pre-scans (both architectures)
- **Frame-dependent-trap rejection** (`HasFrameDependentTrap()` in `jit_common.h`) — the callback passes `nullptr` for the interpreter frame and keeps locals in native stack slots, so any trap that reads/writes `frame->mem` (e.g. `SERL_*`, `SYS_TIME`, `GMT_TIME`, `FILE_*_TIME`, `LOAD_CLS_BY_INST`) would dereference null. Methods containing such traps are rejected from compilation and stay interpreted.
- **Operand-kind compile guards** — if operand-stack tracking diverges from the expected kind (e.g. a float reaches an integer compare due to a front-end mismatch), the compile *fails cleanly* and the method falls back to the interpreter rather than emitting bad code.

### Callback Bridge (`ProcessStackCallback` → `JitStackCallback`)
For operations the back-end doesn't emit natively (allocation, non-JIT calls, traps, conversions, threading) the JIT'ed code calls back into the interpreter:

```mermaid
sequenceDiagram
    participant J as JIT'ed code
    participant C as JitStackCallback
    participant I as Interpreter/Runtime
    J->>J: FlushLocalCache (spill live regs)
    J->>J: spill non-param regs to TMP slots
    J->>J: copy params to operand stack
    J->>C: call (instr_id, stacks, ip, ...) via ABI regs
    C->>I: perform op (alloc / call / trap)
    Note over I: GC may move young objects here
    C-->>J: return
    J->>J: restore spilled regs
    J->>J: reload INSTANCE_MEM from frame->mem[0]
```

The reload of `INSTANCE_MEM` after the callback is essential: a callback can trigger GC, which may relocate (promote) young-generation objects, so the cached `self` pointer must be re-read from the frame. The number of params a callback consumes from the working stack is per-opcode — getting it wrong corrupts the stack (e.g. `RAND_FLOAT` consumes 0).

### Code Layout
![JIT Code Layout](../../../../docs/images/jit_design.svg "JIT Code Layout")

### Implementation
C++ using the STL. Back-end sources: `amd64/jit_amd_lp64.{h,cpp}`, `arm64/jit_arm_a64.{h,cpp}`; shared driver and tunables in `jit_common.{h,cpp}`. Canonical benchmark numbers live in [`docs/performance.md`](../../../../docs/performance.md).

## Loop locals in registers (AMD64)

The hottest `Int`/`Char` locals of each loop (up to three) live in `R13`-`R15` for the loop's
extent: loaded at the loop header (the back-edge target), read and written as registers inside,
stored back on every exit. Object slots are never pinned. `docs/JIT_LOOP_LOCALS_DESIGN.md` has the
design; `OBJECK_JIT_REPORT=1` lists each pinned loop, `OBJECK_JIT_PIN_MAX=<n>` caps the count
(`0` turns pinning off) and `OBJECK_JIT_PIN_SKIP=<substring>` exempts matching methods -- the way
to tell a pinning problem from anything else.

## Not compiled, by design

- A method containing a try region (`TRY_START`/`TRY_END` — what `?->` desugars to) runs in the interpreter on both backends: recovery needs the interpreter's handler stack, and native code has no way to resume at a handler. `OBJECK_JIT_REPORT=1` names such methods.

## Diagnostics

- `OBJECK_JIT_REPORT=1` — stderr line per method the JIT hands back to the interpreter (unsupported opcode, or the instruction where compilation failed). Both backends. Use it on a real program before deciding which fallback to fix next.
- `CL=/D_DEBUG_JIT` (MSVC) / `-D_DEBUG_JIT` — build an `obr` that prints every emitted instruction; it also prints per call at runtime, so keep iteration counts small.
