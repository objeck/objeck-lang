#!/usr/bin/env python3
"""Run the nightly hardening steps and triage what they found.

The nightly workflow (.github/workflows/nightly-hardening.yml) runs five kinds
of long check on every CI platform: the differential regression, the fuzzer,
the regression suite under the heap verifier (minor and major stress), and
loops of the GC stress tests. A red nightly is only useful if it says, without
anyone opening five job logs, whether the failure is the runner's fault, a
defect already on file, or something new. This script is that classifier, and
the wrappers that produce the per-step result files it reads.

Subcommands
-----------
run      Run one step's command, tee its output to a log, and write a result
         file. `--require PATH` and `--require-text PATH:TEXT` make a missing
         tool (or a binary/runner that lacks a knob) a FAILURE, never a skip.
             nightly_triage.py run --leg L --step S --out DIR [--config C]
                 [--cwd D] [--timeout SECS] [--parser generic|regression]
                 [--default-seed SEED] [--require P]... [--require-text P:T]...
                 -- command args...
loop     Compile each named regression test once and run it N times in each
         JIT mode (the stress loops). Writes the result file for step "stress".
             nightly_triage.py loop --leg L --out DIR --bin BIN_DIR
                 --tests a,b --runs N [--modes off,1] [--src-dir D]
                 [--timeout SECS] [--budget SECS] [--opt s3]
         The result file is rewritten after every run; --budget stops the loop
         (recorded as a failure) before the step's own timeout can kill it.

Both run and loop write an "unfinished" result before starting, so a step
killed by its timeout, a job timeout or a cancel is classified NEW. Only a step
that never started (no result file at all) is infra.
triage   Read every result file, classify each failure as infra, known or new,
         and write the summary, the tracking-issue body (every new signature,
         grouped by leg and step) and an issue body for each new failure that
         deserves its own issue (see "Issues" below).
             nightly_triage.py triage --results DIR --known tools/fuzz/known.json
                 --legs a,b --steps s1,s2 --out DIR [--run-url URL]
                 [--github-output FILE] [--max-issues 5]

Result file (one per leg and step): <out>/<leg>/<step>.json
    {"schema": 1, "leg", "step", "config", "status": "pass"|"fail"|"infra",
     "exit_code", "duration_s", "log", "failures": [
        {"test", "config", "message", "detail", "seed", "reduced", "count"}]}

Known signatures (tools/fuzz/known.json): {"schema": 1, "known": [...]}, the
one schema documented in tools/fuzz/known_schema.py and shared with the
fuzzer. Each entry may give regexes (re.search) for "leg", "step", "config",
"test" and "message", a fuzzer "signature" regex (matched against the
signature in run_fuzz's "FAIL fuzz: <signature> (seed N)" line), or an exact
triage "id"; an absent field matches anything. A missing, unreadable or
off-schema file is reported as a NEW failure, so the nightly cannot go
quietly green without its triage data.

Classes
-------
infra  the runner, network or artifact store failed, or a step left no result
       file (it never started: tree download or setup failed, runner lost).
       Comment only.
known  matches a known.json signature. Summary only.
new    everything else. Listed in the run's one tracking issue.

Issues
------
A run reports through ONE tracking issue (a comment per run) that lists every
new signature, grouped by leg and step. A new failure gets an issue of its own
(seed, reduced program, output, artifacts, a known.json snippet) only when it
is worth one on its own:
  * the same failure is new on more than one leg (a cross-leg signature: the
    signature without the leg, and without the runners' "(exit N)" /
    "(possible hang ...)" suffixes that differ between the .sh and .cmd), or
  * it is a crash (a fatal signal, an access violation) or a heap-verifier
    violation, on any number of legs.
One issue per cross-leg signature, titled by it, so a failure on three legs is
one issue naming the three. The first nightly opened five issues for what was
two test-output bugs and one slow step; --max-issues still caps the count.

Standard library only.
"""
import argparse
import hashlib
import json
import os
import re
import signal
import subprocess
import sys
import threading
import time

# known.json's schema is shared with the fuzzer (tools/fuzz/known_schema.py)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "fuzz"))
import known_schema  # noqa: E402

SCHEMA = 1
DETAIL_LINES = 40
EXCERPT_LINES = 60
BODY_LIMIT = 60000
BASE_LIBS = "cipher,collect,xml,json"

# Text that means the machine failed, not the code under test.
INFRA_PATTERNS = [
    r"No space left on device",
    r"The runner has received a shutdown signal",
    r"lost communication with the server",
    r"Unable to (?:download|find) artifact",
    r"Artifact not found",
    r"API rate limit exceeded",
    r"Could not resolve host",
    r"Temporary failure in name resolution",
    r"Connection reset by peer.*(?:github|apt|brew)",
    r"E: Failed to fetch",
    r"Error: Download failed",
    r"\bno result file\b",
]
INFRA_RE = re.compile("|".join(INFRA_PATTERNS), re.IGNORECASE)

JIT_MODE_FLAGS = {"off": ["--jit=off"], "1": ["--jit=1"], "default": []}

# Written before a step's command starts and replaced when it returns. Must not
# match INFRA_RE: a hang that outlives the step timeout is a real finding.
UNFINISHED_MESSAGE = ("started but never finished: killed before writing its "
                      "final result (hang past the step timeout, job timeout "
                      "or cancellation)")


# ---------------------------------------------------------------------------
# Result files
# ---------------------------------------------------------------------------

def make_failure(test, message, config="", detail="", seed="", reduced="", count=1):
    return {"test": test or "", "config": config or "", "message": message or "",
            "detail": detail or "", "seed": seed or "", "reduced": reduced or "",
            "count": count}


def write_result(out_dir, leg, step, config, status, exit_code, duration,
                 log_name, failures, command=None, note=""):
    leg_dir = os.path.join(out_dir, leg)
    os.makedirs(leg_dir, exist_ok=True)
    result = {"schema": SCHEMA, "leg": leg, "step": step, "config": config,
              "status": status, "exit_code": exit_code,
              "duration_s": round(duration, 1), "log": log_name,
              "command": command or [], "note": note, "failures": failures}
    path = os.path.join(leg_dir, step + ".json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    return path


def tail(text, n):
    lines = text.splitlines()
    return "\n".join(lines[-n:])


# ---------------------------------------------------------------------------
# Log parsers
# ---------------------------------------------------------------------------

RUNNING_RE = re.compile(r"^Running:\s+(.+?)\.\.\.\s*$")
REG_FAIL_RE = re.compile(r"^\s*(?:\[FAIL\]|FAIL\b)\s*(.*)$")
OUTPUT_OPEN_RE = re.compile(r"^\s*--- output ---\s*$")
# Exactly the runners' closing line: a test that prints its own dashed rule
# must not end the output block early.
OUTPUT_CLOSE_RE = re.compile(r"^\s*-{14}\s*$")
SUMMARY_RE = re.compile(r"^\s*(?:={8,}|Failed tests:)\s*$")
SEED_RE = re.compile(r"\bseed\b\s*[=:]?\s*([0-9A-Za-z_\-]+)", re.IGNORECASE)
REDUCED_RE = re.compile(r"\breduced(?:\s+program)?\s*[=:]\s*(\S+)", re.IGNORECASE)
CONFIG_RE = re.compile(r"\bs[0-3]/(?:off|default|jit1|jit=\w+)\b")
GENERIC_FAIL_RE = re.compile(
    r"^\s*\[?(FAIL|FAILED|MISMATCH|DIFF|CRASH|TIMEOUT)\]?(?::|\s)\s*(.*)$")
TEST_TOKEN_RE = re.compile(r"^([A-Za-z_][\w.\-]*?):?(?:\s|$)")


def parse_regression_log(text, config=""):
    """Failures from run_regression.sh / run_regression.cmd output.

    Both runners print "Running: NAME..." before each test. The POSIX one marks
    a failure "  [FAIL] reason", the Windows one "  FAIL (reason)", and both then
    echo the test's output between "--- output ---" and a dashed line. A FAIL
    printed by the test itself inside that block is detail, not a new failure.
    The runners record one failure per test, so a second FAIL before the next
    "Running:" (test output whose own dashed line closed the block early) is
    also detail.
    """
    failures = []
    current = ""
    current_failed = False
    in_output = False
    for line in text.splitlines():
        if in_output:
            if OUTPUT_CLOSE_RE.match(line):
                in_output = False
            elif failures:
                d = failures[-1]
                if d["detail"].count("\n") < DETAIL_LINES:
                    d["detail"] += line.strip() + "\n"
            continue
        m = RUNNING_RE.match(line)
        if m:
            current = m.group(1)
            current_failed = False
            continue
        if OUTPUT_OPEN_RE.match(line):
            in_output = True
            continue
        if SUMMARY_RE.match(line):
            current, current_failed = "", False  # the end-of-run report
            continue
        if current_failed:
            d = failures[-1]
            if line.strip() and d["detail"].count("\n") < DETAIL_LINES:
                d["detail"] += line.strip() + "\n"
            continue
        m = REG_FAIL_RE.match(line)
        if m and current:
            reason = m.group(1).strip()
            if reason.startswith("(") and reason.endswith(")"):
                reason = reason[1:-1]
            failures.append(make_failure(current, reason or "failed", config))
            current_failed = True
    return failures


def parse_generic_log(text, config="", default_seed=""):
    """Failures from a tool whose format is not fixed (differential, fuzzer).

    A failure is a line starting with FAIL, FAILED, MISMATCH, DIFF, CRASH or
    TIMEOUT (optionally bracketed). The first token after it is taken as the
    test name, an sN/mode token as the configuration. "seed: X" and
    "reduced: PATH" lines attach to the failure before them.
    """
    failures = []
    pending_seed = ""
    pending_reduced = ""
    for line in text.splitlines():
        m = GENERIC_FAIL_RE.match(line)
        if m:
            rest = m.group(2).strip()
            if re.match(r"^\d+\s*$", rest) or rest == "":
                continue  # a count ("FAILED: 0"), not a failure
            t = TEST_TOKEN_RE.match(rest)
            test = t.group(1) if t else ""
            c = CONFIG_RE.search(rest)
            f = make_failure(test, rest, c.group(0) if c else config,
                             seed=default_seed)
            s = SEED_RE.search(rest)
            if s:
                f["seed"] = s.group(1)
            elif pending_seed:
                f["seed"] = pending_seed
            r = REDUCED_RE.search(rest)
            if r:
                f["reduced"] = r.group(1)
            elif pending_reduced:
                f["reduced"] = pending_reduced
            pending_seed = pending_reduced = ""
            failures.append(f)
            continue
        s = SEED_RE.search(line) if "seed" in line.lower() else None
        r = REDUCED_RE.search(line)
        if failures:
            if s and not SEED_RE.search(failures[-1]["message"]):
                failures[-1]["seed"] = s.group(1)
            if r:
                failures[-1]["reduced"] = r.group(1)
            if failures[-1]["detail"].count("\n") < DETAIL_LINES:
                failures[-1]["detail"] += line.rstrip() + "\n"
        else:
            if s:
                pending_seed = s.group(1)
            if r:
                pending_reduced = r.group(1)
    return failures


# ---------------------------------------------------------------------------
# run
# ---------------------------------------------------------------------------

def check_requirements(requires, require_texts):
    """Return a list of failure messages for missing tools or knobs."""
    problems = []
    for path in requires:
        if not os.path.exists(path):
            problems.append("required tool missing: " + path)
    for spec in require_texts:
        if ":" not in spec:
            problems.append("bad --require-text (want PATH:TEXT): " + spec)
            continue
        path, text = spec.rsplit(":", 1)
        if not os.path.exists(path):
            problems.append("required file missing: " + path)
            continue
        with open(path, "rb") as f:
            data = f.read()
        if text.encode("utf-8") not in data:
            problems.append("%s does not contain %s (feature not in this build)"
                            % (path, text))
    return problems


def build_command(command, cwd):
    """Pick an interpreter for scripts so the step YAML stays OS-neutral."""
    if not command:
        return command
    first = command[0]
    # A bare script name means the one in --cwd. cmd /c looks only on PATH when
    # NoDefaultCurrentDirectoryInExePath is set, so name it by full path.
    local = os.path.join(cwd or ".", first)
    if os.path.dirname(first) == "" and os.path.isfile(local):
        first = os.path.abspath(local)
        command = [first] + list(command[1:])
    lower = first.lower()
    if lower.endswith((".cmd", ".bat")) and os.name == "nt":
        return ["cmd", "/d", "/c"] + command
    if lower.endswith(".sh"):
        return ["bash"] + command
    if lower.endswith(".py"):
        return [sys.executable] + command
    return command


def kill_tree(proc):
    try:
        if os.name == "nt":
            subprocess.run(["taskkill", "/T", "/F", "/PID", str(proc.pid)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            os.killpg(proc.pid, signal.SIGKILL)
    except (OSError, ProcessLookupError):
        pass
    try:
        proc.kill()
    except OSError:
        pass


def run_tee(command, cwd, log_path, timeout, echo=True):
    """Run command, copy output to log_path (and stdout). Returns (code, timed_out)."""
    kwargs = {}
    if os.name != "nt":
        kwargs["start_new_session"] = True
    with open(log_path, "wb") as log:
        proc = subprocess.Popen(command, cwd=cwd, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, **kwargs)

        def pump():
            for raw in iter(proc.stdout.readline, b""):
                log.write(raw)
                if echo:
                    out = getattr(sys.stdout, "buffer", None)
                    if out is not None:
                        out.write(raw)
                        out.flush()
                    else:
                        sys.stdout.write(raw.decode("utf-8", "replace"))

        reader = threading.Thread(target=pump, daemon=True)
        reader.start()
        timed_out = False
        try:
            code = proc.wait(timeout=timeout if timeout and timeout > 0 else None)
        except subprocess.TimeoutExpired:
            timed_out = True
            kill_tree(proc)
            code = proc.wait()
        reader.join(10)
        proc.stdout.close()
    return code, timed_out


def cmd_run(args, command):
    leg_dir = os.path.join(args.out, args.leg)
    os.makedirs(leg_dir, exist_ok=True)
    log_name = args.step + ".log"
    log_path = os.path.join(leg_dir, log_name)
    start = time.time()

    problems = check_requirements(args.require, args.require_text)
    if problems:
        with open(log_path, "w", encoding="utf-8") as f:
            f.write("\n".join(problems) + "\n")
        for p in problems:
            print("::error::%s: %s" % (args.step, p))
        failures = [make_failure("(tool)", p, args.config) for p in problems]
        write_result(args.out, args.leg, args.step, args.config, "fail", -1,
                     time.time() - start, log_name, failures, command)
        return 1

    if not command:
        print("run: no command given after --", file=sys.stderr)
        return 2
    full = build_command(command, args.cwd)
    # Replaced when the command returns. If the step is killed first (its
    # timeout-minutes, a job timeout, a cancel), this marker is what triage
    # reads: a step that started and never finished is a finding, not infra.
    write_result(args.out, args.leg, args.step, args.config, "fail", -1, 0.0,
                 log_name, [make_failure("(step)", UNFINISHED_MESSAGE, args.config,
                                         seed=args.default_seed)], full)
    try:
        code, timed_out = run_tee(full, args.cwd, log_path, args.timeout)
    except OSError as e:
        with open(log_path, "a", encoding="utf-8") as f:
            f.write("could not start %s: %s\n" % (full[0], e))
        failures = [make_failure("(tool)", "could not start %s: %s" % (full[0], e),
                                 args.config)]
        write_result(args.out, args.leg, args.step, args.config, "fail", -1,
                     time.time() - start, log_name, failures, full)
        return 1
    duration = time.time() - start

    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    if args.parser == "regression":
        failures = parse_regression_log(text, args.config)
    else:
        failures = parse_generic_log(text, args.config, args.default_seed)

    if timed_out:
        failures.append(make_failure("(step)", "step timed out after %ds" % args.timeout,
                                     args.config, detail=tail(text, DETAIL_LINES),
                                     seed=args.default_seed))
    elif code != 0 and not failures:
        failures.append(make_failure("(step)", "exit code %d" % code, args.config,
                                     detail=tail(text, DETAIL_LINES),
                                     seed=args.default_seed))
    status = "pass" if (code == 0 and not timed_out and not failures) else "fail"
    # Exit 0 with FAIL lines still fails: the runners once scored on exit code
    # alone and counted printed failures as passes.
    write_result(args.out, args.leg, args.step, args.config, status, code,
                 duration, log_name, failures, full)
    return 0 if status == "pass" else 1


# ---------------------------------------------------------------------------
# loop
# ---------------------------------------------------------------------------

def extra_libs(src_path):
    try:
        with open(src_path, "r", encoding="utf-8-sig", errors="replace") as f:
            for line in f:
                m = re.search(r"# EXTRA_LIBS:\s*(\S+)", line)
                if m:
                    return m.group(1)
    except OSError:
        pass
    return ""


def run_loop(tests, runs, modes, compile_fn, run_fn, log=print,
             checkpoint=None, remaining=None):
    """Core of the stress loop, separated from process handling for testing.

    compile_fn(test) -> (ok, output, program)
    run_fn(program, flags) -> (exit_code or None on timeout, output)
    checkpoint(failures, done, total) is called after every compile and run, so
    a result file on disk is never more than one run stale.
    remaining() -> seconds left in the budget (None = unlimited). When it hits
    zero the loop stops and records "budget exhausted" as a failure.
    Returns aggregated failures: one per (test, mode, message) with a count.
    """
    agg = {}
    order = []
    total = len(tests) * len(modes) * runs
    done = [0]

    def snapshot():
        out = []
        for key in order:
            f = dict(agg[key])
            if f["config"] not in ("compile", "budget"):
                f["detail"] = "failed %d of %d runs; %s" % (f["count"], runs, f["detail"])
            out.append(f)
        return out

    def tick():
        if checkpoint:
            checkpoint(snapshot(), done[0], total)

    def out_of_budget(test, config):
        left = remaining() if remaining else None
        if left is None or left > 0:
            return False
        key = ("(loop)", "budget", "budget exhausted")
        agg[key] = make_failure(
            "(loop)", "budget exhausted", "budget",
            detail="stopped at %s %s after %d of %d runs; the slowest test is "
            "the likeliest hang" % (test, config, done[0], total))
        order.append(key)
        log("  [FAIL] budget exhausted after %d of %d runs" % (done[0], total))
        return True

    for test in tests:
        ok, output, program = compile_fn(test)
        if not ok:
            key = (test, "compile", "compilation failed")
            agg[key] = make_failure(test, "compilation failed", "compile",
                                    detail=tail(output, DETAIL_LINES))
            order.append(key)
            log("  [FAIL] %s: compilation failed" % test)
            done[0] += len(modes) * runs
            tick()
            continue
        for mode in modes:
            flags = JIT_MODE_FLAGS.get(mode)
            if flags is None:
                flags = ["--jit=" + mode]
            config = "jit=" + mode
            bad = 0
            for i in range(1, runs + 1):
                if out_of_budget(test, config):
                    tick()
                    return snapshot()
                code, output = run_fn(program, flags)
                done[0] += 1
                if code == 0:
                    tick()
                    continue
                bad += 1
                message = ("timed out" if code is None else "exit code %d" % code)
                key = (test, config, message)
                if key in agg:
                    agg[key]["count"] += 1
                else:
                    agg[key] = make_failure(
                        test, message, config,
                        detail="first seen on run %d of %d\n%s"
                        % (i, runs, tail(output, DETAIL_LINES)))
                    order.append(key)
                tick()
            log("  %s %s: %d/%d failed" % (test, config, bad, runs))
    return snapshot()


def compile_command(obc, src, libs, opt, program):
    return [obc, "-src", src, "-lib", libs, "-opt", opt, "-dest", program]


def run_command(obr, flags, program):
    return [obr] + flags + [program]


def tool_path(bin_dir, name):
    exe = os.path.join(bin_dir, name + ".exe")
    return exe if os.name == "nt" or os.path.exists(exe) else os.path.join(bin_dir, name)


def cmd_loop(args):
    leg_dir = os.path.join(args.out, args.leg)
    work = os.path.join(leg_dir, "stress-work")
    os.makedirs(work, exist_ok=True)
    log_name = "stress.log"
    log_path = os.path.join(leg_dir, log_name)
    start = time.time()
    bin_dir = os.path.abspath(args.bin)
    obc = tool_path(bin_dir, "obc")
    obr = tool_path(bin_dir, "obr")
    tests = [t for t in args.tests.split(",") if t]
    modes = [m for m in args.modes.split(",") if m]
    config = "loop x%d jit=%s" % (args.runs, "/".join(modes))

    problems = check_requirements(
        [obc, obr] + [os.path.join(args.src_dir, t + ".obs") for t in tests], [])
    if problems:
        with open(log_path, "w", encoding="utf-8") as f:
            f.write("\n".join(problems) + "\n")
        failures = [make_failure("(tool)", p, config) for p in problems]
        write_result(args.out, args.leg, "stress", config, "fail", -1, 0.0,
                     log_name, failures)
        return 1

    env = dict(os.environ)
    lib_dir = os.path.join(os.path.dirname(bin_dir), "lib")
    env.setdefault("OBJECK_LIB_PATH", lib_dir)
    native = os.path.join(lib_dir, "native")
    for var in ("PATH", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"):
        env[var] = native + os.pathsep + env.get(var, "")

    log_file = open(log_path, "w", encoding="utf-8")

    def remaining():
        if not args.budget or args.budget <= 0:
            return None
        return args.budget - (time.time() - start)

    def log(msg):
        print(msg)
        log_file.write(msg + "\n")
        log_file.flush()

    def compile_fn(test):
        src = os.path.abspath(os.path.join(args.src_dir, test + ".obs"))
        program = os.path.join(os.path.abspath(work), test + ".obe")
        libs = BASE_LIBS
        extra = extra_libs(src)
        if extra:
            libs += "," + extra
        cmd = compile_command(obc, src, libs, args.opt, program)
        log("compile: " + " ".join(cmd))
        limit = 600
        left = remaining()
        if left is not None:
            limit = max(1, min(limit, int(left) + 1))
        try:
            p = subprocess.run(cmd, cwd=bin_dir, env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=limit)
            out = p.stdout.decode("utf-8", "replace")
            return p.returncode == 0 and os.path.exists(program), out, program
        except (OSError, subprocess.TimeoutExpired) as e:
            return False, str(e), program

    def run_fn(program, flags):
        # One hung run may not outlive the budget either.
        limit = args.timeout
        left = remaining()
        if left is not None:
            limit = max(1, min(limit, int(left) + 1))
        try:
            p = subprocess.run(run_command(obr, flags, program), cwd=work, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               timeout=limit)
            return p.returncode, p.stdout.decode("utf-8", "replace")
        except subprocess.TimeoutExpired as e:
            out = (e.stdout or b"").decode("utf-8", "replace")
            return None, out
        except OSError as e:
            return -1, str(e)

    def checkpoint(failures, done, total):
        # Until the loop returns, the file says "unfinished" on top of what has
        # failed so far, so a kill leaves a finding rather than no file.
        marker = make_failure("(step)", UNFINISHED_MESSAGE, config,
                              detail="%d of %d runs done" % (done, total))
        write_result(args.out, args.leg, "stress", config, "fail", -1,
                     time.time() - start, log_name, failures + [marker])

    checkpoint([], 0, len(tests) * len(modes) * args.runs)
    try:
        failures = run_loop(tests, args.runs, modes, compile_fn, run_fn, log,
                            checkpoint, remaining)
    finally:
        log_file.close()
    status = "fail" if failures else "pass"
    write_result(args.out, args.leg, "stress", config, status,
                 1 if failures else 0, time.time() - start, log_name, failures)
    return 1 if failures else 0


# ---------------------------------------------------------------------------
# triage
# ---------------------------------------------------------------------------

def normalize(message):
    m = message.lower()
    m = re.sub(r"0x[0-9a-f]+", "0x#", m)
    m = re.sub(r"\d+", "#", m)
    return re.sub(r"\s+", " ", m).strip()


def signature(leg, step, config, test, message):
    # Fuzzer program names carry a per-seed index (prog_17); without folding
    # the digits the same bug found under tomorrow's seed would be "new".
    if step == "fuzz":
        test = normalize(test)
    key = "|".join([leg, step, config, test, normalize(message)])
    return hashlib.sha1(key.encode("utf-8")).hexdigest()[:10]


# The regression runners word the same outcome differently: run_regression.sh
# says "runtime error (exit 134)" and "timed out after 300s (possible hang /
# infinite loop)", run_regression.cmd "runtime error" and "timed out after 300s".
RUNNER_SUFFIX_RE = re.compile(r"\s*\((?:exit [^)]*|possible hang[^)]*)\)\s*$")


def cross_signature(step, config, test, message):
    """The signature without the leg: one failure on several legs shares it."""
    if step == "fuzz":
        test = normalize(test)
    key = "|".join([step, config, test, normalize(RUNNER_SUFFIX_RE.sub("", message))])
    return hashlib.sha1(key.encode("utf-8")).hexdigest()[:10]


# A process that died rather than failed: a fatal signal (the POSIX runners
# report 128+N, Python -N), a Windows exception code, or the shell's words.
CRASH_PATTERNS = [
    r"\(exit (?:132|134|135|136|139)\)",
    r"\bexit(?:ed| code) -(?:4|6|7|8|11)\b",
    r"\bexited (?:132|134|135|136|139)\b",
    r"\bexit code (?:3221225477|-1073741819|3221225725|-1073741571|3221226505|-1073740791)\b",
    r"\b0xC0000005\b", r"\b0xC00000FD\b", r"\b0xC0000409\b",
    r"\bSegmentation fault\b", r"\bBus error\b", r"\bIllegal instruction\b",
    r"\bcore dumped\b", r"\bdumped core\b", r"\bAborted\b(?! by the user)",
]
CRASH_RE = re.compile("|".join(CRASH_PATTERNS), re.IGNORECASE)
# memory_verify.cpp: ">>> gc-verify: B2 violation in collection 4 (minor, after): ..."
VERIFIER_RE = re.compile(r">>> gc-verify: .*violation")


def failure_kind(message, detail):
    """'verifier', 'crash' or '' (an ordinary failure).

    A violation is the verifier's own line, which reaches us inside the test's
    output, so that scan reads the detail too. A crash is read from the
    runner's verdict (the message) alone: the detail is whatever the test
    printed, and a test that printed "Segmentation fault" or "core dumped" --
    a fixture that greps for one, say -- would otherwise be escalated to its
    own issue. The POSIX runners put the signal in the message ("runtime error
    (exit 139)"), which is where a real crash shows up."""
    if VERIFIER_RE.search((message or "") + "\n" + (detail or "")):
        return "verifier"
    if CRASH_RE.search(message or ""):
        return "crash"
    return ""


def load_known(path):
    """Return (entries, error). A missing or malformed file is an error."""
    try:
        return known_schema.load(path), None
    except known_schema.KnownError as e:
        return [], str(e)


def match_known(entries, item):
    """The raw known.json entry `item` matches (known_schema.match), or None.
    A failure parsed from run_fuzz's FAIL line carries its fuzzer signature,
    so a "signature" entry means the same thing here as in the fuzzer."""
    if item.get("step") == known_schema.FUZZ_STEP and "signature" not in item:
        item = dict(item, signature=known_schema.fuzz_signature(item.get("test", ""),
                                                                item.get("message", "")))
    return known_schema.match(entries, item)


def collect_results(results_dir, legs, steps):
    """Yield (leg, step, result-or-None) for expected and discovered steps."""
    seen = set()
    found = []
    if os.path.isdir(results_dir):
        for root, _dirs, files in os.walk(results_dir):
            for name in sorted(files):
                if not name.endswith(".json"):
                    continue
                path = os.path.join(root, name)
                try:
                    with open(path, "r", encoding="utf-8") as f:
                        data = json.load(f)
                except (OSError, ValueError):
                    continue
                if not isinstance(data, dict) or data.get("schema") != SCHEMA:
                    continue
                key = (data.get("leg", ""), data.get("step", ""))
                if key in seen:
                    continue
                seen.add(key)
                data["_dir"] = os.path.relpath(root, results_dir)
                found.append((key[0], key[1], data))
    for leg in legs:
        for step in steps:
            if (leg, step) not in seen:
                found.append((leg, step, None))
    order = {s: i for i, s in enumerate(steps)}
    legorder = {l: i for i, l in enumerate(legs)}
    found.sort(key=lambda x: (legorder.get(x[0], 99), order.get(x[1], 99), x[0], x[1]))
    return found


def classify(results_dir, known_path, legs, steps):
    entries, known_error = load_known(known_path)
    items = []

    def add(leg, step, config, f, log, klass=None, known=None):
        item = {"leg": leg, "step": step, "config": f.get("config") or config,
                "test": f.get("test", ""), "message": f.get("message", ""),
                "detail": f.get("detail", ""), "seed": f.get("seed", ""),
                "reduced": f.get("reduced", ""), "count": f.get("count", 1),
                "log": log}
        item["id"] = signature(leg, step, item["config"], item["test"], item["message"])
        item["cross"] = cross_signature(step, item["config"], item["test"], item["message"])
        item["kind"] = failure_kind(item["message"], item["detail"])
        if klass is None:
            if INFRA_RE.search(item["message"]) or INFRA_RE.search(item["detail"]):
                klass = "infra"
            else:
                known = match_known(entries, item)
                klass = "known" if known else "new"
        item["class"] = klass
        if known:
            item["known"] = {k: known[k] for k in ("id", "issue", "note") if k in known}
        items.append(item)

    if known_error:
        add("nightly", "triage", "", make_failure("known.json", known_error), "", "new")

    counted = 0
    for leg, step, result in collect_results(results_dir, legs, steps):
        if result is None:
            add(leg, step, "", make_failure(
                "(step)", "no result file: the step did not run to completion "
                "(job cancelled, runner lost, tree download failed or an earlier "
                "setup step failed)"), "", "infra")
            continue
        counted += 1
        log = os.path.join(result.get("_dir", leg), result.get("log", "")).replace("\\", "/")
        status = result.get("status")
        failures = result.get("failures") or []
        if status == "pass" and not failures:
            continue
        if status == "infra":
            for f in failures or [make_failure("(step)", result.get("note") or "infra")]:
                add(leg, step, result.get("config", ""), f, log, "infra")
            continue
        if not failures:
            failures = [make_failure("(step)", "exit code %s" % result.get("exit_code"))]
        for f in failures:
            add(leg, step, result.get("config", ""), f, log)

    # One signature per id: several legs never collapse (leg is in the key),
    # but the same line printed twice does.
    unique = {}
    for item in items:
        if item["id"] in unique:
            unique[item["id"]]["count"] += item["count"]
        else:
            unique[item["id"]] = item
    items = list(unique.values())

    # Which legs each new failure is new on, and which own issue (if any)
    # covers it. Keyed by issue_key, the same key the issues group by, so the
    # summary's "Also new on" column never names fewer legs than the issue does
    # (the two disagreed while this was keyed by the message-sensitive
    # cross-signature: a test failing differently on two runners read as one
    # leg in the table and three in its issue).
    legs_of = {}
    for item in items:
        if item["class"] == "new":
            legs_of.setdefault(issue_key(item), set()).add(item["leg"])
    for item in items:
        if item["class"] == "new":
            item["legs"] = sorted(legs_of[issue_key(item)], key=lambda l: (
                legs.index(l) if l in legs else 99, l))
    for group in issue_groups(items):
        for item in group:
            item["issue_key"] = issue_key(item)

    if any(i["class"] == "new" for i in items):
        status = "new"
    elif any(i["class"] == "infra" for i in items):
        status = "infra"
    elif items:
        status = "known"
    else:
        status = "green"
    return {"status": status, "items": items, "results_read": counted}


def issue_title(item):
    """Keyed by issue_key (not the leg), so the same failure on another leg or
    a later night comments on the existing issue instead of opening another."""
    test = item["test"] or "(step)"
    return "Nightly hardening [%s] %s %s" % (issue_key(item), item["step"], test)


def issue_key(item):
    """What one issue covers: a regression test in a step (so the differential's
    two lines for one divergence, or one test's different messages on two
    runners, are one issue), or, for fuzzer findings and step-level failures,
    the cross-leg signature."""
    test = item["test"]
    if item["step"] == known_schema.FUZZ_STEP or not test or test.startswith("("):
        return item.get("cross") or cross_signature(item["step"], item["config"], test,
                                                    item["message"])
    return cross_signature(item["step"], "", test, "")


KIND_RANK = {"verifier": 0, "crash": 1, "": 2}


def group_legs(group):
    """The distinct legs of a group, in first-seen (report) order."""
    seen = []
    for g in group:
        if g["leg"] not in seen:
            seen.append(g["leg"])
    return seen


def issue_groups(items):
    """New failures that get their own issue: [[item, ...], ...], one list per
    issue_key, holding every new item under that key. A key qualifies when its
    items span more than one leg or any of them is a crash or verifier
    violation. Most severe first (verifier, crash, then most legs), so the
    --max-issues cap drops the least important."""
    groups = {}
    order = []
    for item in items:
        if item["class"] != "new":
            continue
        key = issue_key(item)
        if key not in groups:
            groups[key] = []
            order.append(key)
        groups[key].append(item)
    out = []
    for n, key in enumerate(order):
        g = groups[key]
        legs = {i["leg"] for i in g}
        kinds = [i["kind"] for i in g if i["kind"]]
        if len(legs) > 1 or kinds:
            rank = min(KIND_RANK[k] for k in kinds) if kinds else KIND_RANK[""]
            out.append((rank, -len(legs), n, g))
    out.sort(key=lambda x: x[:3])
    return [g for _r, _l, _n, g in out]


def message_regex(message):
    """A known.json message regex that survives changed counts and addresses."""
    parts = re.split(r"(0x[0-9a-fA-F]+|\d+)", message[:120])
    out = []
    for p in parts:
        if re.match(r"^0x[0-9a-fA-F]+$", p):
            out.append(r"0x[0-9a-fA-F]+")
        elif p.isdigit():
            out.append(r"\d+")
        else:
            out.append(re.escape(p))
    return "".join(out)


def fence(text):
    # Tildes, so backticks in program output cannot close the block early.
    lines = text.replace("~~~", "~ ~ ~").splitlines()
    if len(lines) > EXCERPT_LINES:
        lines = lines[:EXCERPT_LINES] + ["... (%d more lines in the log artifact)"
                                         % (len(lines) - EXCERPT_LINES)]
    return "~~~\n" + "\n".join(lines) + "\n~~~"


KIND_TEXT = {"verifier": "heap-verifier violation", "crash": "crash", "": "failure"}


def issue_body(group, run_url=""):
    """The body of a new failure's own issue. `group` is one item or the list
    of items (one per leg) that share a cross-leg signature."""
    group = [group] if isinstance(group, dict) else list(group)
    item = group[0]
    legs = group_legs(group)
    kinds = sorted({g.get("kind", "") for g in group} - {""}, key=lambda k: KIND_RANK[k])
    why = []
    if len(legs) > 1:
        why.append("new on %d legs" % len(legs))
    if kinds:
        why.append(" and ".join(KIND_TEXT[k] for k in kinds))
    lines = [
        "A new failure from the nightly hardening workflow%s." % (
            " (%s)" % ", ".join(why) if why else ""),
        "",
        "Issue key: `%s` (step and test, without the leg)" % issue_key(item),
        "",
        "| Signature | Leg | Step | Configuration | Test | Message | Occurrences |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    for g in group:
        lines.append("| `%s` | %s | %s | %s | %s | %s | %s |" % (
            g["id"], g["leg"], g["step"], g["config"] or "-", g["test"] or "-",
            (g["message"] or "-").replace("|", "\\|").replace("\n", " ")[:160], g["count"]))
    lines += [
        "",
        "**Message:** %s" % (item["message"] or "-"),
        "",
    ]
    for g in group:
        artifact = "nightly-%s" % g["leg"]
        lines += [
            "**%s:** seed %s; reduced program %s; artifacts `%s`%s%s" % (
                g["leg"],
                "`%s`" % g["seed"] if g["seed"] else "none recorded",
                "`%s` (in artifact `%s`)" % (g["reduced"], artifact) if g["reduced"]
                else "none recorded",
                artifact,
                ", log `%s`" % g["log"] if g.get("log") else "",
                " from %s" % run_url if run_url else ""),
            "",
        ]
    for g in group:
        if g["detail"].strip():
            title = "### Output" if len(group) == 1 else "### Output (%s)" % g["leg"]
            lines += [title, "", fence(g["detail"].rstrip()), ""]
            if len(group) > 1:
                lines += ["The other legs' output is in their artifacts.", ""]
            break
    fuzz_sig = known_schema.fuzz_signature(item["test"], item["message"]) \
        if item["step"] == known_schema.FUZZ_STEP else None
    if fuzz_sig is not None:
        # the same entry then suppresses it in local run_fuzz runs too
        snippet = {
            "signature": "^%s$" % re.escape(fuzz_sig),
            "issue": "#<this issue>",
            "note": "<why this is expected>",
        }
    elif len(legs) == 1 and len(group) == 1:
        snippet = {
            "id": item["id"],
            "leg": "^%s$" % re.escape(item["leg"]),
            "step": "^%s$" % re.escape(item["step"]),
            "message": re.escape(item["message"][:120]),
            "issue": "#<this issue>",
            "note": "<why this is expected>",
        }
    else:
        snippet = {
            "leg": "^(?:%s)$" % "|".join(re.escape(l) for l in legs),
            "step": "^%s$" % re.escape(item["step"]),
            "test": "^%s$" % re.escape(item["test"]),
            "message": message_regex(RUNNER_SUFFIX_RE.sub("", item["message"])),
            "issue": "#<this issue>",
            "note": "<why this is expected>",
        }
    lines += [
        "### Triage",
        "",
        "Fix it, or, if it is understood and tracked, add its signature to "
        "`tools/fuzz/known.json` so later nightlies list it as known:",
        "",
        "~~~json",
        json.dumps(snippet, indent=2),
        "~~~",
        "",
    ]
    return "\n".join(lines)[:BODY_LIMIT]


def summary_markdown(report, run_url=""):
    items = report["items"]
    by = {"new": [], "known": [], "infra": []}
    for i in items:
        by[i["class"]].append(i)
    head = {"green": "all steps passed on every leg",
            "known": "only known failures",
            "infra": "infrastructure failures, nothing new",
            "new": "NEW failures"}[report["status"]]
    lines = ["### Nightly hardening: %s" % head, "",
             "%d result files read; %d new, %d known, %d infra."
             % (report["results_read"], len(by["new"]), len(by["known"]), len(by["infra"])),
             ""]
    if run_url:
        lines += ["Run: %s" % run_url, ""]

    def cell(text):
        return (text or "").replace("|", "\\|").replace("\n", " ")[:160]

    if by["new"]:
        groups = issue_groups(items)
        lines += ["#### New", ""]
        if groups:
            lines += ["%d of them get an issue of their own (new on more than one leg, "
                      "or a crash or verifier violation):" % len(groups), ""]
            for g in groups:
                kinds = sorted({x["kind"] for x in g} - {""}, key=lambda k: KIND_RANK[k])
                lines.append("- `%s` %s %s on %s%s" % (
                    issue_key(g[0]), g[0]["step"], g[0]["test"] or "(step)",
                    ", ".join(group_legs(g)),
                    " (%s)" % ", ".join(KIND_TEXT[k] for k in kinds) if kinds else ""))
            lines.append("")
        else:
            lines += ["None gets an issue of its own: each is new on one leg only and "
                      "none is a crash or verifier violation.", ""]
        by_leg_step = {}
        order = []
        for i in by["new"]:
            key = (i["leg"], i["step"])
            if key not in by_leg_step:
                by_leg_step[key] = []
                order.append(key)
            by_leg_step[key].append(i)
        for leg, step in order:
            lines += ["##### %s / %s" % (leg, step), "",
                      "| Signature | Config | Test | Message | Kind | Also new on | Seed | Count |",
                      "| --- | --- | --- | --- | --- | --- | --- | --- |"]
            for i in by_leg_step[(leg, step)]:
                others = [l for l in i.get("legs", []) if l != leg]
                lines.append("| `%s` | %s | %s | %s | %s | %s | %s | %s |" % (
                    i["id"], i["config"] or "-", i["test"] or "-", cell(i["message"]),
                    KIND_TEXT[i.get("kind", "")], ", ".join(others) or "-",
                    i["seed"] or "-", i["count"]))
            lines.append("")

    for klass, title in (("known", "Known"), ("infra", "Infrastructure")):
        if not by[klass]:
            continue
        lines += ["#### %s" % title, "",
                  "| Signature | Leg | Step | Config | Test | Message | Seed | Count |",
                  "| --- | --- | --- | --- | --- | --- | --- | --- |"]
        for i in by[klass]:
            ref = ""
            if klass == "known" and i.get("known", {}).get("issue"):
                ref = " (%s)" % i["known"]["issue"]
            lines.append("| `%s` | %s | %s | %s | %s | %s%s | %s | %s |" % (
                i["id"], i["leg"], i["step"], i["config"] or "-", i["test"] or "-",
                cell(i["message"]), ref, i["seed"] or "-", i["count"]))
        lines.append("")
    return "\n".join(lines)


def cmd_triage(args):
    legs = [l for l in args.legs.split(",") if l]
    steps = [s for s in args.steps.split(",") if s]
    report = classify(args.results, args.known, legs, steps)
    os.makedirs(os.path.join(args.out, "issues"), exist_ok=True)

    summary = summary_markdown(report, args.run_url)
    with open(os.path.join(args.out, "summary.md"), "w", encoding="utf-8") as f:
        f.write(summary + "\n")

    new = [i for i in report["items"] if i["class"] == "new"]
    groups = issue_groups(report["items"])
    issues = []
    for group in groups[:args.max_issues]:
        key = issue_key(group[0])
        name = "issues/%s.md" % key
        with open(os.path.join(args.out, name), "w", encoding="utf-8") as f:
            f.write(issue_body(group, args.run_url))
        issues.append({"id": key, "workflow": issue_title(group[0]), "body_file": name,
                       "legs": group_legs(group)})

    tracking = summary
    if len(groups) > args.max_issues:
        tracking += ("\n%d more failures qualify for an issue of their own but have none "
                     "this run (cap %d); they are in the tables above.\n"
                     % (len(groups) - args.max_issues, args.max_issues))
    with open(os.path.join(args.out, "tracking.md"), "w", encoding="utf-8") as f:
        f.write(tracking[:BODY_LIMIT])
    with open(os.path.join(args.out, "triage.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
    with open(os.path.join(args.out, "new_issues.json"), "w", encoding="utf-8") as f:
        json.dump(issues, f, indent=2)

    counts = {k: sum(1 for i in report["items"] if i["class"] == k)
              for k in ("new", "known", "infra")}
    if args.github_output:
        with open(args.github_output, "a", encoding="utf-8") as f:
            f.write("status=%s\n" % report["status"])
            f.write("new_count=%d\n" % counts["new"])
            f.write("issue_count=%d\n" % len(issues))
            f.write("known_count=%d\n" % counts["known"])
            f.write("infra_count=%d\n" % counts["infra"])
            f.write("new_issues=%s\n" % json.dumps(issues, separators=(",", ":")))
    print(summary)
    return 0


# ---------------------------------------------------------------------------

def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    command = []
    if "--" in argv:
        i = argv.index("--")
        argv, command = argv[:i], argv[i + 1:]

    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("run")
    r.add_argument("--leg", required=True)
    r.add_argument("--step", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--config", default="")
    r.add_argument("--cwd", default=None)
    r.add_argument("--timeout", type=int, default=0)
    r.add_argument("--parser", choices=["generic", "regression"], default="generic")
    r.add_argument("--default-seed", default="")
    r.add_argument("--require", action="append", default=[])
    r.add_argument("--require-text", action="append", default=[])

    lp = sub.add_parser("loop")
    lp.add_argument("--leg", required=True)
    lp.add_argument("--out", required=True)
    lp.add_argument("--bin", required=True)
    lp.add_argument("--tests", required=True)
    lp.add_argument("--runs", type=int, default=20)
    lp.add_argument("--modes", default="off,1")
    lp.add_argument("--src-dir", default="programs/regression")
    lp.add_argument("--timeout", type=int, default=300)
    lp.add_argument("--budget", type=int, default=0,
                    help="seconds for the whole loop (0 = unlimited); keep it "
                    "below the step's timeout-minutes")
    lp.add_argument("--opt", default="s3")

    t = sub.add_parser("triage")
    t.add_argument("--results", required=True)
    t.add_argument("--known", required=True)
    t.add_argument("--legs", required=True)
    t.add_argument("--steps", required=True)
    t.add_argument("--out", required=True)
    t.add_argument("--run-url", default="")
    t.add_argument("--github-output", default="")
    t.add_argument("--max-issues", type=int, default=5)

    args = p.parse_args(argv)
    if args.cmd == "run":
        # --require and --require-text paths are relative to the invoking
        # directory, not --cwd, so the workflow names them from the repo root.
        return cmd_run(args, command)
    if args.cmd == "loop":
        return cmd_loop(args)
    return cmd_triage(args)


if __name__ == "__main__":
    sys.exit(main())
