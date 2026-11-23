# GC-root coverage: generated programs, run under every execution mode

The JIT finds its GC roots from hand-synchronised frame-layout constants
(`core/vm/arch/jit/jit_frame_layout.h`, `core/vm/arch/ref_map.h`) and types every spill site
by an abstract interpretation of the bytecode. A bug in either is a silent use-after-free
or a missed relocation of a moved young-generation object. Until this tool, the maps were
checked only by `OBJECK_GC_ROOTS=audit` over the existing regression suite, which is
weaker than a program written to hurt them (`docs/GC_ROOTS_DESIGN.md` section 9 promised
this test as `gc_root_coverage`; it was never written).

```
python tools/gc_roots/run_roots.py --bin <deploy>/bin --mode ci                      # 30 seeds
python tools/gc_roots/run_roots.py --bin <deploy>/bin --mode full --count 1000 --start 5000 -j 8
python tools/gc_roots/run_roots.py --bin <deploy>/bin --reduce 5003                  # minimize a divergent seed
python tools/gc_roots/gen_roots.py --seed 42 > prog.obs                              # one program
python tools/gc_roots/gen_roots.py --seed 42 --info                                  # its features, as JSON
python -m unittest discover -s tools/gc_roots -p "test_*.py"                          # generator unit tests
```

Python standard library only. `--bin` is a deploy tree's `bin` directory (`lib` beside it).

## What a generated program does

A seed picks 3 to 5 static scenarios, 1 or 2 instance scenarios (every reference is a
**field**: `self` is a root and every store goes through the write barrier) and, usually,
one thread scenario. Each scenario starts with 3 to 5 reference locals beside 2 to 4 `Int`
and 0 to 2 `Float` locals, then runs a random block of statements. `Main` calls each
scenario 14 times, which crosses the default JIT threshold (10) part way, so the same
method runs interpreted and then compiled.

| kept live | across |
|---|---|
| `String`, `Int[]`, `Box[]`, `String[]`, class instances (a virtual `Get`, a link field, `Chain` lists), `FuncRef<IntRef>` closures that capture an object, typed function references (`() ~ IntRef`, two words), `Int`, `Float` | loops (`for`, `each`, `while`) with `break`, `continue` and early `return`; `if`/`else`; `select`; nested to depth 2 |
| the same | pending expression temporaries: a value computed before a call that allocates (call arguments of every kind, receivers, array bases, string interpolation, binary operands, `?:`, `&`/`\|` joins, a freshly allocated argument) |
| the same | method calls, recursion (`Rec`), small callees that `-opt s3` inlines (`Tiny`, `Tiny2`, `Tiny3`, small generated `Pn` helpers) |
| the same | `?->` regions: a divide by zero and a `Runtime->Raise`, several frames deep, recovered in the caller |
| an address-valued `Int` (`Runtime->TestAddressOf`, needs `OBJECK_GC_TEST_HOOKS=1`, which the runner sets) | a collection: it must come back unchanged (#1452, #1473) |
| the spawning thread's locals | worker threads running other scenarios while another worker only allocates |

Every kept value is folded into a digest (`Fold(d, x) = (d * 131 + x) % 1000000007`) that
`Main` prints, so a reference the collector lost, or failed to relocate, changes the output.
Nothing printed depends on an address, a time or a thread schedule. The generator is a pure
function of its seed (`random.Random(seed)`, no other source).

## What the runner compares

Each program is compiled at `-opt s0` and `-opt s3`. The oracle is `ref`: `s0`, `--jit=off`,
the default 128 MB nursery, where these small programs never collect. Everything else runs
under collection pressure and must print exactly what `ref` printed:

| tag | flags / environment | what it exercises |
|---|---|---|
| `off64` | `--jit=off --nursery=64k`, `OBJECK_GC_ROOTS=audit` | interpreter stack maps |
| `off64v` | `--jit=off --nursery=64k --gc-threshold=128k`, `OBJECK_GC_VERIFY=checkmark` | interpreter with major collections and the heap verifier |
| `def64` | `--nursery=64k` | default JIT: a method is compiled after 10 calls, mid-run |
| `jit1` | `--jit=1 --nursery=64k`, `OBJECK_GC_ROOTS=audit` | every method compiled; the typed spill slots |
| `jit1v` | `--jit=1 --nursery=64k --gc-threshold=256k`, `OBJECK_GC_VERIFY=checkmark` | major collections and the heap verifier |
| `jit1big` | `--jit=1` | the JIT without collection pressure (separates a codegen bug from a root bug) |
| `thr1` | `OBJECK_JIT_THRESHOLD=1 --nursery=128k` | the env spelling of compile-everything, a second nursery size |
| `mix3`, `mix7` | `OBJECK_JIT_THRESHOLD=3` / `7`, 64k / 96k nursery, `OBJECK_GC_ROOTS=audit` | a callee below the threshold runs in the interpreter above its compiled caller (the bridge): interpreted and compiled frames on one stack, which the audit checks |

`--mode ci` runs `s0/jit1`, `s0/mix3`, `s3/off64`, `s3/def64`, `s3/jit1`, `s3/jit1v` (6 runs, 2 compiles
and the reference per seed); `--mode full` runs every tag at both optimization levels. A run
fails when its output or exit status differs from `ref`, when it crashes or times out (120 s),
or when its audit summary reports a map disagreement (`failed`, `mismatched_threads`,
`ref_points_at_nothing`, `int_points_at_object`, `jit_value_slot_at_object`). The runner also
reports how many of the generated methods the `--jit=1` runs actually compiled (about 95%; a
method with a `?->` region is never compiled, `TRY_START`, and a few overflow the register pool):
a differential run whose code never reaches the JIT compares the interpreter with itself.

### Address-valued `Int`s are opt-in (`--addr on|auto|off`, default `off`)

`Runtime->TestAddressOf(o)` (test hook, `OBJECK_GC_TEST_HOOKS=1`) gives an `Int` equal to an
object's address; the program holds one pending across a collection and compares it with a copy.
The precise maps type it `Int` and leave it alone, and `gc_address_int_pending.obs` holds that
property in the regression suite. A generated program, though, always has some interpreted method
under compiled frames (a method with a `?->` region, one the JIT refuses, a callee still below the
threshold), and an interpreted frame beneath compiled frames is still scanned on a guess
(`docs/GC_ROOTS_DESIGN.md` section 12), so a pending address there is rewritten. That limit is
documented and cannot be produced by Objeck code outside the test hook, so for these programs the
runner skips `def64`, `thr1`, `mix3` and `mix7`, ignores the audit's `int_points_at_object` and
`jit_value_slot_at_object`, and drops an output mismatch of a `--jit=1` configuration when its
`--jit=1` run left a generated method uncompiled. What remains is a program whose every method
compiled, which is held to exact output.

## When it finds something

A failing seed leaves `gc_roots_out/seed_N/` with `prog.obs`, both `.obe` files and
`report.txt` (the configurations that failed and the first differing line).
`--reduce N` runs a greedy delta-debugging pass over the program's statements against the
first failing configuration (`--config s3/jit1` to choose) and writes `seed_N_reduced.obs`.
Re-run the reduced program with the flags from the table to confirm, and compare with
`OBJECK_GC_ROOTS=conservative` (the old guess) and `OBJECK_JIT_PIN_MAX=0` (no loop pinning)
to localize.

A configuration the generator cannot compile is reported as a setup problem (exit 2): that is
a generator bug, not a VM finding.

## Measured (2026-11-23, linux-x64 build of master `565464fa80`)

- 30 seeds in `--mode ci` take 23 s with `-j 4` (32-thread machine; the CI runners are slower).
- 40 seeds under `--jit=1`: 36,493 spill sites in generated methods, 13,784 typed value (`val`),
  2,865 typed reference (`ref`), 14,305 and/or (`andor`), 0 `unknown`, 0 `mismatch`
  (`OBJECK_JIT_REPORT=1`). The generated shapes never reach the map's fallback paths.
- A fault-injection control: a throwaway `obr` that skips the relocation of the AMD64 `TMP_REF_*`
  slots in `FixupRoots` (one line, `core/vm/arch/memory.cpp`) is caught on 15 of the first 30 `--mode ci`
  seeds (wrong digests, `gc-verify` B3 violations, SIGSEGV), none on the unmodified VM. A run
  that cannot fail would prove nothing.
- Clean runs on the unmodified VM: see the pull request that added this tool for the seed ranges.

## Where it runs

- CI: `.github/workflows/ci-build.yml`, step "GC root coverage (advisory)", 30 seeds, next to
  the VM flag tests, `continue-on-error`. It is advisory until it has run clean across enough CI
  runs to rule out flakiness.
- Locally, for long runs: `--mode full --count 1000 -j 8`. Use fresh `--start` ranges; the
  seeds are stable, so a divergence is reproduced by `--start N --count 1`.

## Not covered

Windows and macOS behave as their CI legs show; this was developed against Linux AMD64 and
run on Linux ARM64 under qemu (an unoptimized `-O1` build without the HTTP/2 and HTTP/3 libraries,
100 seeds in `--mode full`). Only `Float` locals and function-reference params of
the shapes above exist; no generics, no `Byte`/`Char`/`Bool` locals as roots, no
native-library callbacks (the bridge is covered by `gc_roots_jit_bridge.obs`). The oracle is
agreement with an uncollected run, so a bug that gives the same wrong answer everywhere is
not found.
