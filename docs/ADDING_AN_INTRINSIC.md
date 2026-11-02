# Adding an intrinsic (a new trap)

An *intrinsic* is a bare keyword that only the library sources may use, such as
`THREAD_IS_ALIVE;` in `core/compiler/lib_src/lang.obs`. The compiler lowers it to a
`TRAP` instruction whose operand-stack id names a C++ handler in the VM. This is how
`Runtime->GetProcessId()`, `Base->GetInstanceID()` and most of the system library reach
the operating system. A user program cannot write one: the keywords exist only in the
bootstrap compiler, `sys_obc`, which is built with `-D_SYSTEM` and compiles `lang.obs`.

This page lists every site a new intrinsic touches, in the order to touch them, with
`Runtime->TestAddressOf` (PR #1483, commit `e5b6a177da`, trap `GC_TEST_ADDRESS`) as the
worked example, and what each omission looks like. `python3 tools/cicd/check_trap_handlers.py`
checks the sites marked **[checked]**; CI runs it in the `tools` job and
`tools/cicd/local_ci.py` runs it on your machine in under a second.

Sites are named by function, not line number, because line numbers rot.

## The sites

| # | File | What to add | `TestAddressOf` |
|---|------|-------------|-----------------|
| 1 | `core/shared/traps.h` | an enumerator at the **end** of `enum Traps` | `GC_TEST_ADDRESS` after `INST_STABLE_ID` |
| 2 | `core/compiler/scanner.h` | the token, inside the `#ifdef _SYSTEM` part of `ScannerTokenType` | `GC_TEST_ADDRESS,` after `THREAD_IS_ALIVE` |
| 3 | `core/compiler/scanner.cpp` | `ident_map[L"NAME"] = NAME;` in `Scanner::LoadKeywords`, inside its `#ifdef _SYSTEM` block **[checked]** | `ident_map[L"GC_TEST_ADDRESS"] = GC_TEST_ADDRESS;` |
| 4 | `core/compiler/parser.cpp` | a `case NAME:` in `Parser::ParseStatement` that builds `MakeSystemStatement(..., instructions::NAME)`, then `NextToken()` **[checked]** | the new case after `THREAD_IS_ALIVE` |
| 5 | `core/compiler/intermediate.cpp` | a `case instructions::NAME:` in `IntermediateEmitter::EmitSystemDirective` **[checked]** | loads the argument, writes the id, `TRAP_RTRN, 2L` |
| 6 | `core/vm/common.cpp` | a `case NAME:` in `TrapProcessor::ProcessTrap` **[checked]** | the handler, which pops the argument and pushes the result |
| 7 | `core/compiler/lib_src/lang.obs` (or another `lib_src` file) | the library method whose body is the bare keyword, with a doc comment | `function : TestAddressOf(o : Base) ~ Int { GC_TEST_ADDRESS; }` |
| 8 | `core/lib/lang.obl` (or the other `.obl` you changed) | rebuild and commit it | 31 bytes larger |
| 9 | `programs/regression/` | a test that calls the new method | `vm_flag_fixtures/gc_address_int_pending.obs`, run by section 28 of `run_vm_flag_tests.py` |

### 1. `traps.h`: append, never insert

Only the first enumerator of `enum Traps` has an explicit value (`LOAD_CLS_INST_ID = -4000`); the rest count up from it, so an enumerator's position **is** its wire id, and
that id is written into every compiled `.obl` and `.obe`. Insert one in the middle and
every trap after it silently changes meaning. Put the new one last (after the comment
that says so) and give it a comment naming the method. Never delete one either: that
renumbers the rest. The two dead enumerators `LOAD_CLS_BY_NAME` and `FILE_LOGGER_DIR`
are still there for that reason.

### 2 and 3. The scanner

The token enum and the keyword map are separate lists, and the map is the authority:
a word that is missing from `ident_map` scans as an ordinary identifier. Keep the
spelling in the map identical to the enumerator's name, which is what lets every other
site be found with one `grep`.

### 4. The parser

`ParseStatement` has a long `#ifdef _SYSTEM` run of cases, one per intrinsic. Copy a
neighbour and change **both** names: the `case` label and the `instructions::NAME`
argument. A copy that keeps the neighbour's argument parses cleanly and then runs the
neighbour's trap **[checked]**.

### 5. The emitter

`EmitSystemDirective` turns the statement into instructions. The shape is fixed:

1. push each argument, `LOAD_INT_VAR n, LOCL` (or `LOAD_FLOAT_VAR`, or `LOAD_INST_MEM`
   for `@self` when the method is an instance method), in the order the handler pops
   them in reverse;
2. `MakeIntLitInstruction(..., instructions::NAME)`, the trap id;
3. `TRAP, n` or `TRAP_RTRN, n`, where `n` counts the values pushed **including the id**
   (`TestAddressOf` pushes one argument and the id: `2L`).

`TRAP_RTRN` is the form for a handler that pushes a result word; `TRAP` is for one that
pushes nothing. The JIT and the reference-map verifier (`core/vm/arch/ref_map.h`) model
a plain `TRAP` as pushing nothing, so a value-returning trap written as `TRAP` is only
safe as the last thing a method does before its return. Several older cases do exactly
that; for a new one use `TRAP_RTRN`.

### 6. The VM handler

`ProcessTrap` pops the id, then switches on it. It has **no `default:`**: an id with no
case falls out of the switch and returns false, `Handle_TRAP` (`core/vm/dispatch.cpp`)
unwinds the stack and the process exits with status 1. The handler pops its arguments
in reverse order of pushing and pushes the result. Remember:

* anything that reads or writes the interpreter's `frame`, or the instruction pointer
  `inst`, is a *frame-dependent* trap. Compiled code calls `ProcessTrap` with
  `frame == nullptr`, so such a trap must be listed in `HasFrameDependentTrap()` in
  `core/vm/arch/jit/jit_common.h`, which keeps the method out of the JIT;
* a handler that holds an object across an allocation or a blocking call must keep it
  reachable for the collector (see `StdInString` in `common.cpp` for the pattern) and an
  object stored into another object needs the write barrier (`docs/native_interface.md`);
* the handler is shared by the interpreter and by JIT-compiled code, so test both
  (`OBJECK_JIT_THRESHOLD=1`, which the Linux CI leg also runs).

`GC_TEST_ADDRESS` is deliberately inert unless `OBJECK_GC_TEST_HOOKS=1`; its `static
const bool hooks` reads the variable once. A test-only trap should be gated like this
so ordinary programs cannot reach it.

### 7. The library method

The method body is nothing but the keyword, with a trailing semicolon. Give it a doc
comment (`#~ ... ~#`, ending any code fence it opens: `check_doc_fences.py` enforces
that). A new public method also changes the editor API index; `tools/cicd/check_api_index.py`
(run by CI against a built tree) fails when `tools/lsp/server/objk_apis.json` is stale,
so regenerate it with `tools/lsp/server/doc_json/gen_json.sh`.

### 8. Rebuild `lang.obl` and commit it

The library is compiled by `sys_obc`, which only builds on Linux (use WSL2 on Windows):

```
cd core/compiler && ./update_version.sh        # sys_obc, then lang.obl, then every other .obl
```

`core/lib/*.obl` are **tracked** and are copied, not rebuilt, into deploy trees. The
Linux and macOS CI legs regenerate `lang.obl` from source, which hides a forgotten
commit there, but the windows-x64 leg links the committed file: it fails (or, worse,
passes with a library that lacks the method) if you did not commit the rebuilt one.
For the same reason a local deploy tree may be stale: `cp core/lib/*.obl core/release/deploy*/lib/`.

### 9. A test

A regression test (`programs/regression/*.obs`, exit code 0 passes) or, for something
that needs flags, a fixture and a section in `programs/regression/run_vm_flag_tests.py`.
Run it interpreted and with `OBJECK_JIT_THRESHOLD=1`.

## What goes wrong when a site is forgotten

| Forgotten | What you see |
|-----------|--------------|
| `traps.h` | does not compile, which is the kind one |
| `traps.h`, inserted mid-enum | **nothing now**; every library built before the change misbehaves or exits |
| scanner token / `ident_map` | the keyword scans as an identifier: a parse error naming the line in `lang.obs` |
| parser case | the same parse error. The scanner and emitter are fine, so it looks as if the scanner were at fault. `SOCK_TCP_SSL_SRV_CERT` is in this state today: it has a scanner token, an emitter case and a VM handler, but the parser cannot build it, so no program can reach it **[checked]** |
| parser case names the wrong `instructions::` | the method silently runs another trap **[checked]** |
| emitter case | `EmitSystemDirective` falls to its `default:`, which stops the build with "internal error: no code emitted for system directive N" (it used to compile to nothing: all 18 `SOCK_DTLS_*` traps were dead that way) **[checked]** |
| `ProcessTrap` case | builds, links, passes every test that does not call it, then kills the program with exit status 1 and a stack unwind the first time it runs **[checked]** |
| `lang.obl` not rebuilt | the new method does not exist in the shipped library; windows-x64 CI fails or the call is a compile error |
| `HasFrameDependentTrap` | a JIT-compiled method crashes on a null frame, only when hot, only under `--jit` |
| `TRAP` where `TRAP_RTRN` belongs | the interpreter works; the JIT or the verifier reports a missing return value |

The most expensive entry used to be an edit in the wrong place. `Parser` once carried a
second copy of the whole intrinsic switch, `Parser::ParseSystemStatement`, about 1,900
lines that nothing called. The live cases are the inline `#ifdef _SYSTEM` run inside
`ParseStatement`; a patch to the dead copy compiled, changed nothing, and printed no
error. It was deleted when this page was written. If you find a second copy of any of
these lists, assume one of them is dead and prove which before editing.

## What `check_trap_handlers.py` does and does not cover

It parses `traps.h`, `scanner.cpp`, `ParseStatement`, `EmitSystemDirective` and
`ProcessTrap` and fails when:

* a `Traps` enumerator has no `case` in `ProcessTrap` and is not on its allowlist
  (`NOT_VM_TRAPS`, each entry with a written reason: *dead* or *pseudo-op*);
* an allowlist entry has gone stale (it gained a handler, the compiler started to emit it,
  something started to mention a "dead" one);
* a `_SYSTEM` keyword has no parser case, or its parser case builds a different
  intrinsic, or it has no emitter case (`NOT_PARSED_INTRINSICS` is the allowlist);
* the emitter writes a trap id the VM has no `case` for.

Run `python3 tools/cicd/check_trap_handlers.py --test` to see the negative controls: the
script mutates the real sources, one site at a time, and fails unless each break is
reported.

It does **not** check: that the handler pops as many values as the emitter pushes, the
`TRAP` operand count, `HasFrameDependentTrap`, the `lang.obs` method, or that `lang.obl`
was rebuilt. Those are on you, and the regression suite is the oracle for the rest.

## A new VM *instruction* is a different, larger job

An opcode such as `LOAD_ARY_SIZE` or `F2S` lives in `core/shared/instrs.h`
(`enum InstructionType`) rather than `traps.h`, and also needs the interpreter
(`core/vm/dispatch.cpp`), both JIT back ends, the loader (`core/vm/loader.cpp`), the
assembler listing (`core/compiler/emit.cpp`) and the reference-map verifier. Prefer a
trap unless the operation must be as cheap as a machine instruction.
