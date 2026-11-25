#!/usr/bin/env python3
"""Run the llm_eval tests: each file in tests/ is compiled together with llm_eval.obs and run.

llm_eval is a research harness, not a shipped library (no llm_eval.obl), so its tests are
compiled from source, as tools/llm_eval/eval.obs is. A test passes when it compiles and its
program exits 0 (the tests exit non-zero on a failed check).

    python3 tools/llm_eval/run_tests.py <deploy bin dir> [test name ...]

The bin dir is the one holding obc and obr (core/release/deploy*/bin). The tests start obc
and obr themselves, found beside the running VM.
"""
import os
import subprocess
import sys

LIBS = "cipher,collect,xml,json,gen_collect,csv,ml"


def exe(bin_dir, name):
    path = os.path.join(bin_dir, name + (".exe" if os.name == "nt" else ""))
    if not os.path.exists(path):
        sys.exit("run_tests.py: %s not found" % path)
    return path


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    bin_dir = os.path.abspath(argv[1])
    wanted = set(argv[2:])
    here = os.path.dirname(os.path.abspath(__file__))
    tests_dir = os.path.join(here, "tests")
    library = os.path.join(here, "llm_eval.obs")
    obc, obr = exe(bin_dir, "obc"), exe(bin_dir, "obr")
    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = os.path.abspath(os.path.join(bin_dir, "..", "lib"))
    out_dir = os.path.join(tests_dir, "_out")
    os.makedirs(out_dir, exist_ok=True)

    names = sorted(f[:-4] for f in os.listdir(tests_dir) if f.endswith(".obs"))
    if wanted:
        names = [n for n in names if n in wanted]
    failed = []
    for name in names:
        exe_path = os.path.join(out_dir, name + ".obe")
        comp = subprocess.run(
            [obc, "-src", os.path.join(tests_dir, name + ".obs") + "," + library,
             "-lib", LIBS, "-opt", "s3", "-dest", exe_path],
            cwd=bin_dir, env=env, capture_output=True, text=True)
        if comp.returncode != 0:
            print("FAIL %s: compile\n%s%s" % (name, comp.stdout, comp.stderr))
            failed.append(name)
            continue
        try:
            run = subprocess.run([obr, exe_path], cwd=tests_dir, env=env,
                                 capture_output=True, text=True, timeout=600)
        except subprocess.TimeoutExpired:
            print("FAIL %s: timed out" % name)
            failed.append(name)
            continue
        if run.returncode != 0:
            print("FAIL %s: exit %d\n%s%s" % (name, run.returncode, run.stdout[-2000:], run.stderr[-500:]))
            failed.append(name)
        else:
            print("PASS %s" % name)
    print("%d of %d passed" % (len(names) - len(failed), len(names)))
    return 1 if failed or not names else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
