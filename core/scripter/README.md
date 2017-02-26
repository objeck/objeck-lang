## Scripter
Scripting interface for Objeck.  

###Design
**Work in progress**

Plan to reuse as much code as possible from the existing compiler and VM. Will replace the compiler code that emits instructions to a flat-file with a bridge that generate VM instructions that can be directly executed. 

###Implementation
C++ using the STL and machine code