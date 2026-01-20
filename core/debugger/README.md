# Objeck Debugger (obd)

A command-line debugger for Objeck programs, providing breakpoints, stack inspection, and step-by-step execution. The debugger operates as a command-driven interpreter that works with programs compiled with debug symbols.

## Features

- **Breakpoints**: Set breakpoints at specific lines or methods
- **Step execution**: Step into, over, and out of method calls
- **Stack inspection**: View call stack and local variables
- **Expression evaluation**: Evaluate expressions in the current context
- **Watchpoints**: Monitor variable changes
- **Interactive REPL**: Execute code in the debugging context

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
obd myapp.obe

# Common debugger commands
(obd) break MyClass:42        # Set breakpoint at line 42 in MyClass
(obd) run                     # Run program until breakpoint
(obd) step                    # Step to next line
(obd) next                    # Step over method calls
(obd) print variable          # Print variable value
(obd) stack                   # Show call stack
(obd) continue                # Continue execution
(obd) quit                    # Exit debugger
```

## Commands Reference

| Command | Shortcut | Description |
|---------|----------|-------------|
| `break <location>` | `b` | Set breakpoint |
| `run [args]` | `r` | Start/restart program |
| `continue` | `c` | Continue execution |
| `step` | `s` | Step into |
| `next` | `n` | Step over |
| `finish` | `f` | Step out |
| `print <expr>` | `p` | Print expression |
| `stack` | `bt` | Show backtrace |
| `list` | `l` | List source code |
| `info` | `i` | Show program info |
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
$ obd myapp.obe
Objeck Debugger v2026.2.0
Loading program: myapp.obe

(obd) break Main:15
Breakpoint 1 set at Main:15

(obd) run
Running program...
Hit breakpoint 1 at Main:15

(obd) print count
count = 42

(obd) stack
#0  Main:15
#1  Main:Main(String[])

(obd) next
Main:16

(obd) continue
Program exited normally
```

## See Also

- [Main README](../../README.md) - Project overview
- [Compiler](../compiler/README.md) - Compiling with debug symbols
- [Virtual Machine](../vm/README.md) - VM architecture
- [API Documentation](https://www.objeck.org) - Language reference