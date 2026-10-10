#!/usr/bin/env python3
"""Does an execution provider change the answer? Run on a machine that has one.

WHY THIS IS NOT IN CI
---------------------
It needs a GPU. No runner here has one, so a CI step would skip on every run --
and a skip that silently becomes permanent is how "CI-tested" stops meaning
"tested" (this repo has been bitten: obu was CI-tested and packaged in no
archive until v2026.8.2). So this is a deliberate local check, run on a machine
with a GPU provider, and `docs/INFERENCE_ENGINES_PLAN.md` says so rather than
implying coverage that does not exist.

If it finds no second provider it says SKIP and exits 0 -- it is not a failure to
lack a GPU -- but it says it loudly, on one line, so a run that proved nothing
cannot be mistaken for a run that passed.

WHAT IT ESTABLISHES
-------------------
The same model file and the same deterministic input, run under two providers,
agree on the top-1 class and differ by no more than float reassociation.

Bit-identical output is NOT the property demanded: GPU and CPU kernels
accumulate in different orders. Measured on resnet34 with DirectML, the largest
difference across 1000 logits was 4e-6, while the same provider against itself
was exactly 0 -- which is also what proves `ep=dml` selects a different kernel
instead of silently falling back to CPU, and that each provider is internally
deterministic.

Usage: check_onnx_provider_parity.py <bin-dir> [--model <path>] [--eps cpu,dml]
"""

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

DEFAULT_MODEL = os.path.join(REPO, "programs", "frameworks", "opencv_onnx",
                             "data", "models", "resnet34.onnx")
DRIVER = os.path.join(REPO, "programs", "tests", "onnx_provider_parity.obe")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bin_dir")
    ap.add_argument("--model", default=DEFAULT_MODEL)
    ap.add_argument("--eps", default="cpu,dml",
                    help="two provider selectors to compare, e.g. cpu,dml or cpu,cuda")
    args = ap.parse_args()

    bin_dir = os.path.abspath(args.bin_dir)
    exe = ".exe" if os.name == "nt" else ""
    obr = os.path.join(bin_dir, "obr" + exe)

    for needed in (obr, DRIVER, args.model):
        if not os.path.exists(needed):
            if needed is DRIVER:
                print("SKIP: the driver is not built. Build it with:")
                print("  obc -src programs/tests/onnx_provider_parity.obs "
                      "-lib onnx,opencv,models,json,gen_collect "
                      "-dest programs/tests/onnx_provider_parity.obe")
                return 0
            print("SKIP: missing %s" % needed)
            return 0

    eps = [e.strip() for e in args.eps.split(",") if e.strip()]
    if len(eps) != 2:
        print("Error: --eps takes exactly two selectors, e.g. cpu,dml")
        return 2

    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = os.path.abspath(os.path.join(bin_dir, "..", "lib"))

    # Ask the runtime what it has before asking it to compare. A build with only
    # CPUExecutionProvider would compare CPU against a CPU fallback, print a
    # difference of exactly 0, and look like a pass.
    probe = subprocess.run([obr, DRIVER, args.model, eps[0], eps[0]],
                           capture_output=True, text=True, env=env, timeout=900)
    listed = []
    for line in (probe.stdout or "").splitlines():
        line = line.strip()
        if line.endswith("ExecutionProvider"):
            listed.append(line)
    print("providers on this machine: %s" % (", ".join(listed) or "none reported"))

    accelerated = [p for p in listed if not p.startswith("CPU")]
    if not accelerated:
        print("SKIP: no accelerated provider is present, so there is nothing to "
              "compare against CPU. This machine cannot establish provider parity.")
        return 0

    print("comparing %s against %s on %s\n" % (eps[0], eps[1], os.path.basename(args.model)))
    proc = subprocess.run([obr, DRIVER, args.model, eps[0], eps[1]],
                          capture_output=True, text=True, env=env, timeout=900)
    sys.stdout.write(proc.stdout or "")
    if proc.stderr.strip():
        sys.stderr.write(proc.stderr)

    # Same provider against itself must be bit-identical. Without this, a
    # cross-provider difference cannot be told from run-to-run noise.
    same = subprocess.run([obr, DRIVER, args.model, eps[1], eps[1]],
                          capture_output=True, text=True, env=env, timeout=900)
    identical = any("largest absolute difference: 0.000000" in line
                    for line in (same.stdout or "").splitlines())
    print("\n%s %s against itself is bit-identical"
          % ("[ok]  " if identical else "[FAIL]", eps[1]))

    ok = proc.returncode == 0 and identical
    print("\n%s" % ("PASS: the providers agree" if ok
                    else "FAIL: see above (exit %d)" % proc.returncode))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
