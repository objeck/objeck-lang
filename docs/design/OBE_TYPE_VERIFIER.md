# Completing the `.obe` verifier: what is still trusted, and how to stop trusting it

**Status: design, not built.** This page says which changes to a compiled
program the loader still accepts and the VM cannot survive, why each one
cannot be refused with what an `.obe` holds today, and what would close it.
What the loader does check is in [SECURITY_MODEL.md](../SECURITY_MODEL.md); the
rule at the top of that page -- an `.obe` is native code, do not run one you
do not trust -- stands until the work below is done.

## Where things are (2026-11)

`Loader::Verify` walks every method once with the reference-map builder
(`core/vm/arch/ref_map.h`) and relates what **made** each operand-stack value
to what it is **used as**. Inside one method that is now enough to refuse a
number read through as a reference, a float used as an integer, an object
used as an array, class memory in place of an instance, a return at the wrong
stack height, and so on (the full table is in SECURITY_MODEL.md).

Measured with `tools/fuzz/adhoc/obe_campaign.py` (changed programs made from
285 compiled regression programs, each run with the default JIT, `--jit=off`
and `OBJECK_JIT_THRESHOLD=1`): before these rules 582 of 30,000 crashed the
VM, with them 45; of 100,000 with the final rules, 145. Of the byte-level changes (bit
flips, overwritten integers, cut and repeated chunks, truncation: three in
ten) 2 of 29,980 crashed in that run, against 11 of 8,600 before. Every remaining crash is a *structured* change -- one opcode, operand,
literal or instruction that still parses -- of one of the kinds below.

The builder knows three things about a value: what instruction made it, what
instructions use it, and the declaration of the local, field, parameter or
return it passes through. What it does not know is the list that follows.

## 1. A reference's class

**The gap.** A declaration says `OBJ_PARM` and nothing else: not which class.
So these load and then read or write the wrong memory:

- a field read from an object of another class. `LOAD_INT_VAR 5` changed to
  `LOAD_INT_VAR 7` loads a different object local; the field access that
  follows reads past a smaller object, or reads an `Int` field as a
  reference. The loader bounds an instance variable by the *largest* object
  any class makes, and by the method's own class only when the receiver is
  the method's own instance or an object `NEW_OBJ_INST` just made.
- a method called on an object of another class (the receiver is a local, a
  field, an array element, a parameter or a call's result).
- two arguments of object types passed in each other's place.
- an element loaded from an array and read through: the element type of an
  object array is not recorded at all.

**Why not now.** Parameter and return types are spelled in a method's name
(`Class:Method:o.System.String,i,`), and the loader now uses that: a number
passed or returned where the name spells a class of the program is refused.
Locals and fields have no such spelling outside a `-debug` build.

A first step needs no format change and was left out only for want of
measurement time: carry the class a name spells (a parameter's, a call's
result's), the class of `NEW_OBJ_INST` and of `OBJ_INST_CAST`, through locals
the way stored numbers are carried now (`Builder::CheckFacts`), and require a
field index to fit that class and a call's receiver or class-typed argument to
be related to the class the callee names. It would refuse most of the "two
loads exchanged" changes inside one method. It cannot type a field or an array
element, so it does not replace the declarations below.

**What closes it.** A class id beside each `OBJ_PARM` and `OBJ_ARY_PARM`
declaration (locals, instance and class variables, closure captures), written
by the compiler and read by the loader; -1 for a generic's type parameter and
for `System.Base`. With it the builder carries a class with every reference
(from declarations, `NEW_OBJ_INST`, `OBJ_INST_CAST`, and the callee's name),
and checks, where the class is known:

1. a field index against that class's instance size, and the field's kind
   from that class's declarations;
2. a call's receiver against the callee's class (same class, a descendant, or
   an implementer when the callee is an interface's declaration);
3. an argument and a returned value against the declared class;
4. a store to a field or local against its declared class.

Two things the compiler does today have to be settled first, because a
checker that refuses them refuses valid programs:

- **Enums are `OBJ_PARM`.** An enum-typed local, field or parameter holds an
  `Int` and is declared as an object, so "a number stored where an object is
  declared" cannot be a rule. Give enums their own declaration type
  (`ENUM_PARM`, scanned by the collector as an `Int`), or declare them
  `INT_PARM`. Until then a number can be stored to any object-typed *field*
  or passed where a name does not spell a class, and be read through by
  another method.
- **A temporary of an inlined `a ? b : c` over objects is declared `Int`.**
  `QuerySourceArchive:RowOf` (programs/regression/query_source_archive.obs,
  the inlined `TextOf`) stores a `String` to a local declared `INT_PARM` and
  calls a method on it. It is harmless as compiled (the value is consumed at
  once, with no collection between), but the collector does not see that
  local as a root. The inliner should declare the temporary with the
  expression's type. The loader therefore does not treat an `Int`-declared
  local as a number; it goes by what the method stores to it.

**Cost.** One integer per object declaration in the image; one comparison per
field access and call *at load*, none at run time. The format changes, so the
toolchain version does too (every `.obl` is rebuilt, as at any version bump).

**Run-time half.** Static classes are what the compiler believed. The object
on the stack is what `OBJ_INST_CAST` (checked at run time today) and the
allocation say. With rule 3 and 4 above every store is checked against a
declaration, so a declared class is an invariant and no new run-time check is
needed: the JVM's argument, and the reason the rules must cover stores.

## 2. What a trap takes and returns

**The gap.** `TRAP` and `TRAP_RTRN` call into the VM with an id pushed as a
literal just before; the handler pops what *it* wants. The loader checks that
the id is a known trap, and nothing about its arguments. So:

- an id changed to another known id (`CPY_CHAR_STR_ARY` to
  `LOAD_MULTI_ARY_SIZE`, one apart) hands a handler a stack laid out for a
  different one;
- an argument count changed (`TRAP 2` to `TRAP 1`), or `TRAP` exchanged with
  `TRAP_RTRN`, makes the builder's stack differ from the real one by a word;
- a trap's object arguments are whatever the program pushed.

It also keeps the stack rules approximate everywhere a trap appears: the
builder takes the instruction's count as the words popped and assumes
`TRAP_RTRN` pushes one. The return-height rule is therefore applied only to
methods whose traps are the literal copies the loader checks whole
(`Loader::CheckLiteralCopy`); library wrappers, which are mostly traps, are
exempt.

**What closes it.** One table, `core/shared/trap_signatures.h`, with a row per
trap id: the words it pops, the kind of each (`Int`, `Float`, object of class
*C*, array of element kind *E*, any), and what it pushes. The compiler's
`EmitSystemDirective` is generated from, or checked against, the same table
(`tools/cicd/check_trap_handlers.py` already keeps the id list whole and is
the place to check the rows against the handlers' `PopInt`/`PopFloat` calls).
The loader then requires the instruction's count to be the row's, types the
arguments like a call's, and pushes the row's result. 270 rows; the
work is the census, and the traps that pop a count taken from an argument
(`CPY_CHAR_STR_ARYS` pops one word per element of the array it is given) need
a row kind of their own, checked as `CheckLiteralCopy` does it today.

## 3. Function values

**The gap.** A function value is two words: a packed (class id, method id)
and a closure. `DYN_MTHD_CALL n, type` pops `n` arguments and expects `type`
back, and calls whatever method the packed word names. Nothing relates the
two: a literal changed from one lambda's id to another's calls a method with a
different parameter count or return, and the caller's stack is off from
there; an id that names no method is reported by the interpreter but the
closure of one lambda handed to another is read by the wrong capture layout,
by the callee and by the collector (which finds a closure's layout through
the id it reads from memory: `ClosureDeclarationsOf`).

**What closes it.** A function-typed declaration records its signature (the
`m.(i,)~i` spelling a parameter's name already carries; locals, fields and
captures do not). The builder then gives the packed word a type: a literal
that names a method of that signature, or a load from a slot of that
signature. `NEW_FUNC_INST` and the instructions around it that build a
closure are checked as one shape against the lambda's capture declarations,
the way a literal array is checked against its constant. `DYN_MTHD_CALL`'s
operands are compared with the signature at load.

A run-time comparison in `ProcessDynamicMethodCall` (callee parameter words
against the instruction's count) would catch the stack imbalance alone, at one
comparison per interpreted call through a function value; it was left out
because it adds to that path and does not make the call type-safe.

## 4. Values nothing types

A value loaded from a field of an object that is not the method's own
instance, from an element of an array, or returned by a trap has no maker the
builder knows: it may be read through unchecked. Sections 1 and 2 give each a
type; this is listed so the claim "nothing unknown is read through" is not
made before they are done. When they are, the builder's lenient mode can
refuse a read through an untyped value outright.

## 5. The JIT

The compilers (`arch/jit/amd64`, `arch/jit/arm64`) were written for what `obc`
emits. Three assumptions are now loader rules (a method begins by storing its
parameters; jumps land past labels; float and integer words are not mixed),
because code that broke them crashed the *compiler* at a method's first call.
A changed program the loader accepts can still take a path through
`ProcessInstructions` nobody has compiled before. The campaign's
`OBJECK_JIT_THRESHOLD=1` mode is the test for that; the remaining crashes
there are the kinds above, reached through compiled code. No JIT change is
proposed here: every case found so far was better refused at load.

## Order of work

1. Enum and inlined-temporary declarations (compiler; no format change beyond
   a new declaration type). Unblocks rule 4 of section 1 for locals.
2. Class ids on declarations (format change, next version bump) and the
   builder's class tracking. This is the bulk of the remaining crashes.
3. The trap table, generated into the compiler and the loader.
4. Function-value signatures.
5. Turn the lenient mode off: an instruction the builder does not model, or a
   read through an untyped value, refuses the program.

After each step: the campaign, and the valid-program corpus (every program in
the tree at each `-opt` level, a quarter with `-debug`, and one program per
library that names every documented class) must load with no refusal. A
refusal there is either a rule that is wrong or a compiler bug; the rules
added in 2026-11 found three of the second kind (duplicated arguments after
`??`, undiscarded statement values, a stray swap), which is the other reason
to finish this.
