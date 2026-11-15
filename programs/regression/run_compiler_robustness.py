#!/usr/bin/env python3
r"""Negative testing of the compiler front end: mutation and stress.

Usage: python run_compiler_robustness.py <bin_dir> [--programs N] [--seed S]

<bin_dir> must contain obc(.exe). Two kinds of input are compiled, and what the compiler does
with each is checked. None of it needs a program to run: it is about what obc says about code
that is wrong.

 1. Mutation. Valid regression programs are broken in sixteen known ways (a ';', ')', '(', '{',
    '}', ',' or keyword deleted; ':=' written '='; an identifier misspelled; a string left
    open; a number malformed; a stray character; a type swapped; an operator doubled; the file
    cut short; a call to a method that does not exist). Each case records the line it broke.
    Gates, over all the cases:
      - no crash and no hang, ever (exit status 0-3, under a time limit)
      - at least 80% report an error on or within two lines of the mutation (or name it in
        a note: a string left open is reported where it ends)
      - at most 5% cascade into more than four errors
      - at most 5% report only far from the mutation, at most 15% compile with no error
        (some mutations leave valid code: deleting a ';' at a line end is accepted)
    The seed and the program count are fixed by default, so a failure reruns identically.

 2. Stress. Hostile files: empty, NUL, random bytes, invalid UTF-8, truncated at every kind of
    token, deeply nested parentheses, blocks, calls and generics, thousands of chained
    operators, a 2 MB string, 20000 errors in one file, a call nested 30 deep to a method that
    does not exist. Each must end in a normal compiler exit, quickly. The nesting cases used to
    overflow the stack (exit 0xC00000FD) and the nested call took exponential time.

Why gates and not exact counts: the exact number of errors differs by platform and by what a
change to the parser improves. A crash, a hang or a drop in accuracy is what this catches.
"""
import os
import random
import re
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
EXE = ".exe" if os.name == "nt" else ""
NORMAL_EXITS = (0, 1, 2, 3)
COMPILE_TIMEOUT = 60
SLOW_SECONDS = 20


def prog(body, pre=""):
    return pre + "class Test {\n  function : Main(args : String[]) ~ Nil {\n" + body + "\n  }\n}\n"


# ------------------------------------------------------------------ mutation
def code_lines(lines):
    """Indexes of lines that are plain code: not a comment, a block comment or a 'use'."""
    out = []
    in_block = False
    for i, line in enumerate(lines):
        s = line.strip()
        if "#~" in s:
            in_block = True
        was = in_block
        if "~#" in s:
            in_block = False
        if was or "#" in s or not s or s.startswith("use "):
            continue
        out.append(i)
    return out


class Mutator:
    def __init__(self, rnd):
        self.rnd = rnd
        self.kinds = [
            "del_semicolon", "del_close_paren", "del_open_paren", "del_close_brace", "del_open_brace",
            "typo_identifier", "stray_char", "unterm_string", "assign_eq", "double_operator",
            "del_keyword", "bad_number", "del_comma", "swap_type", "truncate", "undefined_call",
        ]

    def pick(self, lines, pred):
        idx = [i for i in code_lines(lines) if pred(lines[i])]
        return self.rnd.choice(idx) if idx else None

    def sub(self, line, pattern, repl):
        ms = list(re.finditer(pattern, line))
        if not ms:
            return None
        m = self.rnd.choice(ms)
        return line[:m.start()] + repl(m) + line[m.end():]

    def apply(self, kind, lines):
        """Mutates 'lines' in place; returns the 0-based line, or None when it does not apply."""
        r = self.rnd
        no_str = lambda l: '"' not in l
        if kind == "del_semicolon":
            i = self.pick(lines, lambda l: l.rstrip().endswith(";") and no_str(l))
            if i is not None:
                lines[i] = lines[i].rstrip()[:-1]
        elif kind == "del_close_paren":
            i = self.pick(lines, lambda l: ")" in l and no_str(l))
            if i is not None:
                lines[i] = self.sub(lines[i], r"\)", lambda m: "")
        elif kind == "del_open_paren":
            i = self.pick(lines, lambda l: "(" in l and no_str(l))
            if i is not None:
                lines[i] = self.sub(lines[i], r"\(", lambda m: "")
        elif kind == "del_close_brace":
            i = self.pick(lines, lambda l: l.strip() == "}")
            if i is not None:
                lines[i] = ""
        elif kind == "del_open_brace":
            i = self.pick(lines, lambda l: l.rstrip().endswith("{"))
            if i is not None:
                lines[i] = lines[i].rstrip()[:-1]
        elif kind == "typo_identifier":
            i = self.pick(lines, lambda l: re.search(r"\b[a-z_]{5,}\b", l) and no_str(l)
                          and not l.strip().startswith(("use", "class", "function", "method", "bundle")))
            if i is not None:
                def swap(m):
                    w = m.group(0)
                    k = r.randrange(1, len(w) - 2)
                    return w[:k] + w[k + 1] + w[k] + w[k + 2:]
                lines[i] = self.sub(lines[i], r"\b[a-z_]{5,}\b", swap)
        elif kind == "stray_char":
            i = self.pick(lines, lambda l: ";" in l and no_str(l))
            if i is not None:
                ch = r.choice(["@", "`", "$", "?", "“", "\\"])
                lines[i] = lines[i].replace(";", " " + ch + ";", 1)
        elif kind == "unterm_string":
            i = self.pick(lines, lambda l: l.count('"') == 2)
            if i is not None:
                k = lines[i].rfind('"')
                lines[i] = lines[i][:k] + lines[i][k + 1:]
        elif kind == "assign_eq":
            i = self.pick(lines, lambda l: ":=" in l and no_str(l))
            if i is not None:
                lines[i] = lines[i].replace(":=", "=", 1)
        elif kind == "double_operator":
            i = self.pick(lines, lambda l: re.search(r" [+*/-] ", l) and no_str(l))
            if i is not None:
                lines[i] = self.sub(lines[i], r" ([+*/-]) ", lambda m: " %s %s " % (m.group(1), m.group(1)))
        elif kind == "del_keyword":
            i = self.pick(lines, lambda l: re.match(r"\s*(function|method|class|if|while|for|return|else)\b", l))
            if i is not None:
                lines[i] = re.sub(r"\b(function|method|class|if|while|for|return|else)\b", "", lines[i], 1)
        elif kind == "bad_number":
            i = self.pick(lines, lambda l: re.search(r"\b\d+\b", l) and no_str(l) and ":=" in l)
            if i is not None:
                lines[i] = self.sub(lines[i], r"\b\d+\b", lambda m: r.choice(["1..2", m.group(0) + "x", "0x", m.group(0) + "."]))
        elif kind == "del_comma":
            i = self.pick(lines, lambda l: "," in l and no_str(l))
            if i is not None:
                lines[i] = self.sub(lines[i], r",", lambda m: "")
        elif kind == "swap_type":
            i = self.pick(lines, lambda l: re.search(r"\b(Int|String|Float|Bool)\b", l) and ":=" in l)
            if i is not None:
                lines[i] = self.sub(lines[i], r"\b(Int|String|Float|Bool)\b",
                                    lambda m: r.choice([t for t in ["Int", "String", "Float", "Bool", "Nothing"] if t != m.group(1)]))
        elif kind == "truncate":
            ci = code_lines(lines)
            if len(ci) < 6:
                return None
            i = r.choice(ci[len(ci) // 3:])
            del lines[i + 1:]
        elif kind == "undefined_call":
            i = self.pick(lines, lambda l: re.search(r"->[A-Z]\w+\(", l))
            if i is not None:
                lines[i] = self.sub(lines[i], r"->([A-Z]\w+)\(", lambda m: "->" + m.group(1) + "Xq(")
        return i


def mutation_cases(reg_dir, nprog, seed):
    rnd = random.Random(seed)
    names = sorted(f for f in os.listdir(reg_dir) if f.endswith(".obs") and not f.startswith("bad_"))
    cands = []
    for n in names:
        with open(os.path.join(reg_dir, n), encoding="utf-8-sig", errors="replace") as f:
            t = f.read()
        if 600 <= len(t) <= 3500 and "# EXTRA_LIBS" not in t and "use " in t:
            cands.append(n)
    rnd.shuffle(cands)
    mut = Mutator(rnd)
    cases = []
    for n in cands[:nprog]:
        with open(os.path.join(reg_dir, n), encoding="utf-8-sig", errors="replace") as f:
            base = f.read().replace("\r\n", "\n").split("\n")
        for kind in mut.kinds:
            lines = list(base)
            i = mut.apply(kind, lines)
            if i is None or lines == base:
                continue
            cases.append({"prog": n, "kind": kind, "line": i + 1, "text": "\n".join(lines), "src": base[i].strip() if i < len(base) else ""})
    return cases


# ------------------------------------------------------------------- running
class Compiler:
    def __init__(self, bin_dir):
        self.obc = os.path.join(bin_dir, "obc" + EXE)
        self.cwd = bin_dir
        self.env = dict(os.environ, OBJECK_LIB_PATH=os.path.join(os.path.dirname(bin_dir), "lib") + os.sep)
        self.tmp = tempfile.mkdtemp(prefix="robust_")
        self.count = 0

    def run(self, name, data):
        self.count += 1
        path = os.path.join(self.tmp, "%s_%d.obs" % (name, self.count))
        with open(path, "wb") as f:
            f.write(data if isinstance(data, bytes) else data.encode("utf-8"))
        start = time.time()
        try:
            p = subprocess.run([self.obc, "-src", path, "-lib", "cipher,collect,xml,json", "-opt", "s3", "-dest", path + ".obe"],
                               capture_output=True, env=self.env, cwd=self.cwd, timeout=COMPILE_TIMEOUT)
            rc = p.returncode
            log = (p.stdout + p.stderr).decode("utf-8", errors="replace")
        except subprocess.TimeoutExpired:
            rc, log = "TIMEOUT", ""
        elapsed = time.time() - start
        errs = []
        for line in log.splitlines():
            m = re.match(r".*\.obs:\((\d+),(\d+)\): (.*)", line)
            if m and not m.group(3).startswith("Warning"):
                errs.append((int(m.group(1)), m.group(3)))
        return {"rc": rc, "log": log, "errs": errs, "secs": elapsed}


def classify(case, res):
    if res["rc"] == "TIMEOUT":
        return "HANG"
    if res["rc"] not in NORMAL_EXITS or "Unhandled" in res["log"] or "Assertion" in res["log"]:
        return "CRASH"
    if res["secs"] > SLOW_SECONDS:
        return "SLOW"
    if res["rc"] == 0:
        return "NOERR"
    if not res["errs"]:
        return "NOLINE"
    near = any(abs(l - case["line"]) <= 2 for l, _ in res["errs"]) or ("at line %d," % case["line"]) in res["log"]
    if not near and case["kind"] not in ("del_close_brace", "truncate", "del_open_brace"):
        return "FAR"
    if len(res["errs"]) > 4:
        return "CASCADE"
    return "OK"


def run_mutation(comp, reg_dir, nprog, seed):
    cases = mutation_cases(reg_dir, nprog, seed)
    print("Mutation: %d cases from %d programs (seed %d)" % (len(cases), nprog, seed))
    workers = max(1, min(4, (os.cpu_count() or 2)))

    def one(c):
        r = comp.run(c["prog"] + "_" + c["kind"], c["text"])
        c["cls"] = classify(c, r)
        c["res"] = r
        return c

    with ThreadPoolExecutor(max_workers=workers) as ex:
        results = list(ex.map(one, cases))

    tally = {}
    for c in results:
        tally[c["cls"]] = tally.get(c["cls"], 0) + 1
    total = len(results)
    print("  results: " + ", ".join("%s %d" % (k, tally[k]) for k in sorted(tally)))

    failures = []
    for c in results:
        if c["cls"] in ("CRASH", "HANG", "SLOW"):
            failures.append("%s: %s, mutation %s at line %d of %s (exit %s, %.1fs)\n      %s"
                            % (c["cls"], c["prog"], c["kind"], c["line"], c["prog"], c["res"]["rc"], c["res"]["secs"], c["src"][:100]))
    if total:
        def share(*names):
            return sum(tally.get(n, 0) for n in names) / total
        if share("OK") < 0.80:
            failures.append("accuracy: only %.0f%% of mutations report an error at the mutation (need 80%%)" % (100 * share("OK")))
        if share("CASCADE") > 0.05:
            failures.append("cascades: %.0f%% of mutations report more than four errors (limit 5%%)" % (100 * share("CASCADE")))
        if share("FAR", "NOLINE") > 0.05:
            failures.append("far: %.0f%% of mutations report only far from the mutation (limit 5%%)" % (100 * share("FAR", "NOLINE")))
        if share("NOERR") > 0.15:
            failures.append("silent: %.0f%% of mutations compile with no error (limit 15%%)" % (100 * share("NOERR")))
    else:
        failures.append("no mutation cases were built: is %s the regression directory?" % reg_dir)

    # what the worst of them looked like, to read in the log
    shown = 0
    for c in results:
        if c["cls"] in ("CASCADE", "FAR") and shown < 4:
            shown += 1
            print("  e.g. %s: %s, %s at line %d: %s" % (c["cls"], c["prog"], c["kind"], c["line"], c["src"][:80]))
            for line, msg in c["res"]["errs"][:3]:
                print("         (line %d) %s" % (line, msg[:100]))
    return failures


# -------------------------------------------------------------------- stress
def stress_cases():
    rnd = random.Random(3)
    C = []

    def add(name, data, must_error=False):
        C.append((name, data, must_error))

    add("empty", b"")
    add("whitespace_only", b" \t\r\n" * 50)
    add("bom_only", b"\xef\xbb\xbf")
    add("comment_only", b"# nothing here\n")
    add("nul_bytes", b"\x00" * 100, True)
    # a NUL used to end the source without a word on Windows (the rest was never read)
    add("nul_mid_file", prog("x := 1;").encode() + b"\x00 y := ;\n", True)
    add("nul_mid_statement", prog("x := 1;\n y\x00 := 2;").encode(), True)
    # 'ab<ZWSP>cd' reads as one name and scanned as two
    add("zero_width_in_name", prog("ab​cd := 1;"), True)
    add("trailing_dot_in_name", prog("y := 1;\n x := y.;"), True)
    add("invalid_utf8", prog("x := 1;").encode() + b"\xff\xfe\xc3\x28\xa0\xa1")
    add("invalid_utf8_mid", b"class T {\n function : Main(a : String[]) ~ Nil {\n x := 1;\n \xff\n y := 2;\n }\n}\n", True)
    add("lone_cr", prog("x := 1;").replace("\n", "\r"))
    add("crlf", prog("x := 1;").replace("\n", "\r\n"))
    for name, text in (("only_lbrace", "{"), ("only_rbrace", "}"), ("only_quote", '"'), ("only_squote", "'"),
                       ("open_block_comment", "/* never closed"), ("ends_in_class", "class"), ("ends_in_use", "use"),
                       ("eof_after_dot", prog("x := a.")), ("eof_after_arrow", prog("x := a->")),
                       ("eof_after_assign", prog("x :=")), ("eof_in_params", "class T { function : Main(a : "),
                       ("eof_in_generic", prog("x : Vector<")), ("eof_in_lambda", prog("f := \\(")),
                       ("eof_in_select", prog("select(x) {label 1:")), ("open_interp", prog('s := "a{$x')),
                       ("unicode_ident", prog("α := 1; \U0001F600 := 2;")), ("rtl_override", prog("x := 1; ‮ y := 2;"))):
        add(name, text, True)
    for k in range(4):
        add("random_bytes_%d" % k, bytes(rnd.randrange(256) for _ in range(rnd.choice([50, 500, 5000]))))
    chars = list("{}()[];,.:=+-*/<>\"'$@#\\ \nabcXYZ019")
    for k in range(4):
        add("random_tokens_%d" % k, "".join(rnd.choice(chars) for _ in range(rnd.choice([200, 2000, 20000]))))
    # depth: each of these used to overflow the stack
    for d in (400, 20000):
        add("nest_parens_%d" % d, prog("x := " + "(" * d + "1" + ")" * d + ";"))
        add("nest_parens_unclosed_%d" % d, prog("x := " + "(" * d + "1;"), True)
        add("nest_braces_%d" % d, prog("{" * d + "}" * d))
        add("nest_braces_unclosed_%d" % d, prog("{" * d), True)
        add("nest_if_%d" % d, prog("if(true) {" * d + "x := 1;" + "}" * d))
        add("nest_array_%d" % d, prog("x := " + "a[" * d + "0" + "]" * d + ";"), True)
        add("nest_calls_%d" % d, prog("x := " + "F(" * d + "1" + ")" * d + ";"), True)
        add("nest_generic_%d" % d, prog("x : " + "Vector<" * d + "Int" + ">" * d + ";"), True)
        add("nest_interp_%d" % d, prog("s := " + '"{$' * d + "x" + '}"' * d + ";"))
        add("nest_blockcomment_%d" % d, prog("/*" * d + "*/" * d + " x := 1;"))
        add("chain_dot_%d" % d, prog("x := a" + "->b" * d + ";"), True)
        add("lambda_nest_%d" % d, prog("f := " + "\\() ~ Nil => " * d + "x;"), True)
    add("nest_not_20000", prog("x := true " + "& true " * 20000 + ";"))
    add("chain_plus_20000", prog("x := 1" + " + 1" * 20000 + ";"), True)
    add("long_if_else_chain", prog("if(x = 1) {} " + "else if(x = 2) {} " * 20000), True)
    # size
    add("long_identifier", prog("%s := 1;" % ("a" * 200000)))
    add("long_string", prog('s := "%s";' % ("x" * 2000000)))
    add("long_string_unterminated", prog('s := "%s;' % ("x" * 2000000)), True)
    add("long_line_comment", prog("# %s\nx := 1;" % ("c" * 2000000)))
    add("huge_number", prog("x := %s;" % ("9" * 5000)), True)
    add("huge_float", prog("x := %s.%s;" % ("9" * 5000, "9" * 5000)), True)
    add("huge_exponent", prog("x := 1.0e%s;" % ("9" * 100)), True)
    add("many_errors", prog("x := ;\n" * 20000), True)
    add("many_undefined", prog("".join("y%d := zz%d;\n" % (i, i) for i in range(3000))), True)
    add("many_args", prog("F(" + ",".join(["1"] * 20000) + ");"), True)
    add("many_args_unclosed", prog("F(" + ",".join(["1"] * 20000) + ";"), True)
    add("many_unclosed_braces", "class A {\n" * 5000, True)
    add("many_stray_close", "}\n" * 20000, True)
    add("many_bad_chars", prog("@" * 20000), True)
    add("many_bad_strings", prog('s := "a;\n' * 2000), True)
    add("many_bad_numbers", prog("x := 1e3;\n" * 20000), True)
    add("many_char_lits", prog("c := 'ab';\n" * 20000), True)
    # the parent-class retry used to analyze a call's arguments again: 2^n for a missing method
    add("nested_undefined_calls_30", prog("x := " + "Missing(" * 30 + "1" + ")" * 30 + ";"), True)
    # size-scaling: each of these was quadratic (a dead store was looked up against every other
    # one by value, and a select label copied the label map): 54 s and 37 s, now 5 s and 2 s
    add("many_dead_stores_100000", prog("x := 1;\n" * 100000))
    add("many_select_labels_50000", prog("x := 1; select(x) {" + "".join("label %d: {}\n" % i for i in range(50000)) + "}"))
    # confusing but small
    for name, body in (("class_in_method", "class Inner {}"), ("function_in_method", "function : G() ~ Nil {}"),
                       ("use_inside_body", "use System.IO;")):
        add(name, prog(body), True)
    add("method_no_body", "class T { function : Main(a : String[]) ~ Nil }", True)
    add("method_no_rettype", "class T { function : Main(a : String[]) { } }", True)
    add("class_no_name", "class { }", True)
    add("param_no_type", "class T { function : Main(a) ~ Nil { } }", True)
    return C


def run_stress(comp):
    cases = stress_cases()
    print("Stress: %d hostile inputs" % len(cases))
    failures = []
    for name, data, must_error in cases:
        r = comp.run(name, data)
        rc = r["rc"]
        if rc == "TIMEOUT":
            failures.append("HANG: %s did not finish in %ds" % (name, COMPILE_TIMEOUT))
        elif rc not in NORMAL_EXITS or "Unhandled" in r["log"] or "Assertion" in r["log"]:
            failures.append("CRASH: %s ended with exit status %s (a normal compile ends 0-3)" % (name, rc))
        elif r["secs"] > SLOW_SECONDS:
            failures.append("SLOW: %s took %.1fs (limit %ds)" % (name, r["secs"], SLOW_SECONDS))
        elif must_error and rc == 0:
            failures.append("ACCEPTED: %s compiled with no error but is not a valid program" % name)
    print("  %d failures" % len(failures))
    return failures


def main():
    sys.stdout.reconfigure(errors="backslashreplace")
    args = sys.argv[1:]
    if not args or args[0].startswith("--"):
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(args[0])
    nprog, seed = 30, 1
    for i, a in enumerate(args):
        if a == "--programs":
            nprog = int(args[i + 1])
        elif a == "--seed":
            seed = int(args[i + 1])
    if not os.path.exists(os.path.join(bin_dir, "obc" + EXE)):
        print("ERROR: missing obc in %s" % bin_dir)
        return 2

    print("Objeck compiler robustness (mutation and stress)")
    print("  bin: %s\n" % bin_dir)
    comp = Compiler(bin_dir)
    failures = run_mutation(comp, SCRIPT_DIR, nprog, seed) + run_stress(comp)
    print()
    if failures:
        print("FAILED (%d):" % len(failures))
        for f in failures:
            print("  - " + f)
        print("\nRerun one locally: python run_compiler_robustness.py <bin_dir> --seed %d --programs %d" % (seed, nprog))
        return 1
    print("PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
