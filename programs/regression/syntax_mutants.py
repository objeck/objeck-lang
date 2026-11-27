#!/usr/bin/env python3
"""
Mutation smoke for new syntax (docs/design/AGENT_SYNTAX.md): the compiler must answer every damaged program,
never crash and never hang.

A valid source that uses a new construct is damaged at random (characters removed, inserted, repeated; lines
swapped; the text cut short; one significant word changed for another) and compiled. The compiler may accept or
refuse a mutant; what it may not do is end by a signal or an access violation, or run past the time limit. The
random numbers come from a seed, so a run is repeatable and a finding names the seed and the mutant's number.

Used by prompt_literal_diag_test.py and agent_decl_diag_test.py with a fixed seed and a small count, and by hand
for a longer run:

    python syntax_mutants.py <bin_dir> <source.obs>[,<source.obs>...] <libs> [count] [seed]
"""
import concurrent.futures
import os
import random
import subprocess
import sys

EXE = ".exe" if os.name == "nt" else ""
NOISE = ['{', '}', '{{', '}}', '"', "'", ':', ';', ',', '(', ')', '[', ']', '<', '>', '$', '{$', '@', '\\', '\n', '\t', ' ', '~', '->', ':=', '#', '#~', '~#', '.', '?']
WORDS = ['prompt', 'agent', 'backend', 'instructions', 'tools', 'mcp', 'allow', 'deny', 'budget', 'trace', 'steps', 'tokens', 'cost',
         'class', 'function', 'method', 'return', 'String', 'Int', 'Float', 'Nil', 'use', 'bundle', 'if', 'each', 'New']
INSERTS = ['prompt "', 'prompt "{', 'prompt "{x : ', '{name}', '{name : Int}', 'agent A {', 'agent A(', 'budget {', 'backend : ', 'tools : A, ', ' : Int}', '";']


def mutate(text, rng):
    """One to three random edits of the text."""
    for _ in range(rng.randint(1, 3)):
        if not text:
            return text
        kind = rng.randrange(8)
        at = rng.randrange(len(text))
        if kind == 0:      # remove a short span
            text = text[:at] + text[at + rng.randint(1, 12):]
        elif kind == 1:    # insert noise
            text = text[:at] + rng.choice(NOISE) + text[at:]
        elif kind == 2:    # repeat a span
            span = text[at:at + rng.randint(1, 40)]
            text = text[:at] + span + text[at:]
        elif kind == 3:    # cut the text short
            text = text[:at]
        elif kind == 4:    # swap two lines
            lines = text.split('\n')
            if len(lines) > 2:
                i, j = rng.randrange(len(lines)), rng.randrange(len(lines))
                lines[i], lines[j] = lines[j], lines[i]
                text = '\n'.join(lines)
        elif kind == 5:    # change one significant word for another
            present = [w for w in WORDS if w in text]
            if present:
                word = rng.choice(present)
                spots = [i for i in range(len(text)) if text.startswith(word, i)]
                spot = rng.choice(spots)
                text = text[:spot] + rng.choice(WORDS) + text[spot + len(word):]
        elif kind == 6:    # insert the start of a new construct
            text = text[:at] + rng.choice(INSERTS) + text[at:]
        else:              # remove a whole line
            lines = text.split('\n')
            del lines[rng.randrange(len(lines))]
            text = '\n'.join(lines)
    return text


def smoke(bin_dir, sources, libs, work, count, seed, timeout=60, jobs=8):
    """Compiles `count` mutants of the sources (texts). Returns (accepted, refused, findings); a finding is
    (number, what, path of the saved mutant)."""
    obc = os.path.join(bin_dir, "obc" + EXE)
    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = os.path.join(os.path.dirname(os.path.abspath(bin_dir)), "lib")
    rng = random.Random(seed)
    mutants = [mutate(sources[n % len(sources)], rng) for n in range(count)]

    def one(n):
        src = os.path.join(work, "mutant_%d_%d.obs" % (seed, n))
        with open(src, "wb") as f:
            f.write(mutants[n].encode("utf-8", "replace"))
        obe = src[:-4] + ".obe"
        cmd = [obc, "-src", src, "-dest", obe]
        if libs:
            cmd += ["-lib", libs]
        try:
            p = subprocess.run(cmd, cwd=work, env=env, capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            return n, "no answer in %d s" % timeout, src
        # obc answers 0 (compiled) or a small status (refused); anything else is a crash
        if 0 <= p.returncode <= 9:
            for path in (src, obe):
                if os.path.exists(path):
                    os.remove(path)
            return n, "accepted" if p.returncode == 0 else "refused", None
        return n, "ended with status %d" % p.returncode, src

    accepted = refused = 0
    findings = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for n, what, saved in pool.map(one, range(count)):
            if what == "accepted":
                accepted += 1
            elif what == "refused":
                refused += 1
            else:
                findings.append((n, what, saved))
    return accepted, refused, findings


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    import tempfile
    bin_dir, paths, libs = sys.argv[1], sys.argv[2].split(","), sys.argv[3]
    count = int(sys.argv[4]) if len(sys.argv) > 4 else 500
    seed = int(sys.argv[5]) if len(sys.argv) > 5 else 1
    sources = [open(p, encoding="utf-8").read() for p in paths]
    work = tempfile.mkdtemp(prefix="syntax-mutants-")
    accepted, refused, findings = smoke(bin_dir, sources, libs, work, count, seed)
    print("%d mutants (seed %d): %d accepted, %d refused, %d findings" % (count, seed, accepted, refused, len(findings)))
    for n, what, saved in findings:
        print("  FINDING: mutant %d %s; kept as %s" % (n, what, saved))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
