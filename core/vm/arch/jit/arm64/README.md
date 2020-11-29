## JIT Compiler for Apple Silicon (ARMv8) 
While the instruciton set for ARMv7 is very simailr to ARMv8 the instruciton encoding is very differnt. There are 32 integer and floating point registers. Values stored in volatile registers need to stored between function calls.

### Registers
Link (https://stackoverflow.com/questions/28109826/arm64-using-gas-on-ios)
* X0-X7 - arguments and return value (volatile)
* X8 = indirect result (struct) location (or temp reg)
* X9-X15 = temporary (volatile)
* X16-X17 - intro-call-use registers (PLT, Linker) or temp
* X18 - platform specific use (TLS)
* X19-X28 - callee saved registers (non-volatile)
* X29 - frame pointer
* X30 - link register (LR)
vSP - stack pointer and zero (XZR)
* V0-V7, V16-V31 - volatile NEON and FP registers
* V8-V15 - callee saved registers (non-volatile, used for temp vars by compilers)

### Security
* macOS 11 has added security for code exection use:    
* Allocate:    
    
    mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, 0, 0);

* Write:    
    
    memcpy(temp, code, byte_size);
    __clear_cache(temp, temp + byte_size);
    pthread_jit_write_protect_np(true);

### To do
* Instructions:

    lsl_reg_reg, lsr_reg_reg, mul_xxx_xxx and div_xxx_xx

* Functionality: Callback to interpreter
