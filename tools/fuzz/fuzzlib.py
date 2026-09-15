"""Shared plumbing for the Objeck differential fuzzer: running the toolchain
under every configuration, partitioning the outputs, computing signatures and
applying known.json suppressions. Python standard library only.

A configuration is an (optimization level, JIT mode) pair. Every program is
compiled at s0 and s3 and run under each configuration; s0 with the JIT off is
the reference, because it exercises the least machinery (no optimizer, no
native code). Configurations whose stdout and zero/non-zero exit agree form one
output class; the partition of configurations into classes is the heart of a
finding's signature.
"""

import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import known_schema  # noqa: E402

# (name, opt level, obr flags). The first entry is the reference.
CONFIGS = [
    ("s0/off", "s0", ["--jit=off"]),
    ("s3/off", "s3", ["--jit=off"]),
    ("s3/default", "s3", []),
    ("s3/jit1", "s3", ["--jit=1"]),
    ("s0/jit1", "s0", ["--jit=1"]),
]
REFERENCE = CONFIGS[0][0]
OPT_LEVELS = ["s0", "s3"]

# the configuration whose OBJECK_JIT_REPORT stderr gives the compiled fraction
REPORT_CONFIG = "s3/jit1"

# Windows reports a native crash as an NTSTATUS exit code; POSIX as a signal
_CRASH_CODES = {
    3221225477: "access violation",       # 0xC0000005
    3221225725: "stack overflow",         # 0xC00000FD
    3221226505: "stack buffer overrun",   # 0xC0000409
    3221225620: "integer divide by zero", # 0xC0000094
    3221225501: "illegal instruction",    # 0xC000001D
    3221225786: "control-c exit",         # 0xC000013A
}


def tool_command(path):
    """A tool given as a .py file is run with this interpreter (fault wrappers)."""
    if path.lower().endswith(".py"):
        return [sys.executable, path]
    return [path]


class Toolchain:
    """obc/obr from a deploy tree's bin directory, optionally overridden."""

    def __init__(self, bin_dir, obc=None, obr=None, timeout=30.0):
        self.bin_dir = os.path.abspath(bin_dir)
        exe = ".exe" if os.name == "nt" else ""
        self.obc = obc or os.path.join(self.bin_dir, "obc" + exe)
        self.obr = obr or os.path.join(self.bin_dir, "obr" + exe)
        self.timeout = timeout
        lib = os.path.join(os.path.dirname(self.bin_dir), "lib")
        env = dict(os.environ)
        env["OBJECK_LIB_PATH"] = lib
        native = os.path.join(lib, "native")
        env["PATH"] = os.pathsep.join([self.bin_dir, native, env.get("PATH", "")])
        # a leftover per-test opt-out would silently turn every JIT config off
        env.pop("OBJECK_JIT_DISABLE", None)
        env.pop("OBJECK_JIT_THRESHOLD", None)
        self.env = env

    def compile(self, src, opt, dest):
        if os.path.exists(dest):
            os.remove(dest)
        cmd = tool_command(self.obc) + ["-src", os.path.abspath(src), "-opt", opt,
                                        "-dest", os.path.abspath(dest)]
        return _run(cmd, self.env, self.bin_dir, self.timeout * 4)

    def run(self, obe, flags, report=False):
        env = self.env
        if report:
            env = dict(env, OBJECK_JIT_REPORT="1")
        cmd = tool_command(self.obr) + list(flags) + [os.path.abspath(obe)]
        return _run(cmd, env, os.path.dirname(os.path.abspath(obe)), self.timeout)


class Result:
    def __init__(self, code, stdout, stderr, seconds, timed_out=False):
        self.code = code
        self.stdout = stdout
        self.stderr = stderr
        self.seconds = seconds
        self.timed_out = timed_out

    def to_json(self):
        return {"code": self.code, "stdout": self.stdout[-4000:], "stderr": self.stderr[-4000:],
                "seconds": round(self.seconds, 3), "timed_out": self.timed_out}


def _run(cmd, env, cwd, timeout):
    start = time.monotonic()
    try:
        p = subprocess.run(cmd, env=env, cwd=cwd, capture_output=True, timeout=timeout)
        return Result(p.returncode, _text(p.stdout), _text(p.stderr), time.monotonic() - start)
    except subprocess.TimeoutExpired as e:
        return Result(None, _text(e.stdout), _text(e.stderr), time.monotonic() - start, timed_out=True)


def _text(b):
    if b is None:
        return ""
    return b.decode("utf-8", "replace").replace("\r\n", "\n")


def _unsigned(code):
    return code & 0xFFFFFFFF if code is not None and code < 0 else code


def is_crash_code(code):
    if code is None:
        return False
    c = _unsigned(code)
    return c in _CRASH_CODES or c >= 0xC0000000 or (os.name != "nt" and code < 0)


def describe_exit(result):
    if result.timed_out:
        return "timeout"
    c = _unsigned(result.code)
    if c in _CRASH_CODES:
        return _CRASH_CODES[c]
    if c is not None and c >= 0xC0000000:
        return "native exception 0x%08X" % c
    if result.code is not None and result.code < 0:
        return "signal %d" % -result.code
    return "exit %s" % result.code


# ---------------------------------------------------------------------------
# classification

def outcome_key(result):
    """What two runs must share to land in the same output class."""
    if result is None:
        return ("missing",)
    if result.timed_out:
        return ("timeout",)
    if is_crash_code(result.code):
        return ("crash", describe_exit(result), result.stdout)
    return ("ok" if result.code == 0 else "fail", result.stdout)


def partition(results, order):
    """Group configuration names by outcome. Returns a list of lists, each in
    `order` order, the groups themselves ordered by their first member."""
    groups = []
    keys = []
    for name in order:
        k = outcome_key(results.get(name))
        for i, existing in enumerate(keys):
            if existing == k:
                groups[i].append(name)
                break
        else:
            keys.append(k)
            groups.append([name])
    return groups


def partition_text(groups):
    return " | ".join(",".join(g) for g in groups)


_NUM = re.compile(r"-?\d+")
_HEX = re.compile(r"0x[0-9A-Fa-f]+")


def normalize(line):
    """Numbers and addresses out, so a signature survives a different seed."""
    return _NUM.sub("#", _HEX.sub("0x#", line.strip()))


def first_difference(ref_out, other_out):
    """(line number, reference line, other line) of the first differing stdout
    line, or None when they are equal."""
    a = ref_out.split("\n")
    b = other_out.split("\n")
    for i in range(max(len(a), len(b))):
        la = a[i] if i < len(a) else "<eof>"
        lb = b[i] if i < len(b) else "<eof>"
        if la != lb:
            return (i + 1, la, lb)
    return None


def error_text(result):
    """The VM's own diagnosis (the '>>> ... <<<' line) or the last stderr line,
    ignoring the JIT report's '[jit]' chatter."""
    lines = [l for l in result.stderr.split("\n") if l.strip() and not l.lstrip().startswith("[jit]")
             and "local(s) in loop" not in l]
    for l in lines:
        if ">>>" in l:
            return l.strip()
    return lines[-1].strip() if lines else ""


def top_frame(result):
    """Objeck prints no native stack. The top frame is the first method of the
    VM's 'Unwinding local stack' trace (method: pos=N, name='...'), else the
    method named in its error line (method='...'); a native crash has neither."""
    m = re.search(r"method: pos=\d+, name='([^']*)'", result.stderr)
    if not m:
        m = re.search(r"method='([^']*)'", result.stderr)
    return m.group(1) if m else ""


def failure_text(result):
    """' <exit> <error text> @<top frame>' for a failed run, '' for a clean one."""
    if result is None or (result.code == 0 and not result.timed_out):
        return ""
    parts = [describe_exit(result)]
    err = normalize(error_text(result))
    if err:
        parts.append(err)
    frame = top_frame(result)
    if frame:
        parts.append("@" + frame)
    return " " + " ".join(parts)


def signature(results, order=None):
    """A stable text identifying the kind of failure, or None when every
    configuration agrees with the reference and exits 0.

    diverge: <partition> first=<normalized reference line> vs <normalized other>[ <exit> <error> @<frame>]
    crash:   <config> <exit description>[ <error text>][ @<top frame>]
    """
    order = order or [c[0] for c in CONFIGS]
    groups = partition(results, order)
    ref = results.get(order[0])
    bad = []
    for name in order:
        r = results.get(name)
        if r is None:
            continue
        if r.timed_out or is_crash_code(r.code) or (r.code != 0 and name != "compile"):
            bad.append(name)
    if len(groups) == 1 and not bad:
        return None

    parts = []
    crashed = [n for n in order if results.get(n) is not None and
               (results[n].timed_out or is_crash_code(results[n].code))]
    if crashed:
        n = crashed[0]
        r = results[n]
        parts.append("crash: %s%s" % (n, failure_text(r)))
    if len(groups) > 1:
        other_name = groups[1][0]
        other = results.get(other_name)
        diff = first_difference(ref.stdout if ref else "", other.stdout if other else "")
        tail = "" if other_name in crashed else failure_text(other)
        if diff:
            parts.append("diverge: %s first=%s vs %s%s" % (partition_text(groups), normalize(diff[1]),
                                                            normalize(diff[2]), tail))
        else:
            parts.append("diverge: %s exit=%s vs %s%s" % (partition_text(groups), describe_exit(ref) if ref else "-",
                                                           describe_exit(other) if other else "-", tail))
    elif bad and not crashed:
        r = results[bad[0]]
        parts.append("fail: all%s" % failure_text(r))
    return "; ".join(parts)


# ---------------------------------------------------------------------------
# JIT report

_REJECT = re.compile(r"^\[jit\] (\S+?): (not compiled -- .*|compile failed.*)$")
# Positive evidence (OBJECK_JIT_REPORT=1): a compile that succeeded -- the
# shared JitCompiler::TryAutoJitCompile line or the interpreter's entry-method
# line -- or a callee whose code was inlined into a compiled caller (AMD64).
_COMPILED = re.compile(r"^\[jit\] (\S+?): compiled(?: on entry\b.*)?$")
_INLINED = re.compile(r"^\[jit\] (\S+?): inlined (\S+)$")
NEVER_COMPILED = " (no compile reported)"


def jit_rejections(stderr):
    """{method name: reason} for every method the JIT handed back."""
    out = {}
    for line in stderr.split("\n"):
        m = _REJECT.match(line.strip())
        if m:
            out.setdefault(m.group(1), m.group(2))
    return out


def jit_compiled(stderr):
    """Full names of every method the report says reached native code."""
    out = set()
    for line in stderr.split("\n"):
        line = line.strip()
        m = _COMPILED.match(line)
        if m:
            out.add(m.group(1))
            continue
        m = _INLINED.match(line)
        if m:
            out.add(m.group(2))
    return out


def _short(full):
    return ":".join(full.split(":")[:2])


def jit_coverage(stderr, generated, prefixes):
    """(compiled, total, uncovered names) over the generator's methods.

    A method counts as compiled only on positive evidence: a 'compiled' or
    'inlined' report line naming it, and no rejection. Subtracting rejections
    from the total instead scored a VM that ignored --jit=1 -- no report lines
    at all -- as 100% compiled.

    `generated` holds 'Class:Method' names, matched on that prefix of the
    report's full 'Class:Method:signature' form. A reported method under one
    of `prefixes` the generator did not list (a lambda, Main) joins the total.
    Uncovered names are the rejected ones plus generated methods with no
    report line, the latter suffixed NEVER_COMPILED."""
    gen = set(generated)
    rejected = jit_rejections(stderr)
    compiled = jit_compiled(stderr)
    ours = lambda full: any(full.startswith(p) for p in prefixes)
    rej_gen = {_short(f) for f in rejected if _short(f) in gen}
    comp_gen = {_short(f) for f in compiled if _short(f) in gen} - rej_gen
    rej_extra = {f for f in rejected if _short(f) not in gen and ours(f)}
    comp_extra = {f for f in compiled if _short(f) not in gen and ours(f)} - rej_extra
    total = len(gen) + len(rej_extra) + len(comp_extra)
    silent = {n + NEVER_COMPILED for n in gen - comp_gen - rej_gen}
    return len(comp_gen) + len(comp_extra), total, sorted(rej_gen | rej_extra | silent)


# ---------------------------------------------------------------------------
# one program through every configuration

class Outcome:
    def __init__(self):
        self.compile = {}         # opt level -> Result
        self.results = {}         # config name -> Result
        self.signature = None
        self.coverage = (0, 0, [])
        self.seconds = 0.0

    def ref_seconds(self):
        r = self.results.get(REFERENCE)
        return r.seconds if r else 0.0

    def partition_text(self):
        return partition_text(partition(self.results, [c[0] for c in CONFIGS]))

    def to_json(self):
        return {"signature": self.signature,
                "partition": self.partition_text() if self.results else None,
                "compile": {k: v.to_json() for k, v in self.compile.items()},
                "results": {k: v.to_json() for k, v in self.results.items()},
                "jit_compiled": self.coverage[0], "jit_total": self.coverage[1],
                "jit_rejected": self.coverage[2], "seconds": round(self.seconds, 3)}


def compile_signature(compiles):
    """A signature for a compile that crashed, failed or wrote nothing."""
    for opt in OPT_LEVELS:
        r, wrote = compiles[opt]
        if r.timed_out or is_crash_code(r.code):
            return "compile: %s %s" % (opt, describe_exit(r))
        if r.code != 0:
            lines = [l for l in (r.stdout + "\n" + r.stderr).split("\n") if l.strip()]
            err = next((l for l in lines if "):" in l), lines[-1] if lines else "")
            err = re.sub(r"^.*?:\(\d+,\d+\):\s*", "", err)
            return "compile: %s error %s" % (opt, normalize(err))
        if not wrote:
            return "compile: %s missing .obe" % opt
    return None


def evaluate(tc, text, workdir, stem, methods=(), prefixes=()):
    """Compile `text` at every opt level and run every configuration."""
    start = time.monotonic()
    out = Outcome()
    os.makedirs(workdir, exist_ok=True)
    src = os.path.join(workdir, stem + ".obs")
    with open(src, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    compiles = {}
    for opt in OPT_LEVELS:
        dest = os.path.join(workdir, "%s_%s.obe" % (stem, opt))
        r = tc.compile(src, opt, dest)
        out.compile[opt] = r
        compiles[opt] = (r, os.path.exists(dest))
    sig = compile_signature(compiles)
    if sig is not None:
        out.signature = sig
        out.seconds = time.monotonic() - start
        return out
    for name, opt, flags in CONFIGS:
        obe = os.path.join(workdir, "%s_%s.obe" % (stem, opt))
        report = name == REPORT_CONFIG
        out.results[name] = tc.run(obe, flags, report=report)
        if report:
            out.coverage = jit_coverage(out.results[name].stderr, methods, prefixes)
    out.signature = signature(out.results)
    out.seconds = time.monotonic() - start
    return out


# ---------------------------------------------------------------------------
# known.json

#
# The schema lives in known_schema.py, shared with tools/cicd/nightly_triage.py.

def load_known(path):
    """Validated entries of a known.json; raises known_schema.KnownError."""
    return known_schema.load(path)


def match_known(sig, known, leg=""):
    """The raw known entry matching fuzzer signature `sig` on `leg`, or None.
    The fuzzer is always step 'fuzz'."""
    if sig is None:
        return None
    return known_schema.match(known, {"signature": sig, "leg": leg, "step": known_schema.FUZZ_STEP})
