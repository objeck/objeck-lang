#!/usr/bin/env python3
"""
Runs, locally and in parallel, the checks ci-build.yml's "Tools" job runs that need
no build: seconds here instead of a cloud round trip.

    python tools/cicd/local_ci.py            run every check
    python tools/cicd/local_ci.py --list     name them
    python tools/cicd/local_ci.py doc        only the checks whose name contains "doc"

Exit status is 0 only when every selected check passes. A check that cannot run (its
script is missing) FAILS rather than being skipped: a gate that quietly stops running is
how the cloud jobs went green without checking anything, so this one cannot do that.

This list mirrors ci-build.yml; when a step is added to that job, add it here (the
check_local_ci_list test in this file's --self-test fails when a script the workflow
runs is missing from CHECKS).

What it does not cover: anything that needs the built toolchain (the LSP suite, the
regression suite, check_docs_deps.py --artifacts). tools/cicd/local_linux.sh builds the
committed tree in a clean copy for those.
"""
import concurrent.futures
import glob
import os
import re
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PY = sys.executable


def py(*args):
    return [PY] + list(args)


def lib_sources():
    return sorted(glob.glob(os.path.join(ROOT, "core", "compiler", "lib_src", "*.obs")))


# (name, argv builder). Names follow the step names in ci-build.yml.
CHECKS = [
    ("Library lists agree with lib_src", lambda: py("tools/cicd/check_doc_lists.py")),
    ("Doc comments end with their code fence", lambda: py("tools/cicd/check_doc_fences.py")),
    ("Regression tests can report failure", lambda: py("tools/cicd/check_test_exit_codes.py")),
    ("JIT opt-outs are directives with a stated reason", lambda: py("tools/cicd/check_jit_optouts.py")),
    ("Native libraries barrier the objects they receive", lambda: py("tools/cicd/check_native_write_barrier.py")),
    ("The native write-barrier scan can still detect an unbarriered store",
     lambda: py("tools/cicd/test_check_native_write_barrier.py")),
    ("The API index check can still detect a stale index", lambda: py("tools/cicd/test_check_api_index.py")),
    ("Differential and verifier markers have a stated reason", lambda: py("tools/cicd/check_diff_markers.py")),
    ("Differential runner catches divergence", lambda: py("programs/regression/test_run_differential.py")),
    ("Shipped examples are all indexed", lambda: py("tools/cicd/check_examples_index.py")),
    ("Generated files match their .in templates", lambda: py("tools/cicd/check_version_templates.py")),
    ("Committed libraries carry the toolchain version", lambda: py("tools/cicd/check_obl_versions.py")),
    ("Library version check catches a stale library", lambda: py("tools/cicd/check_obl_versions.py", "--test")),
    ("SHA256SUMS re-sign check can detect a missing re-sign", lambda: py("tools/cicd/check_manifest_resign.py", "--test")),
    ("Every SHA256SUMS upload is re-signed", lambda: py("tools/cicd/check_manifest_resign.py")),
    ("Docs name only the dependencies Objeck needs", lambda: py("tools/cicd/check_docs_deps.py", "--docs")),
    ("Benchmark set and inputs match the documented ones", lambda: py("tools/cicd/check_benchmark_inputs.py")),
    ("Benchmark set/input check catches drift", lambda: py("tools/cicd/test_check_benchmark_inputs.py")),
    ("Native call names are held, not re-allocated",
     lambda: py("tools/cicd/hoist_native_names.py", "--check", *lib_sources())),
    ("Nightly workflow contract", lambda: py("tools/cicd/test_nightly_contract.py")),
]


def run_one(item):
    name, build = item
    argv = build()
    script = next((a for a in argv[1:] if a.endswith(".py")), None)
    if script and not os.path.exists(os.path.join(ROOT, script)):
        return name, False, 0.0, "missing script: " + script
    start = time.time()
    proc = subprocess.run(argv, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          universal_newlines=True, errors="replace")
    return name, proc.returncode == 0, time.time() - start, proc.stdout


def self_test():
    """Every check_*/test_* script ci-build.yml runs from tools/cicd must be listed."""
    wf = open(os.path.join(ROOT, ".github", "workflows", "ci-build.yml"), encoding="utf-8").read()
    listed = " ".join(" ".join(b()) for _, b in CHECKS).replace("\\", "/")
    # scripts that need a built toolchain or network are deliberately not here
    needs_build = {"check_api_index.py", "check_openai_image_dispatch.py", "mock_inference_server.py",
                   "test_inference_round_trip.py", "run_lsp_tests.py"}
    missing = []
    for script in sorted(set(re.findall(r"python3? (tools/cicd/(?:check|test)_\w+\.py)", wf))):
        if os.path.basename(script) in needs_build:
            continue
        if script not in listed:
            missing.append(script)
    if missing:
        print("ci-build.yml runs scripts that local_ci.py does not:")
        for m in missing:
            print("  " + m)
        return 1
    print("local_ci.py lists every build-free check ci-build.yml runs")
    return 0


def main(argv):
    if "--self-test" in argv:
        return self_test()
    if "--list" in argv:
        for name, _ in CHECKS:
            print(name)
        return 0
    words = [a.lower() for a in argv if not a.startswith("-")]
    selected = [c for c in CHECKS if not words or any(w in c[0].lower() for w in words)]
    if not selected:
        print("no check matches " + " ".join(words))
        return 2

    start = time.time()
    failed = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(8, len(selected))) as pool:
        for name, ok, secs, out in pool.map(run_one, selected):
            print("%s %-72s %5.1fs" % ("PASS" if ok else "FAIL", name, secs))
            if not ok:
                failed.append((name, out))
    for name, out in failed:
        print("\n--- %s ---" % name)
        print("\n".join(out.rstrip().splitlines()[-25:]))
    print("\n%d of %d checks passed in %.1fs" % (len(selected) - len(failed), len(selected), time.time() - start))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
