# The instruction table

Every bytecode opcode is described once, in [`core/shared/instr_info.h`](../core/shared/instr_info.h): its
operands and what each one *is*, how many words it pops and pushes, and flags (jump, terminator, may trap,
side effect, and so on). [`tools/cicd/check_instr_table.py`](../tools/cicd/check_instr_table.py) proves that the
places that used to restate this by hand agree with it.

**Status: phase 1 of two.** The table only *describes*. No code is driven by it, no opcode was renumbered or
reordered, and no byte the compiler writes or the VM reads changed (verified: see [What was checked](#what-was-checked)).
Phase 2 drives the code from it ([below](#phase-2)).

## Why

What an opcode carries was written out by hand in a dozen places, and nothing compared them:

| Site | What it restates |
|---|---|
| `core/shared/instrs.h` | the opcode enum. The numbers **are** the `.obe`/`.obl` format |
| `core/compiler/emit.cpp` `IntermediateInstruction::Write` | the operands written, per opcode |
| `core/compiler/linker.cpp` `Library::LoadStatements` | the `.obl` reader, per opcode |
| `core/vm/loader.cpp` `Loader::LoadStatements` | the `.obe` reader and its verification, per opcode |
| `core/vm/dispatch.cpp` `instr_dispatch` | the handler for each opcode, by position |
| `core/compiler/optimization.cpp` | `MaxLabel`, `CleanLabelsLocation`, `JumpToLocation`, `CanInlineMethod`, and the inliner's slot and label shifting |

Forget one and the failure is late and quiet: a library that links into the wrong code, an inlined body whose
labels collide with its caller's, a switch that falls into its `default` case.

## A row

```cpp
OBJK_ROW(JMP_TABLE, IMM, COUNT, LABEL, NONE, 1, 0, IFLAG_JUMP | IFLAG_TERMINATOR, "pops the selector; ...")
//       name       op1  op2    op3    wide  pops pushes flags                    note
```

* **op1, op2, op3** are the roles of `operand`, `operand2` and `operand3` (`StackInstr::GetOperand`,
  `GetOperand2`, `GetOperand3`): `IMM`, `CHAR`, `LABEL` (a reference), `LABEL_DEF` (`LBL`), `SLOT`, `CTX` (the
  memory context of a slot: `CLS`, `INST`, `LOCL`), `CLASS`, `METHOD`, `COUNT`, `FLAG`, `COND`, `TYPE`, `SIZE`,
  `NONE`, and `RUNTIME` (set by the VM after load, never in a file). `JMP_TABLE` keeps its label in
  **operand3**; the role says so, and the checker holds the label passes to it.
* **wide** is the field that does not fit those three: `I64` (`operand7`, the Int literal), `F64` (`operand4`),
  `STR` (`operand5`) or `STR2` (`operand5` and `operand6`).
* **pops, pushes** are operand-stack *words*: a Float is one, a function value is two. `STACK_VAR` means the count
  depends on something other than the opcode (a call's callee, a trap's id, an array's dimension count); the note
  says what. `IFLAG_CTX_ADDR` means one more word, the base address, is popped unless operand2 is `LOCL`.
* **flags**: `JUMP` (names a jump target), `TERMINATOR`, `CALL`, `MAY_TRAP`, `SIDE_EFFECT`, `ALLOCATES`,
  `CTX_ADDR`, and the status flags `VM_ONLY` (made by the loader, never in a file), `LIB_ONLY` (in a `.obl`
  only; the linker rewrites it), `PSEUDO` (an id used as a trap id, or a marker; never an instruction) and
  `RESERVED` (in the enum but nothing emits, reads or runs it).

`CarriesLabel()` and `CarriesSlot()` are derived from the roles, not stored, so they cannot disagree with them.

The file layout follows from the roles: the used operands in operand, operand2, operand3 order (a `CHAR` as a
`wchar`, the rest as 32-bit ints), then the wide field, after the type byte (and the line number, in a debug
build). The checker derives that and compares it with what the writer and both readers do.

Three `static_assert`s keep the table honest at compile time: one row per enumerator, in enum order, and a set of
rules every row must obey (a jump carries a label; a slot comes with its context). `instrs.h` includes the table,
so **adding an opcode without a row does not compile**.

## Adding an opcode (today)

New opcodes are **appended** (before `END_STMTS`, which sits mid-list for historical reasons: see the comment on
`NEW_OBJ_ARY`), so every existing number keeps its meaning. Then, in this order:

1. `core/shared/instrs.h`: the enumerator. Bump `MAGIC_NUM_EXE` / `MAGIC_NUM_LIB` in `core/shared/version.h`:
   a file that holds the opcode must not load in a VM that does not know it.
2. `core/shared/instr_info.h`: the row, at the same position. Fill in every field from the code you are about
   to write; a wrong row fails the checker.
3. `core/vm/dispatch.cpp`: `Handle_NAME` and its entry in `instr_dispatch`, in enum order (the checker compares
   the order; a `static_assert` there compares the count).
4. `core/compiler/emit.cpp`: a `case` in `IntermediateInstruction::Write` (and `Debug`, for `obc -asm`).
5. `core/compiler/linker.cpp`: a `case` in `Library::LoadStatements` that reads exactly what `Write` wrote.
   If the opcode is `LIB_ONLY`, also resolve it in `Linker::ResolveExternalMethodCalls`.
6. `core/vm/loader.cpp`: a `case` in `Loader::LoadStatements` that reads the same, verifies the operands, and calls
   `CheckTarget` on a label.
7. `core/vm/interpreter.cpp` and both JITs (`core/vm/arch/jit/amd64`, `core/vm/arch/jit/arm64`).
8. If the opcode carries a **label**: `MaxLabel`, `CleanLabelsLocation`, `JumpToLocation`, and the inliner (the
   label-collision scan and the shifting copy in `InlineMethod`, or have `CanInlineMethod` refuse a callee that
   holds it). If it carries a **slot**: the inliner's shifting copy, and `CanInlineMethod`'s non-local check.
9. Rebuild the libraries (the `.obl` files embed the opcode set through the version) and run
   `python3 tools/cicd/check_instr_table.py`.

## What the checker covers

`python3 tools/cicd/check_instr_table.py` (about a second; `--test` breaks each site in memory and fails unless
the checker notices). It runs in `ci-build.yml`'s `tools` job as an **advisory** step for now, and in
`tools/cicd/local_ci.py`.

| | Checks |
|---|---|
| a | the table has one row per enumerator, in order, and still carries its `static_assert`s |
| b | the operands `Write` writes, and the operands each reader reads, equal the row's roles (order and kind); the instruction header (type byte, then line) is read in the order it is written; `VM_ONLY`, `LIB_ONLY`, `PSEUDO` and `RESERVED` opcodes are not accepted by the `.obe` loader |
| c | every label-carrying opcode is in `MaxLabel`, `CleanLabelsLocation`, `JumpToLocation`, the inliner's label scan and its shifting copy (or `CanInlineMethod` refuses it), reading the operand the table says; every slot-carrying opcode is in the inliner's shift and in `CanInlineMethod`'s non-local check |
| d | every opcode that names a jump target is checked by the loader's `CheckTarget` |
| e | every `LIB_ONLY` opcode is resolved by the linker |
| f | `instr_dispatch` lists the handlers in enum order |

The functions are found by name in every `core/compiler/*.cpp`, not by line number, so a pass that moves to
another file is still found, and one that is renamed fails loudly rather than being skipped. An exemption or a
known problem is listed in the script with its reason, and the script warns when the entry is no longer needed.

**What it cannot find.** It compares what the sites *say*; it does not run them. In particular it does not check
the stack counts or the `MAY_TRAP` / `SIDE_EFFECT` flags against the interpreter or the optimizer's purity lists
(`IsEffectFreeRange` and friends): those are read from the code once and are documentation until phase 2 uses
them. It does not read the JIT, `obc -asm`'s `Debug` printer, the debugger or the language server.

## What it found

Writing the table from the code, and running the checker, turned up:

* **A real bug.** `Library::LoadStatements` (the `.obl` reader) reads each instruction's line number *before* its
  type byte, but `Write` writes the type, then the line. A library built with `obc -tar lib -debug` links without
  complaint into scrambled code, and the program is refused at load (`the method can run past its last
  instruction`). Listed in the script's `KNOWN` with its fix.
* **Drift that is harmless today, now stated.** `TRY_START` is missing from the inliner's label scan and shift:
  its handler label is always also defined by an `LBL` (which the scan includes), and `CanInlineMethod` refuses a
  callee holding a `TRY_START`, so the shift never sees one. The checker accepts that because *both* facts hold,
  and fails the day either stops being true. `JMP_TABLE` keeps its label in operand3, and every pass reads it
  from there; the checker holds them to it.
* **A dead opcode.** `COPY_FUNC_VAR` is in the enum, written by `Write` and listed by the inliner, but nothing
  creates it and neither reader nor the interpreter handles it (the dispatch entry is a placeholder). Flagged
  `RESERVED`; it cannot be removed, because that would renumber everything after it.
* **A hazard that is not an opcode mismatch.** `IntermediateInstruction`'s constructors leave the operands a row
  does not use *uninitialized*. Nothing reads an unused operand today, but phase 2's generic passes will.

## What was checked

The table was derived from the code, then the build was proven unchanged: a clean rebuild of `obc` and `obr`
with the header in every translation unit; all 52 libraries rebuilt with `build_libs.sh` are byte-identical to the
committed `.obl` files; and every program in `programs/regression` compiled with the old and the new `obc` at
`-opt s0`, `s1`, `s2`, `s3` and with `-debug` gives byte-identical `.obe` files. Linux x86-64 only; Windows, macOS
and ARM64 are verified by CI.

## Phase 2

Make the table the source and the hand-written sites its users:

1. **Generate the reader and the writer.** `Write`, both `LoadStatements`, and the loader's verifier hooks become
   one table-driven function each, parameterised by the role (`LABEL` gets `CheckTarget`, `SLOT` gets
   `CheckSlot`, `CLASS` gets `CheckClassId`). The per-opcode switches (about 700 lines) go away.
2. **Drive the label passes and the inliner from the roles.** `MaxLabel`, `CleanLabelsLocation` and `JumpToLocation`
   rewrite every operand whose role is `LABEL`/`LABEL_DEF`; the inliner shifts every `SLOT` and `LABEL`, and its
   `default` case, which copies an unknown opcode unchanged, becomes an error. Adding a label-carrying opcode then
   needs no optimizer edit.
3. **Use the flags.** `IsEffectFreeRange`, `DeadStore`, LICM and the barrier lists in constant propagation, copy
   propagation and CSE are written from `MAY_TRAP`, `SIDE_EFFECT`, `CALL` and `TERMINATOR`.
4. **Zero the unused operands** in the constructors.
5. **An opcode-table hash in the file header.** A hash of the table's rows (names, roles, wire layout) is written
   next to the version, and the loader and linker refuse a file whose hash differs. An opcode change without a
   version bump is then detected instead of misread.
6. Promote the checker from advisory to required, and fix the `.obl` reader's header order (separately; it is a bug
   today).
