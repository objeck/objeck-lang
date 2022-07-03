## Virutal Machine
The Objeck virtual machine is a stack-based VM that can convert bytecode into machine code for faster execution. The VM has a caching "mark and sweep" garbage collector along with access to host capabilities such as networking and file access.

![alt text](../../images/design3.png "Objeck VM")

### Design
The major components of the VM are the interpreter, JIT compiler and memory manager. All 3 components interop with one another. For portability, OS functions for Windows and POSIX environments are abstracted.

The VM supports the following targets:

1. Windows (x64, AMD64)
2. macOS (POSIX, AMD64)
3. macOS (POSIX, ARM64)
4. Linux (POSIX, AMD64)

### Implementation
C++ using STL with machine code generation
