## REPL

### Design
The REPL editor maintains an in-memory document that is compiled and executed as statements are entered. The compiler performs basic (level 1) optimizations and either emits binary code executed by the VM or reports errors to the user. The REPL compiler reduces noise by suppressing warning messages, such as unused variables.

### Implementation
C++ using STL with machine code generation