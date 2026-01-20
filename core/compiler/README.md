# Objeck Compiler (obc)

The Objeck compiler converts source code into Objeck executables (.obe) or Objeck libraries (.obl). It produces compressed bytecode for the Objeck VM, a stack-based runtime system, and performs extensive optimizations including method inlining. Optional debug symbols can be generated for use with the runtime debugger.

![alt text](../../docs/images/compiling2.svg "Objeck Compiler")

## Features

- **Modern CLI**: GNU-style flags (`--source`/`-s`, `--destination`/`-d`, `--debug`/`-D`) with backward compatibility
- **Optimizing compiler**: Multiple optimization passes for performance
- **Type safety**: Strong static typing with type inference
- **Generics support**: Type erasure and type boxing (v5.x+)
- **Debug symbols**: Optional debug information for debugger integration
- **Library support**: Links against external Objeck libraries (.obl)

## Usage

```bash
# Compile a program (modern syntax)
obc --source hello.obs --destination hello.obe

# Compile with shortcuts
obc -s hello.obs -d hello.obe

# Compile with debug symbols
obc -s myapp.obs -d myapp.obe --debug

# Legacy syntax (still supported)
obc -src hello.obs -dest hello.obe
```

## Architecture

### Frontend
- **Scanner & Parser**: Handwritten recursive-descent parser for complete control over error messages and performance
- **Contextual Analyzer**: Implements type checking and semantic analysis, with special handling for primitives to act as objects
- **Code Emitter**: Transforms the abstract syntax tree into intermediate bytecode blocks

### Backend
- **Optimizer**: Multi-pass optimization pipeline (see below)
- **Linker**: Resolves external library references and produces final bytecode
- **Code Pruning**: Eliminates unreachable code and unused definitions

### Optimization Pipeline

The optimizer performs the following optimizations in order:

1. **Jump optimizations** - Eliminates redundant jumps and unreachable code
2. **Constant folding** - Evaluates constant expressions at compile time
3. **Getter/Setter inlining** - Inlines simple property accessors
4. **Dead store removal** - Removes unused variable assignments
5. **Constant propagation** - Propagates constant values through the program
6. **Method inlining** - Inlines small, frequently-called methods
7. **Strength reduction** - Replaces expensive operations with cheaper equivalents
8. **Instruction optimization** - Peephole optimizations on bytecode sequences

### Generics Implementation

As of v5.x, the compiler supports generics through type erasure, similar to Java. Generic types are compiled to their upper bounds, with runtime type checks inserted where necessary. Type boxing allows primitives to be used seamlessly with generic containers.

## Implementation

- **Language**: C++ with STL
- **Build System**: Visual Studio (Windows), Makefile (Linux/macOS)
- **Dependencies**: Standard C++ library, platform-specific APIs

## Output Format

The compiler produces bytecode files in Objeck's compressed format:
- **.obe files**: Executable programs
- **.obl files**: Reusable libraries

All bytecode is position-independent and portable across platforms.

## See Also

- [Main README](../../README.md) - Project overview
- [Virtual Machine](../vm/README.md) - Bytecode execution environment
- [Debugger](../debugger/README.md) - Runtime debugging
- [API Documentation](https://www.objeck.org) - Language reference
