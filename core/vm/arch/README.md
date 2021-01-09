## Memory Manager
Mark and sweep garbage collector.

### Design
Memory is allocated until a threshold is hit which evokes the garbage collector. The garbage collector scans all "roots" namely the calculation stack, interpreter stack and process stack for JIT'ed code. Scanning of roots and associated memory is done in separate threads. All scanned memory is tagged, memory not tagged is released into a pool.

### Implementation
C++ using the STL.