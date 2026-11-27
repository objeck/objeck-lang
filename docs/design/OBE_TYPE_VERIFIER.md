# Completing the `.obe` verifier: what is still trusted, and how to stop trusting it

**Status (2026-11): sections 2, 3 and 5 are built as far as an image allows.
Section 1 was built for the classes an image spells, measured, and switched
off: it refused no valid program and none of the changed programs that crash.
What would make it worth having is a format change that is designed here and
not made.** This page says which
changes to a compiled program the loader still accepts and the VM cannot
survive, why each one cannot be refused with what an `.obe` holds today, and
what would close it. What the loader does check is in
[SECURITY_MODEL.md](../SECURITY_MODEL.md); the rule at the top of that page --
an `.obe` is native code, do not run one you do not trust -- stands until the
work below is done.

## Where things are

`Loader::Verify` walks every method once with the reference-map builder
(`core/vm/arch/ref_map.h`) and relates what **made** each operand-stack value
to what it is **used as**: a number read through as a reference, a float used
as an integer, an object used as an array, a return at the wrong stack height,
and so on (the full table is in SECURITY_MODEL.md). Since 2026-11 it also knows
what every trap takes and returns and the method a function value names.

Measured with `tools/fuzz/adhoc/obe_campaign.py` (changed programs made from
278 compiled regression programs, each run with the default JIT,
`--jit=off` and `OBJECK_JIT_THRESHOLD=1`; the same 30,000 changes for every
row):

| VM | crashed | distinct faulting sites |
|---|---|---|
| before the stack and operand rules (v2026.11.3) | 582 | 75 |
| with them (the start of this work) | 45 | 16 |
| + the trap table, a frame's size, the JIT's literal array | 26 | 11 |
| + a reference's class, where spelled | 26 | 10 |
| + function values | 13 | 7 |
| + the kind of array | 13 | 6 |
| + a class's family bound, undeclared locals, new arrays (every rule on) | 11 | 5 |
| **the VM as shipped**: the trap table, function values, the small cases; class and array-kind rules off | 11 | 5 |

(The first row is from the earlier campaign, on other seeds. One of the seeds,
compiled by the earlier compiler, holds the valueless return after a `try`
that the later rows refuse, so its changes are all refused there.) The class
rule and the array kinds bought nothing measurable here, and a rule that buys
nothing is only a chance to refuse a valid program: both are in the builder
behind `CLASS_RULES` and `ARRAY_KIND_RULES` (`core/vm/arch/ref_map.h`), off. No valid program
is refused by any row: every `.obs` in the repository at `-opt s0` to `s3`, a
quarter again with `-debug`, and one program per library that names every
documented class, 5,067 images, load under `OBJECK_VERIFY_ONLY=1`
(`tools/fuzz/adhoc/obe_corpus_gate.py`). That gate decided the shape of every
rule below: three stricter forms were written first and dropped because valid
programs broke them, and each is recorded where it applies.

## 1. A reference's class

**Built, measured, switched off (`CLASS_RULES`).** A reference is given the
static class of the expression that made it, as far as an image says one: `NEW_OBJ_INST`, `OBJ_INST_CAST`, the method's
own instance, a parameter or a call's result whose name spells a *qualified*
class (`o.System.String`), a trap's result, and a load of a parameter's local.
It is refused when it is then used as an **unrelated** class -- neither
inherits from the other and neither is an interface -- as the receiver of a
method that is not virtual and reads its instance, as an argument or a returned
value where a class is spelled, stored to a parameter's local, or handed to a
trap that reads an object. An instance variable read through it must lie
within the largest object of that class and its descendants (not, as before,
of any class).

**Why only that.** The first version carried a class through every local and
required each use to be *an instance of* the class needed. It refused 262 of
5,067 valid images, for three reasons that are properties of the compiler, not
of the rule:

- **A cast is checked and then not always written.** The compiler checks that
  `As(T)` is between related classes. In a call chain it does not always emit
  `OBJ_INST_CAST`: `worker->Call(Nil, 5000)->GetValue()->As(IntRef)->Get()`
  (programs/deploy/actors_35.obs) calls `IntRef:Get` on `GetValue`'s
  `System.Base` with nothing between. So a valid program may use a reference as
  any class *related* to its static type, and "is an instance of" cannot be a
  rule. It is also a hole in valid programs: that call on a value that is not
  an `IntRef` reads the wrong object instead of failing the cast.
- **A temporary's slot holds values of different classes.** The compiler gives
  every *variable* a slot of its own, and the inliner and string interpolation
  reuse one temporary slot for unrelated values (`Classes:Main`,
  programs/deploy/classes_4.obs: a Shape, then a String). What was stored to a
  slot anywhere in the method says nothing about one load of it.
- **A variable's static type is its declaration's.** `b : Base := Dog->New()`
  followed by `b->As(Cat)` behind a type test is valid, and with the cast not
  written the value "made as a Dog" is used as a Cat. A claim may be no more
  precise than the static type of the expression at the use, and for a load of
  a local that is the variable's declared type, which an image does not record
  (a parameter's is in the method's name, which is why that one local counts).
  For the same reason two different claims that meet at a join are no claim:
  `c ? cat : dog` has their common type.

Two more things a name cannot say: `o.T` may be a generic's parameter or a
class of the program named `T`, and nothing in an image tells which, so only a
qualified name is taken for a class; and a function (the language's static
method) is called with whatever instance its caller has, which the inliner
also parks in a local, so only a callee that *reads* its instance (a field
through it, a store, an argument, a return, a trap that reads it, or a call to
a method that does) holds its callers to its class.

**What is still open.** Everything the builder cannot give a static class:

- a field read from an object that is not the method's own instance or one
  `NEW_OBJ_INST` just made: the loader bounds the field's index by the
  *largest* object of any class, and types the value it loads by nothing;
- a reference loaded from a local that is not a parameter, from a field, a
  capture or an array element, and then used as a receiver or an argument;
- a class of the program's own (unqualified) bundle anywhere a name spells it.

These are most of the crashes that remain (the list at the end).

**What closes it.** The static class of each declaration, written by the
compiler: a class id beside each `OBJ_PARM` and `OBJ_ARY_PARM` (locals,
instance and class variables, closure captures), -1 for a generic's parameter,
an interface and `System.Base`. With it every load has a static class, and the
rules above (unrelated classes only) apply to all of them. For a field read
through a reference of static class *S* the bound becomes the largest object
among *S* and its descendants rather than of the program, and the field's kind
the one declared at that index wherever *S*'s family agrees.

It is a format change (the declaration record in `.obe` and `.obl`, the
linker, the loader, `tools/fuzz/loader/obe_image.py`, every committed `.obl`),
so it belongs with a version bump, and three compiler changes come first,
because a checker that refuses what the compiler writes refuses valid
programs:

1. **Write every cast.** `OBJ_INST_CAST` wherever `As(T)` narrows, including
   mid-chain. Then a static class is an upper bound the program cannot leave
   without a run-time check, "is an instance of" replaces "is related to", and
   a field index can be held to the static class itself. It costs one check per
   cast that is skipped today; measure it on the benchmarks before deciding.
2. **Enums are not objects.** An enum-typed local, field or parameter holds an
   `Int` and is declared `OBJ_PARM` (and an enum's array is declared
   `INT_ARY_PARM` and allocated with `NEW_OBJ_ARY`). Give enums their own
   declaration type, scanned by the collector as an `Int`.
3. **A temporary is declared as what it holds.** The inliner's temporary for
   `a ? b : c` is declared `Int` whatever the branches are (the loader takes an
   `Int` local for untyped because of it, and lets a Float be stored to one),
   and one slot serves temporaries of different classes. Either declare a
   temporary per type or mark temporaries as untyped.

Without 1, the declared classes still close the "two loads exchanged" changes
between unrelated classes, which is most of them.

## 2. What a trap takes and returns

**Built.** `core/shared/trap_signatures.h`, one row per trap id: the
instruction (`TRAP` or `TRAP_RTRN`) and count the compiler writes, whether the
handler pushes a result, the kind of each word (Int, Float, which arrays, an
object and of which classes) and of the result, and for the 27 traps whose
handler reads its caller's frame or instance rather than the operand stack, the
library methods it may stand in and whether it is the instance it reads. The
builder requires the row's instruction, count and method, types the words and
the result, and so knows every instruction's effect on the stack: the
return-height rule now applies to every method.

The table is generated (`tools/cicd/gen_trap_signatures.py`), from the two
places that already agreed on it: `IntermediateEmitter::EmitSystemDirective`
(the instruction and count, `trap_shapes.py`) and the compiled libraries, where
a directive is a statement of a library method and the method's own signature
types its words. `--check` compares the header with the compiler without a
build and runs in CI; `--observed` compares it with a run. That last check is
how the two rows the libraries do not imply were found: the regression suite on
a VM that printed each trap's effect on the stack agreed with the table for 190
of the 192 traps that ran (`FILE_TEMP_NAME` pushes a result its wrapper's shape
hides, and `LOAD_CLS_BY_INST`, written `TRAP_RTRN`, pushes none). The 77 traps
the suite does not run are as the libraries imply and not observed.

One compiler defect came out of it: a value-returning method that ends in a
`try` whose handlers do not match re-raises and then reached the method's
implicit return with nothing on the stack. The compiler now returns the type's
zero there.

**What is still open.** A handler that trusts a length or an index inside a
well-typed argument; that is a bug in the handler, found by fuzzing the traps,
not a loader rule. Four rows are typed by count only (no library method holds
the directive in the form the generator reads).

## 3. Function values

**Built.** A function value is two words, an id (the tag, a class and a method
packed in a literal) and a closure. The builder follows an id from the literal
that made it, on the stack and through the method's function-typed locals (a
function variable has one declared type, so every store to it fits every call
through it), and from the function types an image spells (a parameter's, a
call's result's). A literal used as an id names a method that exists; a number
or a reference is not used as one; where the use says what is called
(`DYN_MTHD_CALL`'s count and return, an argument or a return whose type is
spelled) every method and spelled type the id can be has those parameter words
and that return; a lambda that declares captures is paired with a closure made
large enough for them, and a new closure takes no store past its size or its
lambda's captures, each as the kind declared.

No comparison was added to the call path: `ProcessDynamicMethodCall` is
unchanged.

**What is still open.** A function value loaded from a field, a capture or an
array element has no type in an image, so a call through it is unchecked, and
so is the pairing of an id and a closure that were not made together in the
method that uses them. The declarations of section 1 would carry the function
type (`m.(i,)~i`, as a parameter's name already does) beside each `FUNC_PARM`.

## 4. Values nothing types

A value loaded from a field of an object that is not the method's own instance,
from an element of an array, or returned by one of the four untyped traps has
no maker the builder knows: it may be read through unchecked. Section 1's
declarations give each a type; this is listed so the claim "nothing unknown is
read through" is not made before they exist. When they do, the builder's
lenient mode can refuse a read through an untyped value outright.

## 4a. The kind of an array

**Built, measured, switched off (`ARRAY_KIND_RULES`).** Per reference, the
arrays it was made as (an allocation, a declared local, field, capture or
parameter, a spelled result, a trap's result) against the arrays every use
allows (an element instruction, a copy, a clear, a text conversion, a declared
store, argument or return, a trap's argument), in four kinds: Byte and Bool,
Char, Float, and word. Int and object arrays cannot be told apart: an enum's
array is declared `INT_ARY_PARM` and allocated with `NEW_OBJ_ARY`. No valid
image is refused, and on the campaign it changed one outcome. What stays on
from it: a new array is an array, so one passed where an object is declared is
refused.

## 5. The JIT

The compilers (`arch/jit/amd64`, `arch/jit/arm64`) were written for what `obc`
emits. Assumptions that are now loader rules: a method begins by storing its
parameters; jumps land past labels; float and integer words are not mixed. One was fixed in the compilers instead,
because a valid (if wrong) program can reach it: `STOR_INT_ARY_ELM` with a
literal where the array should be read the literal's register before
`ArrayIndex` refused it. A changed program the loader accepts can still take a
path through `ProcessInstructions` nobody has compiled before; the campaign's
`OBJECK_JIT_THRESHOLD=1` mode is the test for that.

## What still crashes

Of the 30,000 changes, 11 still crash the VM as shipped:

| changed programs | what was changed | where the VM faults | why the loader cannot refuse it |
|---|---|---|---|
| 6 | a load repeated or dropped (`repeat ClosureBodyCallResult:#{L3}#:[5] LOAD_INT_VAR`, `repeat Tagged:ViewedSize:[3]`, `repeat MethodGenericsLibrary:Length:o.System.String,[2]`, `repeat ClosureDirectCall:#{L0}#:[1]`, `repeat GcClosureCaptureNurseryEnd:#{L0}#:[1]`, `drop System.Introspection.Method:GetParameters:[1]`) | `LoadClsInstIntVar`, compiled code | the field that follows is read through what the extra load left: a captured value, a field of the instance, a String. Each is an object, of a class nothing declares, read at an index bounded only by the largest object of any class |
| 1 | `operand Tok:New:o.Tok#Kind,o.System.String,[5] LOAD_INT_VAR 1 -> 0` | compiled code | the load now takes the enum parameter, an `Int` declared `OBJ_PARM`, where the String was |
| 1 | `operand CollectOrderedHashTest:#{L0}#:...,[2] LOAD_INT_VAR 0 -> 7` (interpreter only) | `Execute` | another object local of the lambda, of a class nothing declares |
| 2 | `swap SelectStringExpressions:New:o.System.String,[3] STOR_INT_VAR <-> LOAD_INST_MEM`, `operand ...,[0] STOR_INT_VAR 0 -> 1` | `HashStringId` | the string-hash trap is handed a local the parameter was never stored to: a reference of no declared class (the trap's row says String; `CLASS_RULES` cannot see a local's class) |
| 1 | `opcode Holder:Set:o.T,[2] OBJ_INST_CAST -> OBJ_TYPE_OF` | `ValidObjectCast` | the type test's 0 or 1 is stored to an object field, as an enum's value validly is, and another method reads through it |

Every one is section 1: a reference whose class no image records, or a number
in a slot declared an object because enums are. None is a trap's, a function
value's or a stack's.

## Order of work

1. The three compiler changes of section 1 (casts, enums, temporaries), each
   measured on the benchmarks and the corpus before the next.
2. Declared classes and function types (format change, with a version bump) and
   the builder's rules over them.
3. Turn the lenient mode off: an instruction the builder does not model, or a
   read through an untyped value, refuses the program.

After each step: the campaign, and the valid-program corpus must load with no
refusal. A refusal there is either a rule that is wrong or a compiler bug; the
rules added in 2026-11 found four of the second kind (duplicated arguments
after `??`, undiscarded statement values, a stray swap, a return with no value
after a `try`), which is the other reason to finish this.
