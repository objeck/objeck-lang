#!/usr/bin/env python3
"""
A library method whose body is a trap must not crash the VM when a valid program
hands it Nil: as its receiver, or as an argument.

Usage: check_nil_trap_probe.py <bin_dir> [--list] [--keep <dir>] [--jobs <n>]

Why this exists. Most of the methods in core/compiler/lib_src/lang.obs that the VM
implements itself have a one-line body, a directive such as STRING_HASH_ID, which
compiles to a TRAP instruction. They are not virtual, so the call does not look at
the receiver, and the handler in core/vm/common.cpp gets whatever the program passed.
31 of them read through a Nil receiver unchecked: 's : String; select(s) {...}',
'w : Serializer; w->Write(1);' and 't : Timer; t->Start();' ended the VM with a
segmentation fault in every JIT mode. A crash like that is found by calling the
method, not by reading 270 handlers, so this check calls all of them.

What it does. It reads lang.obs, takes every public method, function and constructor
whose body holds a directive, and writes ONE program with a case for
  - each instance method, called on a Nil receiver with ordinary arguments, and
  - each reference-typed parameter, passed Nil while the receiver and the other
    arguments are real.
The program is compiled once and each case is run on its own, with the default JIT,
--jit=off and --jit=1. A case passes when the VM ends with status 0 or 1 by itself:
the Nil runtime error, an error the library reports, or a method that tolerates Nil.
A signal, an access violation or a hang fails. So a new trap-bodied method is covered
the day it is written; if the check cannot write a call for it (a parameter type it
has no value for), it fails and says which, and the type goes in VALUES below.

What it leaves out, by name, so the list cannot grow unnoticed (SKIPPED below): the
Nil-argument cases of classes that need a live peer or a subclass to have a receiver
at all (sockets, sessions, pipes, threads), and the cases that would make a real
network connection. Their Nil-RECEIVER cases are all here.

--list prints the cases and exits; --keep leaves the program and its files in <dir>.
"""
import concurrent.futures
import os
import re
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""
HERE = os.path.dirname(os.path.abspath(__file__))
LANG = os.path.normpath(os.path.join(HERE, "..", "..", "core", "compiler", "lib_src", "lang.obs"))
MODES = (("the default JIT", []), ("--jit=off", ["--jit=off"]), ("--jit=1", ["--jit=1"]))
NIL_MESSAGE = "Attempting to dereference a 'Nil' memory"

# A value of each parameter type the directive-bodied methods take. Nothing here is Nil.
VALUES = {
    "Int": "1", "Float": "1.0", "Bool": "true", "Char": "'a'", "Byte": "1",
    "String": '"nil_probe"', "Base": '"nil_probe"', "Error": 'Error->New("nil_probe")',
    "Number->Format": "Number->Format->DEC",
    "Pipe->Mode": "System.IO.Pipe->Mode->OPEN",
    "ThreadMutex": 'System.Concurrency.ThreadMutex->New("nil_probe")',
}

# A receiver for the Nil-argument cases, by class. The two files are made by this check
# in the directory the cases run in.
RECEIVERS = {
    "System.String": '"nil_probe"',
    "System.IO.Serializer": "System.IO.Serializer->New()",
    "System.IO.Filesystem.FileReader": 'System.IO.Filesystem.FileReader->New("nil_probe_in.tmp")',
    "System.IO.Filesystem.FileWriter": 'System.IO.Filesystem.FileWriter->New("nil_probe_out.tmp")',
    "System.IO.Filesystem.FileReaderWriter": 'System.IO.Filesystem.FileReaderWriter->New("nil_probe_in.tmp")',
    "System.API.DllProxy": 'System.API.DllProxy->New("nil_probe")',
}

# Nil-argument cases that are not made, and why. Each is 'Bundle.Class' -> reason; every
# class named here must still have such cases, or the entry is stale and the check says so.
NO_RECEIVER = {
    "System.Concurrency.Thread": "a Thread is a subclass the program writes",
    "System.IO.Pipe": "a Pipe needs a peer process",
    "System.IO.Net.TCPSocket": "needs a live connection",
    "System.IO.Net.TCPSecureSocket": "needs a live connection",
    "System.IO.Net.UDPSocket": "needs a bound peer",
    "System.IO.Net.UDPSocketServer": "needs a bound port",
    "System.IO.Net.DTLSSocket": "needs a live connection",
    "System.IO.Net.Http2Session": "needs a live connection",
    "System.IO.Net.Http3Session": "needs a live connection",
}
# Constructors and functions of these classes are given a Nil address only: a real
# address with another argument Nil would connect to it.
NETWORK = "System.IO.Net."
# Runtime's signal functions install a handler or raise a signal: neither is a Nil probe.
NOT_PROBED = {"System.Runtime:SetSignal", "System.Runtime:RaiseSignal"}

PRIMITIVES = ("Int", "Float", "Bool", "Char", "Byte")


def strip_comments(text):
    """lang.obs without its comments, line numbers kept."""
    text = re.sub(r"#~.*?~#", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"#[^\n]*", "", text)


def split_params(text):
    """'a : Int, b : Bool[,]' -> [('a', 'Int'), ('b', 'Bool[,]')]; a default value is dropped."""
    params, depth, start = [], 0, 0
    for i, ch in enumerate(text + ","):
        if ch in "[(":
            depth += 1
        elif ch in "])":
            depth -= 1
        elif ch == "," and depth == 0:
            part = text[start:i].strip()
            start = i + 1
            if part:
                name, _, kind = part.partition(":")
                params.append((name.strip(), kind.split(":=")[0].strip()))
    return params


def trap_methods(path=LANG):
    """Every method, function and constructor of lang.obs whose body holds a directive.

    Returns (methods, classes): dicts with bundle, cls, kind ('method', 'function' or
    'New'), public, name, params, line; and {class name: bundle} for every class."""
    with open(path, encoding="utf-8") as f:
        text = strip_comments(f.read())
    head = re.compile(
        r"\bbundle\s+(?P<bundle>[\w.]+)\s*\{"
        r"|\b(?P<scope>class|interface|enum|consts)\s+(?P<scoped>\$?\w+)[^{;]*\{"
        r"|\b(?P<kind>method|function)\s*:\s*(?P<quals>(?:\w+\s*:\s*)*)(?P<name>\w+)\s*\((?P<params>[^)]*)\)\s*~\s*[^{;]+?\{"
        r"|\bNew\s*(?::\s*(?P<newqual>\w+)\s*)?\((?P<newparams>[^)]*)\)\s*\{")

    def block_end(i):
        depth = 1
        while depth:
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        return i

    methods, classes = [], {}
    bundle = cls = None
    pos = 0
    while True:
        m = head.search(text, pos)
        if not m:
            break
        pos = m.end()
        if m.group("bundle"):
            bundle = m.group("bundle")
        elif m.group("scope") == "class":
            cls = m.group("scoped").lstrip("$")
            classes[cls] = bundle
        elif m.group("scope"):
            pos = block_end(pos)
        else:
            end = block_end(pos)
            body, pos = text[pos:end - 1], end
            if not re.search(r"^\s*[A-Z][A-Z0-9_]+;", body, flags=re.M):
                continue
            if m.group("kind"):
                quals = m.group("quals")
                # a function is public unless it says otherwise; a method is private unless it does
                public = "private" not in quals and (m.group("kind") == "function" or "public" in quals)
                kind, name, params = m.group("kind"), m.group("name"), m.group("params")
            else:
                kind, name, params = "New", "New", m.group("newparams")
                public = m.group("newqual") != "private"
            methods.append({"bundle": bundle, "cls": cls, "kind": kind, "public": public, "name": name,
                            "params": split_params(params), "line": text.count("\n", 0, m.start()) + 1})
    return methods, classes


def is_reference(kind):
    return "[" in kind or kind.split(".")[-1] not in PRIMITIVES and "->" not in kind and "(" not in kind


def qualified(kind, classes):
    """A parameter type as a program outside the library writes it."""
    base, bracket, dims = kind.partition("[")
    short = base.split(".")[-1]
    if "." not in base and short in classes and classes[short] != "System":
        base = classes[short] + "." + short
    return base + bracket + dims


def value_of(kind, classes):
    """An expression of the given type that is not Nil, or None when VALUES has none."""
    base, bracket, dims = kind.partition("[")
    short = base.split(".")[-1]
    if bracket:
        # one more dimension per comma: Int[,] -> Int->New[2,2]
        sizes = ",".join(["2"] * (dims.count(",") + 1))
        return f"{qualified(base, classes)}->New[{sizes}]"
    return VALUES.get(kind, VALUES.get(short))


def build_cases(methods, classes):
    """(cases, unbuildable, skipped). A case is (label, [statements])."""
    cases, unbuildable, skipped = [], [], {}
    for m in methods:
        owner = f"{m['bundle']}.{m['cls']}"
        # as a program writes it: Int, Bool and the other System classes go by their short name
        spelled = m["cls"] if m["bundle"] == "System" else owner
        signature = f"{owner}:{m['name']}({', '.join(k for _, k in m['params'])})"
        if not m["public"] or f"{owner}:{m['name']}" in NOT_PROBED:
            continue
        values = [value_of(k, classes) for _, k in m["params"]]
        refs = [i for i, (_, k) in enumerate(m["params"]) if is_reference(k)]
        if m["kind"] != "method" and not refs:
            continue
        if None in values:
            unbuildable.append(f"{signature} (lang.obs:{m['line']}): no value for "
                               f"'{m['params'][values.index(None)][1]}'")
            continue

        def call(target, args):
            return f"{target}->{m['name']}({', '.join(args)});"

        if m["kind"] == "method":
            cases.append((f"Nil receiver: {signature}", [f"r : {spelled};", call("r", values)]))
        if m["kind"] == "method" and refs:
            if owner in NO_RECEIVER:
                skipped.setdefault(owner, 0)
                skipped[owner] += len(refs)
                continue
            if owner not in RECEIVERS:
                unbuildable.append(f"{signature} (lang.obs:{m['line']}): no receiver for '{owner}'")
                continue
        if owner.startswith(NETWORK):
            refs = refs[:1]
        for i in refs:
            name, kind = m["params"][i]
            args = values[:i] + ["nil_arg"] + values[i + 1:]
            body = [f"nil_arg : {qualified(kind, classes)};"]
            if m["kind"] == "method":
                body += [f"r := {RECEIVERS[owner]};", call("r", args)]
            elif m["cls"] == "BaseArray":
                # $BaseArray has no name a program can write: its functions are an array's methods
                body.append(call(args[0], args[1:]))
            else:
                body.append(call(spelled, args))
            cases.append((f"Nil argument '{name}': {signature}", body))
    stale = sorted(set(NO_RECEIVER) - set(skipped))
    unbuildable += [f"NO_RECEIVER names '{owner}', which has no Nil-argument case to leave out" for owner in stale]
    return cases, unbuildable, skipped


def program_of(cases):
    """One class: a function per case, and a Main that runs the one its argument numbers."""
    out = ["# written by check_nil_trap_probe.py: do not edit", "class NilTrapProbe {"]
    for n, (label, body) in enumerate(cases):
        out.append(f"\t# {label}")
        out.append(f"\tfunction : Case{n}() ~ Nil {{")
        out += ["\t\t" + line for line in body]
        out.append("\t}")
        out.append("")
    # groups of 50 keep any one method small
    for g in range(0, len(cases), 50):
        out.append(f"\tfunction : Group{g // 50}(which : Int) ~ Nil {{")
        for n in range(g, min(g + 50, len(cases))):
            out.append(f"\t\tif(which = {n}) {{ Case{n}(); }};")
        out.append("\t}")
        out.append("")
    out.append("\tfunction : Main(args : String[]) ~ Nil {")
    out.append("\t\twhich := args[0]->ToInt();")
    out.append('\t\t"probe {$which}"->PrintLine();')
    for g in range(0, len(cases), 50):
        out.append(f"\t\tif(which / 50 = {g // 50}) {{ Group{g // 50}(which); }};")
    out.append('\t\t"returned"->PrintLine();')
    out.append("\t}")
    out.append("}")
    return "\n".join(out) + "\n"


def run_case(obr, flags, image, n, env, cwd):
    """(status, text): status 'nil', 'ran', 'error', or a description of a failure."""
    try:
        p = subprocess.run([obr] + flags + [image, str(n)], cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    except subprocess.TimeoutExpired:
        return "did not end in 30 seconds", ""
    text = (p.stdout + p.stderr).decode("utf-8", errors="replace")
    if p.returncode not in (0, 1):
        # a signal (negative on POSIX) or an exception code (Windows): the VM crashed
        return f"ended with status {p.returncode}", text
    if f"probe {n}" not in text:
        return f"did not start (status {p.returncode})", text
    if p.returncode == 0:
        return "ran", text
    return ("nil" if NIL_MESSAGE in text else "error"), text


def probe(obc, obr, env, bin_dir, keep=None, jobs=None, report=print):
    """Runs every case; returns (failures, summary). failures is a list of strings."""
    methods, classes = trap_methods()
    cases, unbuildable, skipped = build_cases(methods, classes)
    if unbuildable:
        return [f"no call could be written for: {u}" for u in unbuildable], ""
    if len(cases) < 200:
        return [f"only {len(cases)} cases were found in {LANG}: the directive pattern no longer matches"], ""

    scratch = os.path.abspath(keep) if keep else tempfile.mkdtemp(prefix="nil_trap_probe_")
    os.makedirs(scratch, exist_ok=True)
    try:
        source = os.path.join(scratch, "nil_trap_probe.obs")
        image = os.path.join(scratch, "nil_trap_probe.obe")
        with open(source, "w", encoding="utf-8", newline="\n") as f:
            f.write(program_of(cases))
        with open(os.path.join(scratch, "nil_probe_in.tmp"), "wb") as f:
            f.write(b"nil probe input\n" * 4)
        p = subprocess.run([obc, "-src", source, "-dest", image], cwd=bin_dir, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if p.returncode != 0 or not os.path.exists(image):
            return ["the probe program did not compile: " + p.stdout.decode("utf-8", errors="replace")[-1500:]], ""

        failures, tally = [], {}
        jobs = jobs or min(8, os.cpu_count() or 2)
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            for label, flags in MODES:
                results = list(pool.map(lambda n: run_case(obr, flags, image, n, env, scratch), range(len(cases))))
                for (about, _), (status, text) in zip(cases, results):
                    kind = "receiver" if about.startswith("Nil receiver") else "argument"
                    if status in ("nil", "ran", "error"):
                        tally[(kind, status)] = tally.get((kind, status), 0) + 1
                    else:
                        failures.append(f"{about} {status} ({label}): {text.strip()[-200:]!r}")
        receivers = sum(1 for about, _ in cases if about.startswith("Nil receiver"))
        summary = (f"{receivers} Nil-receiver and {len(cases) - receivers} Nil-argument cases x {len(MODES)} modes; "
                   + ", ".join(f"{kind} {status}: {count}" for (kind, status), count in sorted(tally.items()))
                   + f"; left out: {sum(skipped.values())} Nil-argument cases of {len(skipped)} classes with no receiver")
        return failures, summary
    finally:
        if not keep:
            import shutil
            shutil.rmtree(scratch, ignore_errors=True)


def main():
    args = sys.argv[1:]
    if "--list" in args:
        methods, classes = trap_methods()
        cases, unbuildable, skipped = build_cases(methods, classes)
        for n, (label, body) in enumerate(cases):
            print(f"{n}\t{label}\t{' '.join(body)}")
        for u in unbuildable:
            print(f"UNBUILDABLE\t{u}")
        for owner, count in sorted(skipped.items()):
            print(f"LEFT OUT\t{owner}: {count} Nil-argument cases ({NO_RECEIVER[owner]})")
        return 1 if unbuildable else 0
    if not args or args[0].startswith("--"):
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(args[0])
    obc, obr = (os.path.join(bin_dir, name + EXE) for name in ("obc", "obr"))
    for p in (obc, obr):
        if not os.path.exists(p):
            print(f"ERROR: missing {p}")
            return 2
    keep = args[args.index("--keep") + 1] if "--keep" in args else None
    jobs = int(args[args.index("--jobs") + 1]) if "--jobs" in args else None
    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = os.path.join(os.path.dirname(bin_dir), "lib") + os.sep

    failures, summary = probe(obc, obr, env, bin_dir, keep=keep, jobs=jobs)
    for failure in failures:
        print(f"  [FAIL] {failure}")
    if failures:
        print(f"check_nil_trap_probe: {len(failures)} failure(s). {summary}")
        return 1
    print(f"check_nil_trap_probe: PASS. {summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
