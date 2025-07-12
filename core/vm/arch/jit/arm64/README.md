## JIT Compiler for ARM64 (Apple Silicon and Raspberry Pi 64-bit)
While the instruction set for ARMv7 is very similar to ARMv8, the instruction encoding is very different. There are 32 integer and floating-point registers, an increase. Values stored in volatile registers need to be saved between function calls. ARMv8 does not support conditional instruction execution.

### Registers and Stack
Utilizing 10 registers for general purposes and 2 more for conversions. Using 8 floating-point registers for general calculations.

Link: (https://stackoverflow.com/questions/28109826/arm64-using-gas-on-ios)
* X0-X7 - arguments and return value (volatile)
* X8 = indirect result (struct) location (or temp reg)
* X9-X15 = temporary (volatile)
* X16-X17 - intro-call-use registers (PLT, Linker) or temp
* X18 - platform-specific use (TLS)
* X19-X28 - callee saved registers (non-volatile)
* X29 - frame pointer
* X30 - link register (LR)
* SP - stack pointer and zero (XZR)
* V0-V7, V16-V31 - volatile NEON and FP registers
* V8-V15 - callee saved registers (non-volatile, used for temp vars by compilers)

The processor stack grows up, and the memory manager was updated to accommodate.

Save the link register between function calls out of the JIT'ed code.

### Security
* macOS 11 has added security for buffer code execution
* Allocating memory: ```mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, 0, 0);```
* Writing to memory:    
    ```
    pthread_jit_write_protect_np(false);
    memcpy(temp, code, byte_size);
    __clear_cache(temp, temp + byte_size);
    pthread_jit_write_protect_np(true);
    ```
    [Hidden gem about ARM cache management](https://community.arm.com/developer/ip-products/processors/b/processors-ip-blog/posts/caches-and-self-modifying-code)
* Application entitlements:
	Consider 'Allow Unsigned Executable Memory' option for shipping vs. signing for code execution
	```
	<dict>
      <key>com.apple.security.cs.allow-jit</key>
      <true/>
      <key>com.apple.security.cs.disable-library-validation</key>
      <true/>
	</dict>
	```
