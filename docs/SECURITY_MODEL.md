# The Objeck VM trust model

**Short version: running an `.obe` is equivalent to running native code with
the privileges of the `obr` process.** The VM checks that a program file is
well formed enough to load; it does not check that the bytecode inside is safe
to execute. Do not run an `.obe` you would not run as a native executable, and
do not treat `obr` as a sandbox. Isolation, where it is needed, comes from the
operating system around the VM -- as the playground does with Docker.

This page says what the loader and interpreter check today, what they do not,
what the web playground relies on, and what is planned. Every statement about
the code cites the file and line it was read from (master, 2026-10-31); line
numbers drift, so the function names are given as well.

Reporting a vulnerability is covered in [SECURITY.md](../SECURITY.md).

---

## Who can do what

There are two ways for a program to do something dangerous, and the VM stops
neither.

1. **By asking.** A correct program compiled from ordinary source can read and
   write any file the process can, open sockets, run shell commands
   (`TrapProcessor::SysCmd` passes its argument to `_wsystem`/`system`,
   `core/vm/common.cpp:4195-4213`), and load native code: `DllProxy->New(name)`
   appends `name` to the native library directory with no check for path
   separators and loads the result (`StackInterpreter::SharedLibraryLoad`,
   `core/vm/interpreter.cpp:3080-3132` for `LoadLibraryW`, `:3198` for
   `dlopen`), so a name with `../` in it loads any file the process can read
   that carries the platform's library suffix, which the VM appends. A native library runs in-process with no restriction at
   all (see [native_interface.md](native_interface.md)).
2. **By lying.** An `.obe` that was not produced by `obc` -- or was edited after
   -- can contain instructions the compiler never emits. The interpreter trusts
   instruction operands, so such a file can read and write memory outside the
   VM's own structures. That makes a crafted `.obe` equivalent to native code
   even with every library withheld.

The first is the language working as designed. The second is the gap a
bytecode verifier would close (below).

---

## What the loader checks

`Loader::Load` and friends (`core/vm/loader.cpp`, `core/vm/loader.h`) validate
the file's *structure*:

| check | where |
|---|---|
| the toolchain version matches the VM's (`VER_NUM`), else refuse | `loader.cpp:71` |
| the magic number names an executable, not a library or unknown file | `loader.cpp:86` |
| every read stays inside the decompressed buffer (`EnsureBytes`) | `loader.h:65` |
| counts and lengths that size an allocation are non-negative and no larger than the bytes left (`ReadCount`) | `loader.h:84` |
| class count non-negative; each class id in `[0, class count)` before it is stored | `loader.cpp:337`, `:354` |
| method count non-negative; each method id in `[0, method count)` before it is stored | `loader.cpp:455`, `:468` |
| instruction count no larger than the bytes left | `loader.cpp:598` |
| every opcode is known; library-only opcodes are refused in an executable | `loader.cpp:945`, `:948` |

The bounds check is off for the bundled-executable path, which loads from
memory with no length (`buffer_end = nullptr`, `loader.h:194`).

## What the loader does not check

Operands are stored as read (`Loader::LoadStatements`, `loader.cpp:591-961`).
Nothing relates them to the program they are part of:

- **Jump targets.** `JMP`'s label operand is stored unchanged
  (`loader.cpp:809`) and becomes the instruction pointer when the jump is
  taken, with no comparison against the method's instruction count
  (`Handle_JMP`, `core/vm/dispatch.cpp:649`, `:652`). `JMP_TABLE` checks the
  selector against its own `range` (`dispatch.cpp:950`) but not `range` against
  the instructions that follow, and the slot it reads supplies the next
  instruction pointer unchecked (`dispatch.cpp:952`).
- **Local and field ids against the frame or object size.** A method's
  `mem_size` is read (`loader.cpp:484`) and sizes its frame (`mem_size + 2`
  words, `StackMethod::NewMemory`, `core/vm/common.h:1276`), but a load or store
  operand is never compared with it: `mem[instr->GetOperand() + 1]` is read and
  written directly (`core/vm/interpreter.cpp:596`, `:889`, `:1754`, `:1821`,
  `:1840`).
- **Trap ids and their arguments.** `TRAP`'s operand is stored as read
  (`loader.cpp:626`); the trap id itself is popped from the operand stack at
  run time (`TrapProcessor::ProcessTrap`, `core/vm/common.cpp:2403`). An
  unknown id fails the call (the switch falls through to `return false`,
  `common.cpp:3225`), but a known trap trusts whatever words the program put on
  the stack as its arguments -- object pointers included.
- **Operand-stack depth.** Nothing computes a method's maximum stack depth or
  checks that each instruction has the operands it pops. `PushInt` and `PopInt`
  index the operand stack with no bound (`core/vm/interpreter.h:540`, `:527`);
  the only limit is checked at a call (`PushFrame`, `interpreter.h:307`).
- **Call targets.** A `MTHD_CALL`'s class and method ids (`loader.cpp:793`) are
  looked up at run time; `StackProgram::GetClass` and `StackClass::GetMethod`
  bounds-check and return `nullptr` (`common.h:2060`, `:1497`), but the call
  paths dereferenced that result (`interpreter.cpp:2439`, `:2550`, `:2611`,
  `:2655`). #1494 reports it as a runtime error instead; it is the
  one item on this list with a run-time guard.

---

## The playground

[playground.objeck.org](https://playground.objeck.org) runs untrusted programs.
It does not rely on the VM for isolation. Its backend
(`programs/web-playground/backend`) compiles the visitor's *source* with `obc`
-- it never accepts an `.obe` -- and then runs it in a Docker container
(`app/services/sandbox.py:206-236`):

- `network_mode="none"` (`sandbox.py:211`)
- a read-only root file system, and a 10 MB `/tmp` tmpfs mounted
  `nosuid,nodev,noexec` (`sandbox.py:212`, `:215`; size in `app/config.py:22`)
- 128 MB of memory with no swap, half a CPU, and at most 32 processes
  (`sandbox.py:216-220`; values in `app/config.py:19-21`)
- every capability dropped and `no-new-privileges` (`sandbox.py:227-228`)
- a non-root `sandbox` user (`sandbox.py:235`)
- `timeout` on both the compile and the run (`container_command`,
  `sandbox.py:150-178`)

Before that it refuses source that names bundles or calls the sandbox would not
honour anyway -- `System.API`, `System.IO.Filesystem`, `System.IO.Net`
(`app/services/libmap.py:61-65`), `DllProxy`, process and socket classes
(`app/services/validator.py:86-101`) -- so a visitor gets a reason rather than a
failure. That check is a lexical scan, by its own description "deliberately not
a parser" (`app/services/source_scan.py:13`); it improves the error messages
and is not the security boundary. The container is.

Anyone hosting Objeck for untrusted users should do the same: treat the VM
process as hostile and contain it from outside.

---

## Planned

Neither of these exists yet; both are planned, and this section changes when
they land.

- **A bytecode verifier**, run by the loader before anything executes, so that
  a crafted `.obe` is refused rather than trusted. It would check, per method:
  every jump and jump-table target is an instruction index inside the method;
  every local and field id is below the frame or object size; every call names
  a class and method that exist; every trap id is known; and the operand-stack
  depth at each instruction is consistent across every path into it, never
  negative and bounded -- the checks listed as missing above. That turns "a
  crafted `.obe` is native code" into "a crafted `.obe` is refused".
- **Deny flags** on `obr`, for capabilities a program should not have even when
  it asks: native libraries (`DllProxy`), running commands, the file system,
  the network. Each would make the matching traps fail as a runtime error.
  Together with the verifier, they would let `obr` itself enforce what the
  playground currently enforces only with a container and a source scan.

Until both exist, the rule at the top of this page stands.
