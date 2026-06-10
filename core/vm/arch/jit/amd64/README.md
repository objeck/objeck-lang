## JIT Compiler — x86-64 (Windows, Linux, macOS)

The AMD64 back-end. A descendant of Objeck's original IA-32 JIT, refactored for 64-bit Linux/macOS and later generalized to 64-bit Windows. Most users run on x64 Windows or Linux, so this is the most heavily exercised back-end.

> History: writing a JIT for Intel means living with funky addressing modes and variable-length instructions — it felt normal at the time, until the ARMv7/ARM64 ports showed how convoluted the x86 encoding really is. Battle scars aside, it's a lean, mean JIT.

See the [shared JIT overview](../README.md) for the auto-JIT lifecycle, the callback bridge, and the safety pre-scans common to both back-ends. This file covers what's **specific to AMD64**.

### Instruction gating — whitelist
AMD64 opts each opcode *in* via `CanJitInstruction()` (`jit_amd_lp64.cpp`). An opcode the whitelist doesn't recognize makes the pre-scan return `false`, so the method simply stays interpreted — no partial/corrupt compile. (ARM64 takes the opposite tack: a blacklist pre-scan.)

### Register & stack model
Accumulator model — intermediate values flow through a small register set; method locals live in stack slots addressed off `RBP`. Frame layout constants (`CLS_ID`, `MTHD_ID`, `OP_STACK`, `STACK_POS`, `JIT_MEM`, `INSTANCE_MEM`, `FRAME_MEM`, temp `TMP_REG_*` / `TMP_XMM_*` slots) are defined at the top of `jit_amd_lp64.h`.

### AMD64-specific optimizations
| Optimization | Notes |
|---|---|
| Local register cache | `local_reg_cache` / `local_xreg_cache` keep a just-stored local live in its register; later loads skip the reload. Flushed at control flow and before any callback. |
| Method inlining | Small (≤ 20 instr), non-virtual, non-recursive, control-flow-free, trap-free callees expand into the caller (`CanInlineMethod` / `ProcessInlineMethod`); `INSTANCE_MEM` is saved/restored around inlined instance methods. **AMD64 only.** |
| Division strength reduction | Power-of-two `/` and `%` become `SAR` + sign-bias / mask + correction (`div_imm_reg`). **AMD64 only.** |
| Loop detection | Backward-jump pre-scan records `{header, backedge}` in `detected_loops` for future loop-aware passes. |
| `cmov` | Branchless conditional moves where profitable. |
| `JMP_TABLE` codegen | `select` tables compile to RIP-relative `LEA` of the slot table → `MOVSXD` (index×4) → indirect `JMP`, no interpreter fallback. |

### Calling-convention details
- **Windows x64 ABI** requires a **32-byte shadow space** before every native call — `call_xfunc` / `call_xfunc2` allocate it. Forgetting it corrupts the caller's stack.
- Param registers differ from the SysV path: Windows uses `RCX/RDX/R8/R9` (callback addr in `R10`); POSIX uses `RDI/RSI/RDX/RCX/R8` (callback addr in `R15`). See `ProcessStackCallback`.

### x87 vs. helper calls (gotcha-driven)
The old code emitted x87 FPU instructions for transcendentals and got several wrong. These now route through C-library calls via `call_xfunc`:
- `fsin` / `fcos` / `ftan` → `call_xfunc(sin/cos/tan)` (the x87 forms produced wrong results).
- `flog` / `flog10` → `call_xfunc(log/log10)` (the x87 versions loaded constants instead of computing).
- `REG_FLOAT`-input handling for `call_xfunc` / `sqrt` / `round` was fixed alongside.

### Code Layout
![JIT Code Layout](../../../../../docs/images/jit_design.svg "JIT Code Layout")

### Implementation
C++ with STL. Sources: `jit_amd_lp64.h`, `jit_amd_lp64.cpp` (~6.2k lines). Shared driver: [`../jit_common.{h,cpp}`](../jit_common.h).
