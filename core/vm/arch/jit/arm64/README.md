## JIT Compiler — ARM64 (Apple Silicon, Raspberry Pi 64-bit)
While ARMv7 and ARMv8 share an instruction *set* family, the encoding is very different. ARM64 adds registers (32 integer + 32 FP/NEON), requires volatile registers to be saved across calls, and — unlike ARMv7 — does **not** support conditional execution of arbitrary instructions (use `csel`-family forms instead).

See the [shared JIT overview](../README.md) for the auto-JIT lifecycle, callback bridge, and safety pre-scans common to both back-ends. This file covers what's **specific to ARM64**.

### Instruction gating — whitelist
ARM64 opts each opcode *in* through its own `CanJitInstruction()` whitelist (`jit_arm_a64.cpp`), the same shape as AMD64's; the one opcode AMD64 accepts and ARM64 does not is `NEW_FUNC_INST`. A method holding anything else stays interpreted whole. Both still share the frame-dependent-trap rejection and operand-kind compile guards from `jit_common`.

### Back-end behavior vs. AMD64
| Feature | ARM64 | Note |
|---|---|---|
| Local register cache | yes | `CacheLocalRegister` / `CacheLocalFpRegister` / `FlushLocalCache` |
| Direct JIT→JIT calls | yes | since [#776](https://github.com/objeck/objeck-lang/pull/776) a compiled caller enters a compiled callee's native entry (`EmitNativeCallSite`, `EmitNativePrologue`: self, the caller's stack pointer and the end of its outgoing area in `X0`-`X2`, the result in `D0`), with inline caches at `virtual` and func-ref sites; anything else takes the callback bridge. Either way the caller reloads a **stale `self`** afterwards (a call may trigger GC, which can move young objects) |
| Method inlining | **no** | the JIT inlines nothing (AMD64's inliner is switched off too) |
| Division by a constant | yes | any constant but `0`, `1` and `-1` is a multiply by a magic number (`EmitMagicDivision`, `smulh`), powers of two included; AMD64 shifts those instead |
| Loop detection | yes | the pre-scan's backward jumps mark the loop headers that get a safepoint poll; no `detected_loops` list, so no loop locals in registers yet (AMD64's F3) |
| `JMP_TABLE` codegen | yes | `ADR` base → `ADD` (index `LSL #2`) → `LDR` word → indirect `BR` |
| `LOG_FLOAT` | C `log()` | computed via the C library, not x87 (there is no x87 on ARM) |

### Registers and Stack
12 integer registers in the pool (`X0`-`X7`, then `X12`-`X15`); `X9`-`X11` are scratch and `X19` holds the safepoint flag's address. 16 floating-point registers (`D0`-`D15`, handed out from `D0`), so the callee-saved `D8`-`D15` are saved only by a method that reaches them. Neither pool spills: an expression that needs more falls back to the interpreter.

Reference: [arm64 using gas on iOS](https://stackoverflow.com/questions/28109826/arm64-using-gas-on-ios)
* X0-X7 — arguments and return value (volatile)
* X8 — indirect result (struct) location, or temp
* X9-X15 — temporary (volatile)
* X16-X17 — intra-call-use registers (PLT, linker), or temp
* X18 — platform-specific (TLS)
* X19-X28 — callee-saved (non-volatile)
* X29 — frame pointer
* X30 — link register (LR)
* SP — stack pointer; XZR — zero register
* V0-V7, V16-V31 — volatile NEON/FP registers
* V8-V15 — callee-saved (non-volatile, used for temp vars)

The processor stack grows **up**, and the memory manager was updated to accommodate (this also affects the direction the GC walks JIT locals — see [`../../README.md`](../../README.md)). Save the link register across any function call made out of JIT'ed code.

### Security — W^X JIT memory on Apple platforms
macOS 11+ enforces W^X for executable code. The back-end allocates and patches code accordingly:
* Allocate: `mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, 0, 0);`
* Write, then re-protect and flush I-cache:
    ```
    pthread_jit_write_protect_np(false);
    memcpy(temp, code, byte_size);
    __clear_cache(temp, temp + byte_size);
    pthread_jit_write_protect_np(true);
    ```
    [Caches and self-modifying code (Arm)](https://community.arm.com/developer/ip-products/processors/b/processors-ip-blog/posts/caches-and-self-modifying-code)
* Entitlements — for shipping, weigh "Allow Unsigned Executable Memory" vs. signing for code execution:
    ```
    <dict>
      <key>com.apple.security.cs.allow-jit</key>
      <true/>
      <key>com.apple.security.cs.disable-library-validation</key>
      <true/>
    </dict>
    ```

### Adding a runtime check or call to emitted code
Lessons from the 2026 JIT heap-corruption hunt (the investigation itself is in git history as `docs/jit-malloc-corruption-investigation.md`):
* Load a 64-bit helper address with raw `movz`/`movk` and call it with a raw `blr`. `move_imm_reg` falls back to the constant pool for a value with three or four non-zero 16-bit chunks, and that load (`ldr x9, [sp, #INT_CONSTS]`) reads the wrong slot while SP is shifted by a register-save block; `call_reg` spills LR to the `TMP_LR` frame slot.
* Preserve **NZCV** around the call: it clobbers the flags, and a store can sit between a compare and its branch.
* Do not emit a stub inside code that hand-encodes relative branches. The large-frame zeroing loop in `RegisterRoot` encodes `b.lt +4` and `b -4` directly, so anything inserted between them desyncs both.
* When passing two registers as arguments, load both from their saved stack slots, not the live registers: `mov x0, base; mov x1, off` clobbers `off` when `off` is `x0`.

### Implementation
C++ with STL. Sources: `jit_arm_a64.h`, `jit_arm_a64.cpp` (~6.6k lines). Shared driver: [`../jit_common.{h,cpp}`](../jit_common.h).
