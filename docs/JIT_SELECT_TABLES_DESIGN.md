# JIT: `select` jump tables (F8)

**Status:** implemented on both backends, 2026-09-09 (batch 5): AMD64 in `44ce48f32b`, ARM64
in `fca3dffff8` (PR #738, first released in v2026.9.1). Item F8 of `JIT_CODEGEN_ASSESSMENT_2026_09.md`. Sections 1 and 2 are the
design as written before the change; the "neither JIT whitelists `JMP_TABLE`" of section 1 is
the problem it fixed, not today's state (both `CanJitInstruction` lists now accept
`JMP_TABLE` and `JMP_TABLE_SLOT`).

**Limitations that exist today.** The compiler emits the table only for a `select` whose labels
all fit in 32 bits (`IsIntegerSelect`) and are dense (`IsDenseInt`: `range <= 2 * labels`,
at most 65536 wide); a one-label `select` is a compare-and-jump and any other `select`
becomes a binary-search tree of compares (`SelectArrayTree`) -- ordinary `JMP`s, which the
JIT always compiled. The JIT refuses a table (and so the method) in three cases, each
recorded as a compile-failure reason: a table that runs past the method, a missing slot,
or a selector that is not an integer immediate, register or local. The table lives inline
after the indirect jump, so every case body must follow it in code order; the compiler's
output guarantees that.

## 1. The problem

A `select` whose labels are dense (`range <= 2 * labels`, at most 65536 wide) compiles to
a `JMP_TABLE` header followed by one `JMP_TABLE_SLOT` per value in `[base, base+range)`:

```
LOAD <value>
JMP_TABLE   base=3 range=4 default=22      ; operand, operand2, operand3
JMP_TABLE_SLOT target=10                   ; value 3
JMP_TABLE_SLOT target=14                   ; value 4
JMP_TABLE_SLOT target=22                   ; value 5 -> default
JMP_TABLE_SLOT target=18                   ; value 6
LBL ... case bodies, each ending in JMP end ...
LBL default body
LBL end
```

The interpreter pops the value, computes `index = value - base`, and jumps to
`slot[index]` when `0 <= index < range`, else to `default`. Every target is an
instruction index (the instruction after a label), the same currency `JMP` uses.

Neither JIT whitelists `JMP_TABLE`, so *any method containing a dense `select`* is
left to the interpreter whole -- parsers, tokenizers, state machines and dispatch
loops, which are exactly the code written with `select`. (A sparse `select`
compiles to a binary-search tree of compares and ordinary `JMP`s and always was compilable.)

## 2. Native shape

One bounds check and one indirect jump, with the table of 32-bit offsets placed
inline right after the jump so the base address is PC-relative and no data
section is needed. Entries are offsets from the table's own start; every target
lies after the table in code order (case bodies follow the slots), so they are
non-negative, but the load is sign-extending anyway.

AMD64:

```
  <value in idx>                   ; the popped working-stack value
  sub   idx, base                  ; skipped when base == 0
  cmp   idx, range
  jae   DEFAULT                    ; unsigned: negative indices fall out too
  lea   tbl, [rip + TABLE]
  movsxd tmp, dword [tbl + idx*4]
  add   tbl, tmp
  jmp   tbl
TABLE:
  dd  slot0 - TABLE, slot1 - TABLE, ...
```

ARM64 (phase 2):

```
  sub   x_idx, x_idx, #base        ; add for a negative base
  cmp   x_idx, #range              ; or a register when range does not encode
  b.hs  DEFAULT
  adr   x_tbl, TABLE
  ldrsw x_tmp, [x_tbl, x_idx, lsl #2]
  add   x_tbl, x_tbl, x_tmp
  br    x_tbl
TABLE:
  .word slot0 - TABLE, ...
```

`jae`/`b.hs` to the default is an ordinary conditional jump: it goes through the
existing `jump_table` fixup with a synthetic `JMP` instruction whose operand is the
default's index. The table entries are a second fixup list, `(entry offset, table
offset, target index)`, resolved in the same pass from each target instruction's
recorded offset. The working stack is empty after the header, as after any jump;
the local write-through cache is flushed before it, as before any jump.

## 3. What it must not disturb

- **Safepoints.** Loop headers are found by *backward* `JMP`s; table targets are all
  forward, so the poll placement is unchanged.
- **Loop locals in registers (F3, AMD64).** A pinned region's jumps out go through
  write-back stubs, which only `JMP` gets. Rule: a table is treated as a set of jump
  edges when regions are planned -- an edge from outside into the interior, or from
  inside to anywhere outside the region, makes the region ineligible. The common
  case (a `select` inside a loop whose cases are inside the loop) stays eligible; a
  `break` inside a case is a plain `JMP` and gets its stub as before.
- **JIT-level inlining** rejected control flow (`JMP`/`LBL`), and `JMP_TABLE` the same way,
  since its targets are indices into the callee. *Update 2026-11-18:* that AMD64 inliner was
  dead code and was removed in `632e0702f7` (2026-11-01), and ARM64 never had one; the
  only inliner left is the compiler's bytecode inliner (`core/compiler/optimization.cpp`),
  which relocates a pasted callee's `JMP_TABLE`/`JMP_TABLE_SLOT` operands.
- **A `select` on a constant** still goes through the table; the compile-time
  shortcut is not worth a second path.
- **The compare/jump fusion** (`skip_jump`) cannot be pending at a table -- the value
  is an integer expression, not a comparison -- and the code checks rather than
  assumes.

## 4. Verification

1. `vm_jit_equiv.obs` gains a `Selects` class: a dense table in a loop, a table with
   default slots, a negative base, a `select` over `Char`, one without `other`, a
   nested `select`, a `select` inside a pinned loop with a `break` in a case, and a
   value that is a method call. The fixture runs interpreted and compiled by
   `run_vm_flag_tests.py` and the outputs must stay byte-identical.
2. `OBJECK_JIT_REPORT=1` on the fixture: no method may still report
   `unsupported opcode 143`.
3. The regression suite normally and with `OBJECK_JIT_THRESHOLD=1`, then CI on all
   five legs (the ARM64 legs are the only ARM64 coverage).
4. Expected gain: a state-machine loop written with `select` moves from interpreter
   speed to native, i.e. the same 20-50x seen for any other method.
