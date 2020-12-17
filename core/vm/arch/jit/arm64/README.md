## JIT Compiler for Apple Silicon (ARMv8) 
While the instruction set for ARMv7 is very similar to ARMv8 the instruction encoding is very different. There are 32 integer and floating point registers. Values stored in volatile registers need to saved between function calls. ARMv8 does not support novel conditional instruction execution.

### Registers
Using 8 registers for general usage and an additional 2 for conversions. Using 8 floating point registers for general calculations.

Link: (https://stackoverflow.com/questions/28109826/arm64-using-gas-on-ios)
* X0-X7 - arguments and return value (volatile)
* X8 = indirect result (struct) location (or temp reg)
* X9-X15 = temporary (volatile)
* X16-X17 - intro-call-use registers (PLT, Linker) or temp
* X18 - platform specific use (TLS)
* X19-X28 - callee saved registers (non-volatile)
* X29 - frame pointer
* X30 - link register (LR)
* SP - stack pointer and zero (XZR)
* V0-V7, V16-V31 - volatile NEON and FP registers
* V8-V15 - callee saved registers (non-volatile, used for temp vars by compilers)

### Security
* macOS 11 has added security for code execution use:    
* Allocating memory: ```mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, 0, 0);```
* Writing to memory:    
    ```
    pthread_jit_write_protect_np(false);
    memcpy(temp, code, byte_size);
    __clear_cache(temp, temp + byte_size); // needed for ARM targets
    pthread_jit_write_protect_np(true);
    ```
* Application entitlements:
	Consider 'Allow Unsigned Executable Memory' option for shipping vs. signing for code execution
	```
	<dict>
		<key>com.apple.security.cs.allow-jit</key>
		<true/>
	</dict>
	```

### Optimizations
* Take advantage of more registers, trade off is storing volatile registers between calls
* Guard ~~12-bit intermediate values~~

### To do
* Testing
   * [Test cases](https://github.com/objeck/objeck-lang/tree/master/programs/test)
     * ~~Manually validate first 20~~
     * Automate validation of the first 100
   * [Examples](https://github.com/objeck/objeck-lang/tree/master/programs/deploy)
     * ~~XML parsing~~
     * ~~JSON web service client~~
* Library support
   * SDL2 support
   * ~~OpenSSL support~~
   * ~~zlib support~~
   * ~~ODBC support~~
* Error Checking
   * ~~Nil reference checking~~
   * ~~Array bounds checking~~
* Conditional branching
   * ~~Basic branching~~
   * ~~Logical comparison of negative numbers~~
   * ~~Forward branching~~
   * ~~Backward branching~~
* Types:
   * ~~Array support~~
   * ~~Basic ints, floats, chars and bytes~~
   * ~~Conversions between ints and floats~~
* Operations: 
   * ~~Mathematical:mul_xxx_xxx and div_xxx_xx, etc.~~
   * ~~Logical: les_reg_reg, gtr_reg_reg, eql_reg_reg, neql_reg_reg, etc.~~
   * ~~Bitwise: or_reg_reg, and_reg_reg, etc.~~
* Callback to interpreter from machine code
   * ~~Need space to save volatile registers~~
   * ~~Wire up memory manager to work with JIT machine code~~