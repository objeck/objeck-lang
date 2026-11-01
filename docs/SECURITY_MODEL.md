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
bytecode verifier closes; the loader's verification pass closes part of it
(below).

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
| class count no larger than the bytes left; each class id in `[0, class count)` and stored once; an interface count no larger than the ids left | `Loader::LoadClasses`, `loader.cpp:413`, `:436`, `:452` |
| method count no larger than the bytes left; each method id in `[0, method count)` and stored once | `Loader::LoadMethods`, `loader.cpp:558`, `:570` |
| instruction count no larger than the bytes left | `loader.cpp:598` |
| every opcode is known; library-only opcodes are refused in an executable | `loader.cpp:945`, `:948` |

The bounds check is off for the bundled-executable path, which loads from
memory with no length (`buffer_end = nullptr`, `loader.h:194`).

These checks are fuzzed: [tools/fuzz/loader](../tools/fuzz/loader/README.md)
holds libFuzzer targets on `Loader::Load`, seeded with the compiled regression
programs, and every input they found is a case in `run_vm_flag_tests.py`
(section 30).

## What the verification pass checks

Once a program has loaded and before any of it runs, `Loader::Verify`
(`core/vm/loader.cpp`) relates each operand to the program it is part of. A
program that breaks a rule is refused with `>>> Invalid bytecode: <method>,
instruction <n>: <rule> <<<` and status 1. Every rule holds for every program
the toolchain compiles: the 849 compiled regression programs (each links the
standard libraries; a quarter are compiled again with `-debug`) all pass.

| rule | what it closes |
|---|---|
| every `JMP`, `TRY_START` and jump-table target is an instruction of the method; a `JMP_TABLE`'s `range` slots exist and are `JMP_TABLE_SLOT`s | `Handle_JMP` and `Handle_JMP_TABLE` (`core/vm/dispatch.cpp`) set the instruction pointer from these unchecked |
| the last instruction is `RTRN` or an unconditional `JMP` | control running off the end reads past the instruction array |
| a local's id plus its width (two words for a function value) fits the frame: `id + width <= mem_size + 1` | `mem[id + 1]` (`interpreter.cpp`) on a frame of `mem_size + 2` words |
| a class variable fits the class's static memory; an instance variable fits the largest object any class makes | `cls_inst_mem[id]` |
| a memory context is `LOCL`, `INST` or `CLS`; array dimensions and `NEW_*_ARY` dimensions are at least 1 | |
| `MTHD_CALL` names a class and a method that exist; `NEW_OBJ_INST`, `OBJ_INST_CAST` and `OBJ_TYPE_OF` name a class that exists | the call paths dereferenced a null class or method |
| a parent id is a class or -1; an interface id is a class or -1 (not linked in) | |
| a declaration type is a `ParamType` (checked as it is read) | the collector walks frames and objects by these |
| `TRAP`/`TRAP_RTRN` directly follow the `LOAD_INT_LIT` that pushes a known trap id (the `Traps` enum, or `SET_SIGNAL`/`RAISE_SIGNAL`), and take at least one argument | `tools/cicd/check_trap_handlers.py` fails when a trap is added that this would refuse |
| the start method exists; with program arguments, String's constructor (method 2) does too | the initialization code calls both |

`run_vm_flag_tests.py` section 29 changes one operand of a freshly compiled
program for each rule and requires the refusal.

## What is still not checked

- **Operand-stack depth.** Nothing computes a method's maximum stack depth or
  checks that each instruction has the operands it pops. `PushInt` and `PopInt`
  index the operand stack with no bound (`core/vm/interpreter.h`); the only
  limit is checked at a call (`PushFrame`). A trap pops as many words as its own
  id says, so a depth check needs a per-trap table the compiler and the VM do
  not share yet.
- **Types.** An instance variable is checked against the largest object, not
  the object on the stack: nothing proves which class that is. Likewise a trap
  trusts whatever words the program put on the stack as its arguments -- object
  pointers included.
- **The bundled-executable path** (`buffer_end = nullptr`) is verified but not
  bounds-checked while it is read.

So the rule at the top of this page still stands: a crafted `.obe` can still
corrupt memory through the stack or through a trap's arguments.

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

- **Stack-depth and type checks**, completing the verifier: the operand-stack
  depth at each instruction consistent across every path into it, never
  negative and bounded, and the class of each object operand known. With them,
  "a crafted `.obe` is native code" becomes "a crafted `.obe` is refused".
- **Deny flags** on `obr`, for capabilities a program should not have even when
  it asks: native libraries (`DllProxy`), running commands, the file system,
  the network. Each would make the matching traps fail as a runtime error.
  Together with a complete verifier, they would let `obr` itself enforce what
  the playground currently enforces only with a container and a source scan.

Until both exist, the rule at the top of this page stands.
