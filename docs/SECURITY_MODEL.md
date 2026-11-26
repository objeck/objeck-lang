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
| a method record's parameter count is between 0 and the words its declarations take (a function-typed parameter is two) | the JIT sized a table from it: a count of -255 grew it toward 2^64 entries until the process ran out of memory |
| no path of a method pops an empty operand stack, and every path into an instruction reaches it at the same stack height. The reference-map builder (`core/vm/arch/ref_map.h`, the one the JIT compiles from) walks each method that has a body | `PopInt` and `PopFloat` index the operand stack with no bound: a store or conditional jump or two-index array load missing its operands read below the stack |
| a method never holds more than 1,000 words on the operand stack above its arguments | a call guarantees 1,024 words of room (`OP_HEADROOM`, `core/vm/call_stack.h`) and nothing inside the method checks again, so a body that pushed more wrote past the reserved stack. The compiler refuses nesting far below this, and the deepest method in 684 valid programs and the libraries holds 203 |
| a non-zero integer literal is not read through as an array (a load, store or size) or as the receiver of a call or field access. A reference-typed local, return or parameter may hold one: an enum value is an `Int` typed as an object | the literal was dereferenced as an address nobody allocated |
| a number is not read through at all: the result of arithmetic, a comparison, a conversion or a float operation, an `Int` or `Float` parameter, field of the method's own instance or call result, and a local that such a value is stored to anywhere in the method | `LoadArySize`, `LoadClsInstIntVar`, the array element handlers and the call paths read memory at whatever word is on the stack (the first crash the mutation campaign reported: one byte changed, and `String:Append` took a literal for its `Char[]`) |
| "read through" includes being passed as a parameter, or returned, where the method's name spells a class of the program or an array (`o.System.String`, `i*`); an enum's name is not a class's, so an enum parameter still takes a number | the callee reads the parameter through, and so does the caller with the result |
| a float is not used as an integer or a reference, and an integer or a reference is not used as a float | the JIT keeps floats and integers in different registers and compiled such code into reads of registers it had not loaded |
| an object is not used as an array, nor an array as an object: what a local, field, parameter, return, `NEW_OBJ_INST` or `NEW_*_ARY` makes, against array loads, stores, sizes, copies and the conversions that read text on one side, and field accesses and monitors on the other (a call's receiver may be an array: a primitive array's helper functions take it there) | an array's size and dimensions sit where an object keeps its first fields |
| a class variable is read from class memory (`LOAD_CLS_MEM`) and nothing else, and class memory is never used as an object or array | the loader bounds a variable's id by the context the instruction names; the interpreter reads it from whatever is on the stack |
| an instance variable read through the method's own instance, or through an object `NEW_OBJ_INST` just made, fits that class's instance size; in a lambda, the captures declared for it | the bound above is the largest object of any class |
| a method called on an object `NEW_OBJ_INST` just made belongs to that object's class, a class it inherits from or an interface it implements | a constructor run on an object of a smaller class wrote past it |
| a method returns with exactly the words its return type names on the operand stack (checked in every method whose only traps are literal copies) | the caller goes on with the stack it had plus those words: a return one word off shifts every argument the caller passes afterwards |
| no call names a method whose body returns nothing where its type names a value (`System.Base:Try`, a compiler intrinsic's placeholder, is the only such body the compiler writes, and nothing calls it) | |
| a literal array or string is copied from an entry of its constant table that exists, into an array no larger than the entry: the compiler's five-instruction shape (dimensions, allocation, index, trap id, trap) is checked whole | `CpyCharStrAry` and its kin index the table and copy the array's length with no bound |
| a method begins with one store for each parameter word, and its parameter count is the words its name spells; the start method takes one | the JIT compiles that prologue without looking at the instructions; callers push by the count |
| `JMP`'s condition is -1, 0 or 1; `DYN_MTHD_CALL`'s parameter count is not negative and its return type is a `MemoryType` | |
| a class's instance size covers its instance declarations | the collector walks an object by its class's declarations |
| the String class id names `System.String` | the VM builds its own strings (an error's text, an argument) as instances of that class |

`run_vm_flag_tests.py` sections 30 and 34 change one operand of a freshly compiled
program for each rule and require the refusal (section 34 with the default JIT,
`--jit=off` and `--jit=1`). The stack rules were also run
over 684 regression programs and 613 example, deploy and test programs, each
compiled with every library: none is refused. The builder is run in a lenient
mode here (a local the declarations do not list is an untyped slot, and a method
it cannot model, or an instruction it has no rule for, is not a verdict); only a
failure that makes the method unsafe refuses the program.

The interpreter also checks, when an array is indexed with more than one index,
that the array has that many dimensions (`ArrayIndex`, `core/vm/interpreter.cpp`).
A virtual call that finds no method in the receiver's classes, or finds one with
no body or with other parameter or return words than its declaration, stops with
`>>> Invalid virtual call: ... <<<` (`StackInterpreter::ResolveVirtualTarget`,
checked when a class's override is first looked up, not on each call). The
collector treats a function value whose id names no class as having no captures.

`OBJECK_VERIFY_ONLY=1` makes `obr` load and verify a program and exit 0 without
running it, or refuse it as above: for checking a file, and what the tests and
the fuzz tools use to measure refusals without a program's side effects.

### How this is measured

`tools/fuzz/adhoc/obe_campaign.py` changes compiled programs one opcode,
operand, literal, instruction or record field at a time (and, for three in ten,
at the byte level as `obe_mutfuzz.py` does) and runs each result with the default
JIT, `--jit=off` and `OBJECK_JIT_THRESHOLD=1`. On 30,000 changes of 272 compiled
regression programs, 407 crashed the VM before the rules from "a number is not
read through" down were added (55 distinct faulting sites); CAMPAIGN_AFTER do with
them, and none of the byte-level changes does. What the remaining ones are, and
why the loader cannot refuse them yet, is in
[design/OBE_TYPE_VERIFIER.md](design/OBE_TYPE_VERIFIER.md).

No valid program is refused: every `.obs` in the repository that compiles on its
own, at `-opt s0` to `s3` and a quarter again with `-debug` (CORPUS_IMAGES images),
and one program per library that names every documented class (768 of 778
classes link whole), load under `OBJECK_VERIFY_ONLY=1`. Three programs did not
at first, and each was a compiler defect the stack rules exposed (CHANGELOG,
v2026.11.4: a call with arguments on the result of `??`).

## What is still not checked

- **Traps.** A trap pops as many words as its own id says, of the kinds its
  handler expects, while the builder takes the count from the instruction's
  operand and types none of them. A trap id changed to another known id, a
  changed count, or an argument of the wrong kind is not caught; that needs a
  per-trap table the compiler and the VM do not share yet. (The literal copies
  are the exception: the table above.)
- **A reference's class.** A declaration says "object", not which class. A field
  read through an object of another class, a method called on one, or two object
  arguments exchanged is not caught unless the object is the method's own
  instance or one `NEW_OBJ_INST` just made. An instance variable is otherwise
  bounded by the largest object of any class. An object-typed *field* may be
  given a number (an enum is one) and read through by another method.
- **Function values.** Nothing relates the method a function value names to the
  parameter count and return type of the `DYN_MTHD_CALL` that calls it, or to
  the closure it is called with.
- **Values nothing types.** An array element, a field of another object and a
  trap's result may be read through unchecked.
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

- **Trap, class and function-value checks**, completing the verifier: a per-trap
  table the compiler and VM share, a class id on each object declaration, and a
  signature on each function-typed one. The design, with the compiler changes it
  needs first, is [design/OBE_TYPE_VERIFIER.md](design/OBE_TYPE_VERIFIER.md). With
  them, "a crafted `.obe` is native code" becomes "a crafted `.obe` is refused".
- **Deny flags** on `obr`, for capabilities a program should not have even when
  it asks: native libraries (`DllProxy`), running commands, the file system,
  the network. Each would make the matching traps fail as a runtime error.
  Together with a complete verifier, they would let `obr` itself enforce what
  the playground currently enforces only with a container and a source scan.

Until both exist, the rule at the top of this page stands.
