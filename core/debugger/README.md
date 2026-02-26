# Objeck Debugger (obd)

A command-line debugger for Objeck programs, providing breakpoints, stack inspection, and step-by-step execution. The debugger operates as a command-driven interpreter that works with programs compiled with debug symbols.

## Features

- **Breakpoints**: Set breakpoints at specific lines
- **Step execution**: Step into, over, and out of method calls
- **Stack inspection**: View call stack and local variables
- **Expression evaluation**: Evaluate expressions in the current context
- **Source listing**: View source code around current position
- **Memory inspection**: View memory allocation statistics

## Prerequisites

Code must be compiled with the `--debug` or `-D` flag to include debug symbols:

```bash
# Compile with debug symbols (modern syntax)
obc --source myapp.obs --destination myapp.obe --debug

# Or using shortcuts
obc -s myapp.obs -d myapp.obe -D

# Legacy syntax
obc -src myapp.obs -dest myapp.obe -debug
```

## Usage

```bash
# Start debugger
obd -b myapp.obe
obd -b myapp.obe -src ../src

# Common debugger commands
> b myapp.obs:42        # Set breakpoint at line 42
> r                     # Run program until breakpoint
> s                     # Step into method calls
> n                     # Step over (next line)
> j                     # Step out (jump out of method)
> p variable            # Print variable value
> l                     # List source code
> stack                 # Show call stack
> c                     # Continue execution
> h                     # Show help
> q                     # Exit debugger
```

## Commands Reference

| Command | Shortcut | Description |
|---------|----------|-------------|
| `run` | `r` | Start/restart program |
| `break <file>:<line>` | `b` | Set breakpoint |
| `breaks` | | List all breakpoints |
| `delete <file>:<line>` | `d` | Remove breakpoint |
| `clear` | | Clear all breakpoints |
| `cont` | `c` | Continue execution |
| `step` | `s` | Step into |
| `next` | `n` | Step over |
| `jump` | `j` | Step out |
| `print <expr>` | `p` | Print expression |
| `list [<file>:<line>]` | `l` | List source code |
| `stack` | | Show call stack |
| `memory` | `m` | Show memory stats |
| `info [class=<C>]` | `i` | Show program/class info |
| `exe <file>` | | Load binary file |
| `src <dir>` | | Set source directory |
| `args '<args>'` | | Set program arguments |
| `help` | `h` | Show help |
| `quit` | `q` | Exit debugger |

Full command documentation: [Debugger Guide](https://www.objeck.org/getting_started.html#debugger)

## Architecture

The debugger consists of:
- **Command Interpreter**: Parses and executes debugging commands
- **Breakpoint Manager**: Manages breakpoints and watchpoints
- **VM Integration**: Interfaces with the Objeck VM for execution control
- **Symbol Table**: Accesses debug symbols from compiled programs

## Implementation

- **Language**: C++ with STL
- **VM Interface**: Direct integration with Objeck VM internals
- **Symbol Format**: Custom debug symbol format embedded in .obe files

## Example Session

```bash
$ obd -b myapp.obe -src .
-------------------------------------
Objeck 2026.2.1 - Interactive Debugger
-------------------------------------

loaded binary: 'myapp.obe'
source file path: './'

> b myapp.obs:15
added breakpoint: file='myapp.obs:15'

> r
break: file='myapp.obs:15', method='Main->Main(..)'

> p count
print: type=Int/Byte/Bool, value=42(0x2a)

> stack
stack:
  frame: pos=1, class='Main', method='Main->Main(a:System.String[])', file=myapp.obs:15

> n
break: file='myapp.obs:16', method='Main->Main(..)'

> c
Program output here
> q
breakpoints cleared.

Goodbye...
```

## See Also

- [Main README](../../README.md) - Project overview
- [Compiler](../compiler/README.md) - Compiling with debug symbols
- [Virtual Machine](../vm/README.md) - VM architecture
- [API Documentation](https://www.objeck.org) - Language reference