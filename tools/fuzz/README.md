# Objeck differential fuzzer

Generates random, deterministic Objeck programs, runs each one through the
compiler and VM under several configurations, and reports any configuration
whose output or exit status differs from the reference. Python standard
library only; plan item "compiler lane step 7" in
`docs/PLAN_2026_10_0_HARDENING.md`.

```
python tools/fuzz/run_fuzz.py --bin core/release/deploy-x64/bin --count 300 --seed 1 -j 6
python tools/fuzz/reduce.py   --bin core/release/deploy-x64/bin tools/fuzz/out/<hash>/seed_<n> --name <name>
python tools/fuzz/gen.py --seed 42 --features F2,F5      # print one program
python -m unittest discover -s tools/fuzz -p "test_*.py"  # FUZZ_BIN=<bin> for the toolchain tests
```

## Files

| file | role |
| --- | --- |
| `gen.py` | typed-AST program generator driven by a recorded choice stream |
| `fuzzlib.py` | toolchain runner, output partitions, signatures, JIT report parser, known.json |
| `run_fuzz.py` | driver: generate, compile, run every configuration, triage, summarize |
| `reduce.py` | shrinks a finding's choice sequence while the finding still reproduces |
| `emi.py` | EMI mode: mutates existing regression tests into variants that must print the same output |
| `known.json` | suppressions: signature regexes of triaged findings |
| `faults/` | a deliberately broken `obc` and `obr`, used to prove findings are caught |
| `findings/` | reduced reproducers of real findings (`.obs` only); all are fixed and kept as guards, each also covered by a regression test |
| `test_*.py` | unit tests; `test_toolchain.py` drives a real deploy tree |

## Configurations

Every program is compiled at `s0` and `s3`, then run as:

| name | compile | `obr` flags |
| --- | --- | --- |
| `s0/off` | s0 | `--jit=off` (reference) |
| `s3/off` | s3 | `--jit=off` |
| `s3/default` | s3 | none (JIT after 10 calls) |
| `s3/jit1` | s3 | `--jit=1`, with `OBJECK_JIT_REPORT=1` |
| `s0/jit1` | s0 | `--jit=1` |

Runs that agree on stdout and on zero/non-zero exit form one output class. A
compiler crash, a compile error, a missing `.obe`, a VM crash (Windows NTSTATUS
or POSIX signal) or a timeout is a finding too.

`s3/jit1` runs with `OBJECK_JIT_REPORT=1`. A generated method counts as
compiled only on positive evidence: the report names it `compiled`, `compiled
on entry` or `inlined` (AMD64), and never rejects it. The driver fails the run
when fewer than `--min-jit` (default 70%) compiled: a differential fuzzer whose
code never reaches the JIT only compares the interpreter with itself.

The metric used to subtract rejections from the total, so a VM that ignored
`--jit=1` (no report lines at all) scored 100%; `faults/fault_obr_nojit.py` and
`faults/fake_obr.py` (`FAKE_OBR_MODE=ignore-jit`) now prove the gate fails it.
The floor is 70% because positive evidence measures about 76% on the x64 VM:
a method reached only from compiled code never counts a call toward the
auto-JIT threshold and stays interpreted (the old metric called that ~97%).
The summary's "most often not compiled" line names them.

A fraction over a handful of methods says little, so the floor applies only
when at least `--min-jit-sample` methods (default 20) were sampled. `--replay
SEED` runs that one seed alone and never applies the floor, so replaying a
clean program whose one method the JIT rejects (4/5 compiled, say) exits 0.
New findings still fail either run, and so does a run where nothing at all
compiled, whatever the sample size.

```
python tools/fuzz/run_fuzz.py --bin core/release/deploy-x64/bin --replay 20260936
```

## Programs

Each program samples a subset of feature layers (swarm testing; F1 always on):

- **F1** ints, locals, `if`/`else if`, `for`/`while`/`do-while`, `break`/`continue`, 1-D and 2-D Int arrays, `Size()`
- **F2** floats (digested as `(x * 1000.0)->As(Int)`, never printed)
- **F3** static functions calling earlier ones, recursion, 5-10 argument calls, virtual dispatch over a small class hierarchy
- **F4** `select` (dense, sparse, negative labels) and strings (concat, interpolation, `SubString`, `Get`, `Size`, `ToInt`)
- **F5** static function references and zero-argument closures (`FuncRef->New(\() ~ IntRef : () => ...)<IntRef>` and bare `\() => ...`)
- **F6** allocation-heavy object graphs: linked lists (built, walked, reversed), trees, object arrays

Every top-level function `Fi` is called 12 times from `Main` and its results
are folded into one printed digest line, `Fi=<Int>`.

Until integer semantics are settled, programs avoid undefined or unspecified
behaviour by construction: each expression carries a magnitude bound and is
wrapped (`% 1048573`, `and 1048575`) before it could pass 2^60; shift counts
stay in 0-63; divisors are non-zero; indexes are masked into power-of-two
arrays; object reads are Nil-checked; loops have literal bounds. They also stay
within JIT limits: at most ~60 local slots and 8 operands per flat expression.

Lambdas with parameters are never generated: they crash `obc` in 9.4 (C1).
A zero-argument lambda returning a basic type (`\() ~ Int : () => e`) is
generated only with `--basic-lambdas`, because its call crashes the 9.4 JIT
with a bare access violation that no known.json regex could tell apart from
a new JIT crash.

## Signatures and known.json

A signature names the kind of failure, with numbers normalized so the same bug
dedups across seeds:

```
diverge: s0/off,s3/default,s3/jit1,s0/jit1 | s3/off first=F#=# vs F#=#
diverge: s0/off,s0/jit1 | s3/off,s3/default,s3/jit1 first=F#=# vs  exit 1 >>> Attempting to dereference a 'Nil' memory element <<< @FuzzProgram->F0(a:Int, b:Int)
crash: s3/default access violation; diverge: s0/off,s3/off,s0/jit1 | s3/default | s3/jit1 first=F#=# vs
compile: s3 access violation
```

The partition lists configurations in output classes, the reference's class
first; `first=` is the first differing stdout line of the second class; a
failing run adds its exit, the VM's `>>> ... <<<` text and the top frame of its
unwind trace.

`known.json` suppresses triaged signatures. It has one schema, defined and
validated in `known_schema.py` and shared with the nightly triage
(`tools/cicd/nightly_triage.py`), so an entry means the same thing to both:

```json
{"schema": 1, "known": [{"signature": "^diverge: s0/off,s3/default,s3/jit1,s0/jit1 \\| s3/off ", "note": "why, and the repro"}]}
```

- `signature` (regex, needs a `note`) matches a fuzzer signature. It may be
  narrowed by `leg` and `step` only; an entry with `leg` matches in `run_fuzz`
  only when `--leg` names a matching leg (the nightly passes it).
- `leg`, `step`, `config`, `test`, `message` (regexes) and `id` (exact) match
  nightly triage failures; the fuzzer ignores entries without `signature`.
- `issue` and `note` are annotations. Unknown keys, a bare list or the old
  `{"signatures": [...]}` spelling are errors in both tools (`run_fuzz` exits 2,
  the triage reports a new failure).

The driver prints `[known]` or `[NEW]` per finding, saves up to three examples
per signature under `--out/<hash>/seed_<n>/` (program, `choices.json`,
`outcome.json`), and exits 1 on any new signature. A new finding is also
printed as `FAIL fuzz: <signature> (seed N)`, the line the nightly triage
parses and recovers the signature from; a nightly issue for a fuzz finding
carries a ready `signature` entry.

## Reducing

`reduce.py` shrinks the recorded choice sequence, not the text: truncate,
delete chunks, zero chunks, lower values. The generator turns any sequence into
a valid program (a draw of 0 is always the simplest option), so there are no
syntax errors to filter. A candidate is kept when it compiles at s0 and s3, the
reference exits 0 with no VM error, the reference runs within 2x the original's
time (+0.25 s), and the configurations partition exactly as the original's did.
It refuses a finding that no longer reproduces.

Replay a finding with the generator it was recorded with: the choice sequence
means a different program once the draw order changes (`test_gen.py` pins it
with golden hashes), and with the `--basic-lambdas` setting it was recorded
with (stored in `choices.json`; findings from before the knob existed need
`--basic-lambdas`).

## EMI mode

`emi.py` fuzzes with real programs instead of generated ones (equivalence
modulo inputs). It takes regression tests that compile and exit 0 -- no
`EXPECT_*`, `NONDETERMINISTIC_OUTPUT`/`DIFF_*`/JIT opt-out markers, network,
thread or timing tests, and whose s0/off output repeats -- and writes variants
that must print exactly what the original prints:

```
python tools/fuzz/emi.py --bin core/release/deploy-x64/bin --variants 10 --seed 1 -j 8
python tools/fuzz/emi.py --bin <bin> --tests core_arithmetic,opt_* --variants 4
python tools/fuzz/emi.py --bin <bin> --list        # eligible tests
```

- **dead**: `if(EmiGuardZq->Dead()) { ... };` at a statement boundary. The
  guard is a static set in `Main` from `args->Size()`, so s3 cannot fold it;
  the block writes live Int locals, clones the previous statement, loops, and
  first calls `EmiGuardZq->Trip(<mark>)`, which prints and exits 97.
- **delete**: a probe build puts `EmiGuardZq->Probe(K);` (stderr, once) at the
  top of every if/else/while/for/each/do/label block and runs at s0/off;
  variants empty blocks whose probe never printed.
- **identity**: Int literals and Int locals become `(x + EmiGuardZq->Zero())`,
  `(7 * EmiGuardZq->One())`, `xor`/`or` with zero; Float literals
  `* EmiGuardZq->FOne()`; if/while conditions `(c) & EmiGuardZq->Live()`.

Each variant is compiled at s0 and s3 and run under `--jit=off` and `--jit=1`;
every configuration is compared with the original's s0/off run (stdout and
zero/non-zero exit; configurations the original itself fails are dropped). A
variant whose s0 compile fails is repaired by dropping the mutations on the
error lines. All configurations agreeing on a different output is a
*semantic* change (the mutation's fault, not a finding); anything else is a
divergence, confirmed by two re-runs, reduced to a minimal mutation set
(ddmin) and saved under `--out/diverge/<test>/v<n>/` (`variant.obs`,
`reduced.obs`, `outcome.json`). Signatures read `diverge: ref,s0/off,... |
s3/off,...` with `ref` the original. Variants are a pure function of
(seed, test, index, probe hits). `test_emi.py` checks determinism, that
variants compile and agree, that `faults/fault_obc_emi.py` (s3 takes the dead
guard) is caught, and that the s3 bytecode keeps every dead block.

## Proving it catches bugs

`test_toolchain.py` runs the real toolchain and checks that:

- `faults/fault_obc.py` (s3 compiles get a wrong constant) is caught as `s0/off,s0/jit1 | s3/off,s3/default,s3/jit1`;
- `faults/fault_obr.py` (`--jit=1` prints a wrong digit) is caught as `s0/off,s3/off,s3/default | s3/jit1,s0/jit1`;
- a known.json entry suppresses the caught fault, and the unmodified toolchain is clean on the same seeds;
- `faults/fault_obr_nojit.py` (`--jit=1` becomes `--jit=off`) fails the JIT-coverage gate.

`test_run_fuzz.py` needs no build: `faults/fake_obc.py` and `faults/fake_obr.py`
stand in for the toolchain. It checks the coverage gate against a VM that
ignores `--jit=1`, and the round trip with the nightly triage: a finding
recorded under `nightly_triage.py run` is `new` without a known.json entry,
`known` with one, and `run_fuzz` honours the same file. `test_known_schema.py`
covers the schema itself.
