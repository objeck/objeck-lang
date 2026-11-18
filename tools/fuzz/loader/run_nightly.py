#!/usr/bin/env python3
"""Nightly run of the loader's libFuzzer targets (see README.md).

    python3 tools/fuzz/loader/run_nightly.py --bin <deploy bin dir> --work <dir> [--minutes 30]

Builds loader_fuzz and loader_file_fuzz (build.sh, clang + sanitizers), seeds a corpus from
the regression programs compiled by the deploy tree's obc (make_corpus.py), fuzzes for a
fixed time, replays everything libFuzzer saved, and prints one line per distinct finding in
the format tools/cicd/nightly_triage.py reads:

    FAIL loader-fuzz: <kind> <function>
      seed: <seed>
      reduced: <path of the saved input>

Exit status 1 when anything was found. Three passes share the time: the decompressed-image
target on small seeds (where mutation pays), the .obe-file target, and a short single-process
pass with leak detection on (fork mode runs with it off to stay fast).

A finding is named by what went wrong and the first project frame of its stack, never by an
address, a line number or the input's hash, so the same bug found on another night is the same
signature. Every finding is fixed afterwards with a case in
programs/regression/run_vm_flag_tests.py (section 30), like the ones in the README.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ARTIFACT_KINDS = ("crash", "oom", "timeout", "leak")

# What a sanitizer or libFuzzer says went wrong, most specific first.
KIND_PATTERNS = [
    (re.compile(r"ERROR: AddressSanitizer: ([\w\-]+)"), lambda m: m.group(1)),
    (re.compile(r"ERROR: LeakSanitizer"), lambda m: "memory-leak"),
    (re.compile(r"runtime error: ([a-z][a-z \-]*[a-z])"), lambda m: "ubsan " + m.group(1).strip()),
    (re.compile(r"ERROR: libFuzzer: out-of-memory"), lambda m: "out-of-memory"),
    (re.compile(r"ERROR: libFuzzer: timeout"), lambda m: "timeout"),
    (re.compile(r"ERROR: libFuzzer: deadly signal"), lambda m: "deadly-signal"),
]
# "#3 0x5581 in Loader::LoadMethods(StackClass*) /work/core/vm/loader.cpp:612:5"
FRAME_RE = re.compile(r"#\d+ 0x[0-9a-f]+ in (.+?) (?:\S*?/)?(?:core/vm|tools/fuzz)/\S+")
# "core/vm/loader.cpp:612:5: runtime error: ..." (UBSan names its own location)
UBSAN_WHERE_RE = re.compile(r"(?:core/vm|tools/fuzz)/(\S+?\.cpp):\d+:\d+: runtime error")


def describe(text, artifact_name):
    """(kind, where) for a replayed artifact: stable across nights, no addresses or lines."""
    kind = ""
    for pattern, name in KIND_PATTERNS:
        m = pattern.search(text)
        if m:
            kind = name(m)
            break
    if not kind:
        kind = artifact_name.split("-", 1)[0] or "unknown"
    where = ""
    m = FRAME_RE.search(text)
    if m:
        where = re.sub(r"\(.*$", "", m.group(1)).strip()
    else:
        m = UBSAN_WHERE_RE.search(text)
        if m:
            where = m.group(1)
    return kind, where or "(no project frame)"


def seed_number(text):
    """libFuzzer wants an integer; a dispatch input may be any text."""
    text = str(text).strip()
    return int(text) if text.isdigit() else zlib.crc32(text.encode("utf-8"))


def find_artifacts(art_dir):
    """Saved inputs worth replaying: crash-, oom-, timeout- and leak- files, not slow-unit-."""
    found = []
    for kind in ARTIFACT_KINDS:
        found += sorted(glob.glob(os.path.join(art_dir, kind + "-*")))
    return found


def replay(binary, path, timeout=60):
    """Runs the target once on a saved input; returns its combined output."""
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:symbolize=1")
    try:
        p = subprocess.run([binary, path], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout, env=env)
        return p.stdout.decode("utf-8", "replace")
    except subprocess.TimeoutExpired as e:
        return (e.stdout or b"").decode("utf-8", "replace") + "\nERROR: libFuzzer: timeout (replay)\n"


def run_pass(binary, corpus, seeds, art, seconds, seed, jobs, leaks, log_path):
    """One libFuzzer run. Fork mode with leaks off, or a single process with leaks on."""
    os.makedirs(corpus, exist_ok=True)
    os.makedirs(art, exist_ok=True)
    cmd = [binary, "-timeout=10", "-rss_limit_mb=3072", "-use_value_profile=1", "-max_len=65536",
           "-max_total_time=%d" % seconds, "-seed=%d" % seed, "-artifact_prefix=%s/" % art]
    if jobs > 1 and not leaks:
        cmd += ["-fork=%d" % jobs, "-ignore_crashes=1", "-ignore_ooms=1", "-ignore_timeouts=1"]
    cmd += [corpus, seeds]
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=%d:abort_on_error=0" % (1 if leaks else 0))
    with open(log_path, "w", encoding="utf-8") as log:
        code = subprocess.call(cmd, stdout=log, stderr=subprocess.STDOUT, env=env)
    return code


def last_stats(log_path):
    """The last progress line libFuzzer printed (coverage, corpus size, speed)."""
    last = ""
    try:
        with open(log_path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if re.match(r"#\d+[:\s]", line) or "stat::" in line:
                    last = line.strip()
    except OSError:
        pass
    return last


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--bin", required=True, help="a built deploy tree's bin directory (its obc compiles the seeds)")
    ap.add_argument("--work", required=True, help="scratch directory: build, seeds, corpora, saved inputs")
    ap.add_argument("--minutes", type=int, default=30, help="total fuzzing time, all passes (default 30)")
    ap.add_argument("--seed", default=time.strftime("%Y%m%d", time.gmtime()),
                    help="libFuzzer seed: a number, or any text (default: the UTC date, YYYYMMDD)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2, help="fork-mode workers (default: all cores)")
    ap.add_argument("--out", default="", help="copy each distinct finding's input here, for the uploaded artifact")
    ap.add_argument("--build-dir", default="", help="use targets already built here instead of running build.sh")
    args = ap.parse_args(argv)
    args.seed = seed_number(args.seed)

    work = os.path.abspath(args.work)
    os.makedirs(work, exist_ok=True)
    build = os.path.abspath(args.build_dir) if args.build_dir else os.path.join(work, "build")
    seeds = os.path.join(work, "seeds")

    if not args.build_dir:
        env = dict(os.environ, CXX=os.environ.get("CXX", "clang++"), JOBS=str(args.jobs))
        print("== building the fuzz targets into %s" % build, flush=True)
        if subprocess.call(["bash", os.path.join(HERE, "build.sh"), build], env=env) != 0:
            print("FAIL loader-fuzz: (tool) build.sh failed")
            return 1
    targets = {"image": os.path.join(build, "loader_fuzz"), "file": os.path.join(build, "loader_file_fuzz")}
    for name, path in targets.items():
        if not os.path.exists(path):
            print("FAIL loader-fuzz: (tool) missing target %s" % path)
            return 1

    if not os.path.isdir(os.path.join(seeds, "slices")):
        print("== seeding from the regression programs", flush=True)
        if subprocess.call([sys.executable, os.path.join(HERE, "make_corpus.py"), args.bin, seeds,
                            "-j", str(args.jobs)]) != 0:
            print("FAIL loader-fuzz: (tool) make_corpus.py failed")
            return 1

    total = max(args.minutes, 3) * 60
    plan = [("image", "slices", int(total * 0.65), False),
            ("file", "files", int(total * 0.25), False),
            ("leak", "slices", int(total * 0.10), True)]
    runs = []
    for label, seed_dir, seconds, leaks in plan:
        target = targets["file" if label == "file" else "image"]
        art = os.path.join(work, "art", label)
        corpus = os.path.join(work, "corpus", label)
        log = os.path.join(work, "fuzz-%s.log" % label)
        print("== %s pass: %ds, %s" % (label, seconds, "1 process, leaks on" if leaks else "%d workers" % args.jobs),
              flush=True)
        run_pass(target, corpus, os.path.join(seeds, seed_dir), art, seconds, args.seed, args.jobs, leaks, log)
        print("   %s" % (last_stats(log) or "(no progress line)"), flush=True)
        runs.append((label, target, art))

    findings = {}
    for label, target, art in runs:
        for path in find_artifacts(art):
            kind, where = describe(replay(target, path), os.path.basename(path))
            key = (kind, where)
            if key not in findings:
                findings[key] = (label, path)

    if not findings:
        print("loader fuzz: no findings in %d minutes (seed %d)" % (args.minutes, args.seed))
        return 0
    if args.out:
        os.makedirs(args.out, exist_ok=True)
    for (kind, where), (label, path) in sorted(findings.items()):
        saved = path
        if args.out:
            saved = os.path.join(args.out, "%s-%s" % (label, os.path.basename(path)))
            shutil.copy(path, saved)
        print("FAIL loader-fuzz: %s %s" % (kind, where))
        print("  seed: %d" % args.seed)
        print("  target: %s" % label)
        print("  reduced: %s" % saved)
    return 1


if __name__ == "__main__":
    sys.exit(main())
