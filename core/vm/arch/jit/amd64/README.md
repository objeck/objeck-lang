## JIT Compiler for x86-64 (Windows, Linux and macOS)

A port of the first JIT compiler for IA32 targets (Windows, Linux, and OSX). The code was refactored to support 64-bit Linux and OSX targets. The refactored code was later extended, generalized, and optimized to support 64-bit Windows alongside Linux and macOS.

### Design
I could write a book here; however, PTSD kicks in. If you can write a JIT compiler for Intel, you can write one for any target. The funky address modes and variable length instructions were challenging to work with; however, I thought it was normal at the time. After writing JIT compilers for ARMv7 and ARM64 I better understand how convoluted the Intel machine coee is. However, most users are on x64 Windows and Linux and utilize this compiler.

Battle scares aside, it is a lean and mean JIT compiler.