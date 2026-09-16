#!/usr/bin/env python3
"""Keep the regression markers that remove coverage honest.

programs/regression/run_differential.py runs every test under several compiler
and VM configurations and requires identical stdout and exit status. Three
markers let a test out of part of that comparison, and one changes scheduling:

    # DIFF_CONFIGS: s3             compare only the listed configurations
    # DIFF_REQUIRES_JIT            skip the --jit=off configurations
    # NONDETERMINISTIC_OUTPUT      compare exit status only, never stdout
    # DIFF_SERIAL                  never run beside another test

and two let a test out of a nightly hardening regression run:

    # VERIFY_SKIP                  not run under the heap verifier (OBJECK_GC_VERIFY set)
    # GC_STRESS_SKIP               not run with a forced tiny heap threshold
                                   (OBJECK_VM_ARGS names --gc-threshold=)

The coverage-reducing ones need a reason. The JIT opt-out marker showed where
that goes unchecked (tools/cicd/check_jit_optouts.py): twenty of twenty-two
opt-outs never said why, and every one passed without it. So, for every
programs/regression/*.obs, this asserts:

1. A directive is a whole line at column 0 with no trailing whitespace.
   DIFF_CONFIGS lists only -opt levels (s0, s3) or config names (s0/off,
   s3/off, s3/default, s3/jit1, s0/jit1).
2. The line right after DIFF_CONFIGS, DIFF_REQUIRES_JIT, NONDETERMINISTIC_OUTPUT,
   VERIFY_SKIP or GC_STRESS_SKIP starts with "# reason:" and says something.
3. No other line contains a marker's text (say "the nondeterministic-output
   marker" in prose), and no test carries both DIFF_REQUIRES_JIT and the JIT
   opt-out marker (the two leave nothing to run).
4. NONDETERMINISTIC_OUTPUT is on at most 5% of the tests. Printing run-dependent
   numbers to stderr ("..."->ErrorLine()) keeps a test comparable; the marker
   is for output that differs by design.
5. A VERIFY_SKIP or GC_STRESS_SKIP reason cites the run that showed the
   failure: "run <id>" with the workflow run's number. These two markers remove
   a test from a step that exists to find heap defects, so the bar is an
   observed failure in a named run, not a plausible mechanism. The first
   attempt at this file failed that bar -- it required VERIFY_SKIP on every
   test that opens a socket, on the theory that loopback peers time out under
   the verifier, while in the run it cited all thirteen such tests had passed
   the verifier step on all five legs in 0-3 s. Only the step that also forces
   --gc-threshold=64k broke two of them.
6. At most VERIFY_OPTOUT_CAP tests carry either verifier opt-out. A run that
   needs more of them is reporting something about the VM, not about the tests.

Run from anywhere:  python3 tools/cicd/check_diff_markers.py [--dir DIR]
Exit 0 when the tree conforms, 1 with one line per problem otherwise.
"""
import argparse
import os
import re
import sys

NEEDS_REASON = ("# DIFF_CONFIGS:", "# DIFF_REQUIRES_JIT", "# NONDETERMINISTIC_OUTPUT",
                "# VERIFY_SKIP", "# GC_STRESS_SKIP")
BARE = ("# DIFF_REQUIRES_JIT", "# NONDETERMINISTIC_OUTPUT", "# DIFF_SERIAL", "# VERIFY_SKIP",
        "# GC_STRESS_SKIP")
ALL = ("# DIFF_CONFIGS", "# DIFF_REQUIRES_JIT", "# NONDETERMINISTIC_OUTPUT", "# DIFF_SERIAL",
       "# VERIFY_SKIP", "# GC_STRESS_SKIP")
REASON = "# reason:"
CONFIG_TOKENS = {"s0", "s3", "s0/off", "s3/off", "s3/default", "s3/jit1", "s0/jit1"}
NONDET_CAP_PERCENT = 5

# The markers that take a test out of a nightly hardening step, and the cap on
# how many tests may carry them.
VERIFY_OPTOUTS = ("# VERIFY_SKIP", "# GC_STRESS_SKIP")
VERIFY_OPTOUT_CAP = 5

# The evidence a verifier opt-out's reason must name: the workflow run that
# recorded the failure, e.g. "run 35027973357".
RUN_REF_RE = re.compile(r"\brun\s+\d{6,}\b")


def repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(here, "..", ".."))


def check_file(path):
    """Return (problems, is_nondeterministic, opts_out_of_a_verifier_step)."""
    problems = []
    optout = False
    with open(path, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    if text.startswith("﻿"):
        text = text[1:]
    lines = [l.rstrip("\r") for l in text.split("\n")]
    nondet = False
    requires_jit = False
    for i, line in enumerate(lines):
        directive = None
        if line in BARE:
            directive = line
        elif line.startswith("# DIFF_CONFIGS:") and line == line.rstrip():
            directive = "# DIFF_CONFIGS:"
            tokens = [t for t in re.split(r"[,\s]+", line[len(directive):]) if t]
            bad = [t for t in tokens if t not in CONFIG_TOKENS]
            if not tokens or bad:
                problems.append((i + 1, "DIFF_CONFIGS names no valid configuration: %s"
                                 % (", ".join(bad) or "(empty)")))
        if directive:
            if directive == "# NONDETERMINISTIC_OUTPUT":
                nondet = True
            if directive == "# DIFF_REQUIRES_JIT":
                requires_jit = True
            if directive in VERIFY_OPTOUTS:
                optout = True
            if directive in NEEDS_REASON:
                nxt = lines[i + 1] if i + 1 < len(lines) else ""
                if not nxt.startswith(REASON) or not nxt[len(REASON):].strip():
                    problems.append((i + 1, "%s without a '# reason: ...' line right after it"
                                     % directive.rstrip(":")))
                elif directive in VERIFY_OPTOUTS and not RUN_REF_RE.search(nxt):
                    problems.append((i + 1, "%s whose reason names no run ('run <id>'): a test leaves "
                                     "a verifier step on an observed failure, not on a theory"
                                     % directive.rstrip(":")))
            continue
        hit = next((m for m in ALL if m in line), None)
        if hit:
            if line.rstrip() in BARE or line.startswith("# DIFF_CONFIGS:"):
                problems.append((i + 1, "directive with trailing whitespace: runners match whole lines"))
            else:
                problems.append((i + 1, "mentions the literal text '%s'; describe the marker in words"
                                 % hit))
    if requires_jit and "# JIT_DISABLE" in lines:
        problems.append((0, "DIFF_REQUIRES_JIT with the JIT opt-out marker leaves nothing to compare"))
    return problems, nondet, optout


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=os.path.join(repo_root(), "programs", "regression"))
    args = ap.parse_args(argv)
    files = sorted(f for f in os.listdir(args.dir) if f.endswith(".obs"))
    failures = []
    nondet = []
    optouts = []
    for name in files:
        problems, is_nondet, is_optout = check_file(os.path.join(args.dir, name))
        if is_nondet:
            nondet.append(name)
        if is_optout:
            optouts.append(name)
        for lineno, what in problems:
            failures.append("  programs/regression/%s:%d: %s" % (name, lineno, what))
    cap = len(files) * NONDET_CAP_PERCENT // 100
    if len(nondet) > cap:
        failures.append("  %d test(s) carry NONDETERMINISTIC_OUTPUT, over the cap of %d (%d%% of %d): %s"
                        % (len(nondet), cap, NONDET_CAP_PERCENT, len(files), ", ".join(nondet)))
    if len(optouts) > VERIFY_OPTOUT_CAP:
        failures.append("  %d test(s) opt out of a verifier step, over the cap of %d: %s"
                        % (len(optouts), VERIFY_OPTOUT_CAP, ", ".join(optouts)))
    if failures:
        print("Differential marker problems:")
        print("\n".join(failures))
        print("See tools/cicd/check_diff_markers.py and programs/regression/run_differential.py.")
        return 1
    print("%d regression test(s): %d nondeterministic-output marker(s) (cap %d), "
          "%d verifier opt-out(s) (cap %d); every coverage-reducing marker has a reason, "
          "and every verifier opt-out names the run that showed the failure."
          % (len(files), len(nondet), cap, len(optouts), VERIFY_OPTOUT_CAP))
    return 0


if __name__ == "__main__":
    sys.exit(main())
