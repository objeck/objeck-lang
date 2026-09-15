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
| `known.json` | suppressions: signature regexes of triaged findings |
| `faults/` | a deliberately broken `obc` and `obr`, used to prove findings are caught |
| `findings/` | reduced reproducers of real findings (`.obs` only) |
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

`s3/jit1`'s stderr lists every method the JIT handed back to the interpreter.
The driver counts those against the generated methods and fails the run when
fewer than `--min-jit` (default 90%) compiled: a differential fuzzer whose code
never reaches the JIT only compares the interpreter with itself.

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

`known.json` suppresses triaged signatures:

```json
{"known": [{"signature": "^diverge: s0/off,s3/default,s3/jit1,s0/jit1 \\| s3/off ", "note": "why, and the repro"}]}
```

The driver prints `[known]` or `[NEW]` per finding, saves up to three examples
per signature under `--out/<hash>/seed_<n>/` (program, `choices.json`,
`outcome.json`), and exits 1 on any new signature.

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

## Proving it catches bugs

`test_toolchain.py` runs the real toolchain and checks that:

- `faults/fault_obc.py` (s3 compiles get a wrong constant) is caught as `s0/off,s0/jit1 | s3/off,s3/default,s3/jit1`;
- `faults/fault_obr.py` (`--jit=1` prints a wrong digit) is caught as `s0/off,s3/off,s3/default | s3/jit1,s0/jit1`;
- a known.json entry suppresses the caught fault, and the unmodified toolchain is clean on the same seeds.
