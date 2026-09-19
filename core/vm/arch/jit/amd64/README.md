## JIT Compiler — x86-64 (Windows, Linux, macOS)

The AMD64 back-end. A descendant of Objeck's original IA-32 JIT, refactored for 64-bit Linux/macOS and later generalized to 64-bit Windows. Most users run on x64 Windows or Linux, so this is the most heavily exercised back-end.

> History: writing a JIT for Intel means living with funky addressing modes and variable-length instructions — it felt normal at the time, until the ARMv7/ARM64 ports showed how convoluted the x86 encoding really is. Battle scars aside, it's a lean, mean JIT.

See the [shared JIT overview](../README.md) for the auto-JIT lifecycle, the callback bridge, and the safety pre-scans common to both back-ends. This file covers what's **specific to AMD64**.

### Instruction gating — whitelist
AMD64 opts each opcode *in* via `CanJitInstruction()` (`jit_amd_lp64.cpp`). An opcode the whitelist doesn't recognize makes the pre-scan return `false`, so the method simply stays interpreted — no partial/corrupt compile. (ARM64's pre-scan is a whitelist of the same shape; the one opcode AMD64 accepts and it does not is `NEW_FUNC_INST`.)

### Register & stack model
Accumulator model — intermediate values flow through a small register set; method locals live in stack slots addressed off `RBP`. Frame layout constants (`CLS_ID`, `MTHD_ID`, `OP_STACK`, `STACK_POS`, `JIT_MEM`, `INSTANCE_MEM`, `FRAME_MEM`, temp `TMP_REG_*` / `TMP_XMM_*` slots) are defined at the top of `jit_amd_lp64.h`.

Each compile creates its register holders once, through `NewRegisterHolder`, and `all_regs` owns them: the free pools (`aval_regs`, `aval_xregs`, and on Windows `aux_regs`, whose `RSI` and `RDI` are handed out only when the eight pool registers are all live), the working stack and the local caches borrow them, and the destructor deletes each holder once. It used to delete from the lists instead, and an auxiliary register that had been through the local cache sat in two of them and was freed twice ([#773](https://github.com/objeck/objeck-lang/issues/773)).

### AMD64-specific optimizations
| Optimization | Notes |
|---|---|
| Local register cache | `local_reg_cache` / `local_xreg_cache` keep a just-stored local live in its register; later loads skip the reload. Flushed at control flow and before any callback. |
| Method inlining | **Not active.** `ProcessInlineMethod` is complete but has no caller: every `MTHD_CALL` takes the call path, because inlining a constructor has `INSTANCE_MEM` offset issues nobody has resolved (the note at the `MTHD_CALL` case). `CanInlineMethod` / `ComputeInlineLocalSpace` still run and size `extra_inline_space`, so every compiled frame reserves stack for inlining that never happens. |
| Division by a constant | Power-of-two `/` and `%` become `SAR` + sign-bias / mask + correction; a literal `-1` is `neg` / `xor`; any other non-zero constant is a multiply by a magic number (`EmitMagicDivision`: the high half of a one-operand `imul`). All in `div_imm_reg`. ARM64 multiplies by a magic number too, powers of two included. |
| Loop detection | Backward-jump pre-scan records `{header, backedge}` in `detected_loops`, which `PlanPinRegions` uses for the loop locals below. |
| Loop locals in registers (F3) | The hottest `Int`/`Char` locals of each loop, up to three, live in `R13`-`R15` for the loop's extent: loaded at the header, stored back on every exit. On Windows up to four `Float` locals also live in `XMM6`-`XMM9` (callee-saved there, caller-saved on POSIX). `OBJECK_JIT_PIN_MAX` / `OBJECK_JIT_PIN_SKIP` narrow it for bisecting. **AMD64 only** so far; `docs/JIT_LOOP_LOCALS_DESIGN.md`. |
| `cmov` | Branchless conditional moves where profitable. |
| `JMP_TABLE` codegen | `select` tables compile to RIP-relative `LEA` of the slot table → `MOVSXD` (index×4) → indirect `JMP`, no interpreter fallback. |

### Calling-convention details
- **Windows x64 ABI** requires a **32-byte shadow space** before every native call — `call_xfunc` / `call_xfunc2` allocate it. Forgetting it corrupts the caller's stack.
- Param registers differ from the SysV path: Windows uses `RCX/RDX/R8/R9` (callback addr in `R10`); POSIX uses `RDI/RSI/RDX/RCX/R8/R9` (callback addr in `R15`). See `EmitBridgeCall`, which `ProcessStackCallback` calls.
- A call from compiled code to compiled code skips that bridge (F7): `EmitNativeCallSite` enters the callee's native entry (`EmitNativePrologue`) with `self` and the caller's frame pointer in the first two argument registers (`RCX`/`RDX` on Windows, `RDI`/`RSI` on POSIX) and the arguments in the caller's outgoing area. The [shared overview](../README.md) draws it; `docs/JIT_CALLING_CONVENTION_DESIGN.md` has the design.

### x87 vs. helper calls (gotcha-driven)
The old code emitted x87 FPU instructions for transcendentals and got several wrong. These now route through C-library calls via `call_xfunc`:
- `fsin` / `fcos` / `ftan` → `call_xfunc(sin/cos/tan)` (the x87 forms produced wrong results).
- `flog` / `flog10` → `call_xfunc(log/log10)` (the x87 versions loaded constants instead of computing).
- `REG_FLOAT`-input handling for `call_xfunc` / `sqrt` / `round` was fixed alongside.

### Code Layout
![JIT Code Layout](../../../../../docs/images/jit_design.svg "JIT Code Layout")

### Implementation
C++ with STL. Sources: `jit_amd_lp64.h`, `jit_amd_lp64.cpp` (~8.7k lines). Shared driver: [`../jit_common.{h,cpp}`](../jit_common.h).
