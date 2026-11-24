# Objeck Debugger (obd)

A command-line debugger for Objeck programs, providing breakpoints, stack inspection, and step-by-step execution. The debugger operates as a command-driven interpreter that works with programs compiled with debug symbols.

**Status:** implemented and shipped (`obd`, command line and DAP). The debugger is a **separate build of the VM sources** (`-D_DEBUGGER -D_NO_JIT`: `core/vm/make/Makefile.amd64.obd`, `core/debugger/vs/debugger.vcxproj`), with its own interpreter and **no JIT**. Two consequences for anyone using it as evidence about a program: it cannot show a bug that only appears in compiled code (bisect those with `obr --jit=off`, then `OBJECK_JIT_REPORT=1`), and the collector, the write barrier and the heap layout it inspects are the interpreter's, not the JIT's. It reads object and collection layouts directly from VM memory through `obj_layout.h`, so a change to an object layout must be mirrored there (the architecture assessment lists this as a risk, finding 8.1).

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
| `break <file>:<line> [if <expr>]` | `b` | Set breakpoint (optionally conditional) |
| `break <Class>-><Method>` | `b` | Break at a method's first body line |
| `tbreak <file>:<line>` | | Temporary (one-shot) breakpoint |
| `breaks` | | List all breakpoints (with ids/state) |
| `delete <file>:<line>` | `d` | Remove breakpoint |
| `enable [<id>]` / `disable [<id>]` | | Enable/disable breakpoint(s) |
| `ignore <id> <count>` | | Skip the next `<count>` hits of a breakpoint |
| `clear` | | Clear all breakpoints |
| `watch <expr>` | | Break when `<expr>` changes (data breakpoint) |
| `watches` | | List watchpoints |
| `unwatch [<id>]` | | Remove watchpoint(s) |
| `cont` | `c` | Continue execution |
| `step` | `s` | Step into |
| `next` | `n` | Step over |
| `jump` | `j` | Step out |
| `until <line>` | | Run until `<line>` in the current frame |
| `print <expr>` | `p` | Print expression |
| `set <var> = <expr>` | | Assign a new value to a live variable |
| `locals` | | Print all locals in the selected frame |
| `frame [<n>]` | `f` | Show/select a stack frame |
| `up` / `down` | | Move to the caller / callee frame |
| `list [<file>:<line>]` | `l` | List source code |
| `stack` | | Show call stack |
| `memory` | `m` | Show memory stats |
| `info [class=<C>]` | `i` | Show program/class info |
| `exe <file>` | | Load binary file |
| `src <dir>` | | Set source directory |
| `args '<args>'` | | Set program arguments |
| `help` | `h` | Show help |
| `quit` | `q` | Exit debugger |

An empty line (just pressing Enter) repeats the previous command, which is handy for repeated `step`/`next`.

Full command documentation: [Debugger Guide](https://www.objeck.org/getting_started.html#debugger)

## DAP Mode (VS Code Debugging)

The debugger includes a built-in [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) server for VS Code integration:

```bash
obd --dap
```

In DAP mode, the debugger communicates over stdin/stdout using JSON-RPC with Content-Length framing. VS Code launches `obd --dap` automatically when you press F5 with the Objeck extension installed.

**Supported DAP requests** (the dispatch in `dap.cpp`): initialize, launch, restart, terminate, disconnect, setBreakpoints, setFunctionBreakpoints, setExceptionBreakpoints, breakpointLocations, dataBreakpointInfo, setDataBreakpoints, configurationDone, threads, stackTrace, scopes, variables, setVariable, setExpression, evaluate, completions, exceptionInfo, modules, loadedSources, continue, next, stepIn (with an optional `targetId`), stepInTargets, stepOut, pause, gotoTargets, goto, cancel, disassemble. `attach` is answered with an error ("Attach is not supported; use a launch configuration"), as is any other request, so a client is never told a request it asked for succeeded when it did not. `supportsStepBack` is false.

**Features:**
- Conditional breakpoints (expression evaluation) and hit-count conditions
- Data breakpoints (watchpoints)
- Function breakpoints (`Class->Method`, `Class.Method`, or a bare method name)
- Exception breakpoints (break on an uncaught Objeck runtime error)
- Logpoints (breakpoints with a `{expr}`-interpolated message that log and continue)
- Variable inspection (locals, parameters, instance/class fields)
- Setting variable values from the editor (`setVariable`, Int/Char/Float locals)
- Step into one call of a statement (`stepInTargets`, then `stepIn` with a `targetId`; calls run in bytecode order, which for `a() + b()` is `b()` first)
- Set the next statement (`gotoTargets` and `goto`): inside the executing method, from a statement start to a statement start, never across a `try`; otherwise an error that says why and no change
- Cancel a request (`cancel`); `OBJECK_DAP_TEST_DELAY_MS` holds `evaluate` and `variables` that many milliseconds (until cancelled), for the tests
- Disassemble a frame's method (`disassemble`; frames carry an `instructionPointerReference`; an address packs class, method and instruction index and counts one per instruction)
- `invalidated` events (after `setVariable`, `setExpression`, `goto`) and `progressStart`/`progressUpdate`/`progressEnd` events (launch and program start-up), sent only to a client that declared `supportsInvalidatedEvent` / `supportsProgressReporting`
- Not offered: `restartFrame` (a frame cannot be re-entered: its parameters are popped into locals at entry and the frames above it hold operand-stack words) and `terminateThreads` (a thread cannot be ended without leaving its locks held)
- In-process restart (re-runs the program without relaunching the adapter)
- Call stack navigation
- ANSI color output

See [docs/editors.md](../../docs/editors.md) for VS Code setup instructions.

## Architecture

The debugger consists of:
- **Command Interpreter**: Parses and executes debugging commands
- **DAP Adapter**: JSON-RPC protocol handler for VS Code integration (`dap.h`/`dap.cpp`)
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

## How to check

- Command line: `programs/regression/run_debugger_tests.sh` or `.cmd` (the POSIX one needs `expect`); 14 tests per the regression `TESTS.md`.
- DAP: `python programs/regression/run_dap_tests.py <bin_dir>`.
- Capabilities the adapter advertises: send `initialize` and read the response, or read `DapAdapter::HandleInitialize` in `dap.cpp`.

## Known limits

- Launch only; no attach.
- No JIT: the debugger interprets every method.
- Expression evaluation uses its own scanner and parser (`scanner.cpp`, `parser.cpp` here), a fork of the compiler's ([ARCHITECTURE_ASSESSMENT_2026_11.md](../../docs/ARCHITECTURE_ASSESSMENT_2026_11.md) finding 8.3), which is a risk for expressions the two grammars treat differently.

## See Also

- [Main README](../../README.md) - Project overview
- [Compiler](../compiler/README.md) - Compiling with debug symbols
- [Virtual Machine](../vm/README.md) - VM architecture
- [API Documentation](https://www.objeck.org) - Language reference