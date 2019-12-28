## Memory Manager
Mark and sweep garbage collector.

### Design
Memory is allocated until a threshold is hit and evokes the garbage collector. Garbage collector scans all roots namely the calculation stack, interpreter call stack and associated JIT machine code. JIT machine code is marked as active for collection via a stack frame flag. All scanned memory is tagged, memory not tagged is freed. 

To enhance performance scanning of roots is done in three separate threads.

### Implementation
C++ using the STL.