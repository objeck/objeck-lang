## JIT Compiler — ARM64 (Apple Silicon, Raspberry Pi 64-bit)
While ARMv7 and ARMv8 share an instruction *set* family, the encoding is very different. ARM64 adds registers (32 integer + 32 FP/NEON), requires volatile registers to be saved across calls, and — unlike ARMv7 — does **not** support conditional execution of arbitrary instructions (use `csel`-family forms instead).

See the [shared JIT overview](../README.md) for the auto-JIT lifecycle, callback bridge, and safety pre-scans common to both back-ends. This file covers what's **specific to ARM64**.

### Instruction gating — blacklist
ARM64 uses a **blacklist** pre-scan: it rejects known-unsupported opcodes and compiles the rest. (AMD64 does the opposite — an explicit whitelist.) Both still share the frame-dependent-trap rejection and operand-kind compile guards from `jit_common`.

### Back-end behavior vs. AMD64
| Feature | ARM64 | Note |
|---|---|---|
| Local register cache | yes | `CacheLocalRegister` / `CacheLocalFpRegister` / `FlushLocalCache` |
| Direct JIT→JIT calls | yes | reloads a **stale `self`** after callbacks (callbacks may trigger GC, which can move young objects) |
| Method inlining | **no** | every `MTHD_CALL` goes through the interpreter callback |
| Division strength reduction | **no** | |
| Loop detection | **no** | |
| `JMP_TABLE` codegen | yes | `ADR` base → `ADD` (index `LSL #2`) → `LDR` word → indirect `BR` |
| `LOG_FLOAT` | C `log()` | computed via the C library, not x87 (there is no x87 on ARM) |

### Registers and Stack
10 integer registers for general use + 2 for conversions; 8 floating-point registers for general calculations.

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

### Implementation
C++ with STL. Sources: `jit_arm_a64.h`, `jit_arm_a64.cpp` (~5.2k lines). Shared driver: [`../jit_common.{h,cpp}`](../jit_common.h).
