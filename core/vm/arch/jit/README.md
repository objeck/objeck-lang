## JIT Compilers
Translates bytecode to executable machine code. 

### Design
JIT compilers favor translation speed vs. code optimization. Compilers iterate over bytecode instructions managing states using a stack and metadata associated with each translation. Method/function variables are stored in the native stack frame eliminating the need for push/pop based calculations. Design uses an accumulator model thus reducing the number of registers required for operations. Compilers eliminate redundant move instructions and folds constant expressions. Boolean expressions are optimized out of loops and target features such as cmov for x86/x86_64 and conditional execution for ARMv7 is used where appropriate. In the process of porting the compiler to 64-bit ARMv8 for Apple Silicon.

Machine code is generated for general runtime error checking such as Nil deferences and array bounds checks. JIT compilers callback to interpreted code as needed.

![alt text](../../../images/jit_design.png "JIT Code Layout")

### Implementation
C++ using the STL.