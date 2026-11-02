# core/shared

Code and headers that more than one executable includes. The compiler, the VM, the debugger, the REPL, the updater and the launchers all read these files, so a change here changes the wire format or the behaviour of all of them; [docs/ARCHITECTURE_MAP.md](../../docs/ARCHITECTURE_MAP.md) shows how they fit.

## Files

| File | What it defines | Who must agree on it |
|---|---|---|
| `instrs.h` | the VM opcodes (`enum` of instructions), `FUNC_VALUE_TAG` and the other constants written into bytecode | compiler (`intermediate.cpp`, `emit.cpp`), VM (`loader.cpp`, `dispatch.cpp`), both JITs |
| `traps.h` | `enum Traps`, the system-service ids a `TRAP` instruction carries. **Append only**: a trap's position, counting up from `LOAD_CLS_INST_ID = -4000`, is its id in every compiled `.obl` and `.obe` | scanner, parser, emitter and `TrapProcessor::ProcessTrap`; guarded by `tools/cicd/check_trap_handlers.py` ([how to add one](../../docs/ADDING_AN_INTRINSIC.md)) |
| `version.in`, `version.h` | `VER_NUM`, `VERSION_STRING` and the `.obe`/`.obl` magic numbers. `version.h` is **generated** from `version.in` by `core/release/update_version.ps1`; edit the template | loader, linker, `obu`, every `.obl` (`tools/cicd/check_version_templates.py`, `check_obl_versions.py`) |
| `sys.h`, `sys.cpp` | inline helpers every tool uses: UTF-8 and wide-string conversion, command-line parsing, environment and library-path lookup (`GetLibraryPath`), whole-file reads, directory listing, string hashing, and `OutputStream`, which writes the zlib-compressed `.obe` and `.obl`. `sys.cpp` only includes `sys.h` | every executable |
| `int_ops.h` | the one definition of `Int` arithmetic (wraparound, shift counts mod 64, division edge cases); the interpreter, the compiler's constant folder and both JIT folders call it so a result never depends on who computed it | interpreter, optimizer, JITs |
| `num_parse.h` | whole-string number parsing that never throws (`std::stoi` and friends end the process on bad input) | VM, compiler, debugger, REPL |
| `exe_path.h`, `utf8_args.h` | the running executable's path; UTF-16 to UTF-8 command-line arguments on Windows | `obu`, launchers, tools |
| `logger.h`, `logger.cpp` | the optional compiler debug log (`OpenLogger`, `GetLogger`) | compiler |
| `styles.css` | a stylesheet that no file under `core/` refers to (the API-doc generator has its own in `core/lib/code_doc/templates/`); a candidate for removal | |

## Build

There is no executable here. `make -f make/Makefile.amd64` (also `Makefile.arm64`, `Makefile.msys2-clang.amd64`, `Makefile.msys2-ucrt.amd64`) compiles `logger.cpp` and `sys.cpp` into `sys.a` and copies it to `core/compiler/` and `core/vm/`; the compiler and VM Makefiles call it.

## Rules

- A change to `instrs.h`, `traps.h` or the file layout is a format change: bump the magic in `version.in` if old files would be misread, rebuild and commit every `core/lib/*.obl`, and say so in the pull request.
- Do not put anything here that only one executable uses.
- Keep platform headers behind `#ifdef`, as `sys.h` does: every executable includes these files.
