#!/usr/bin/env python3
"""
Objeck v2026.10.0 hardening -- cross-platform measurement driver.

Three modes:

  baseline  plan Phase 0.3 perf baselines + the DeadStore perf check.
            master vs v2026.9.4, same boot, interleaved ABAB, warm-ups
            discarded. Answers "is any benchmark >5% slower, p<0.01".
  nursery   G3 nursery sweep (#841): --nursery=16m/32m/64m/128m.
  stress    a #861 data point. The race only opens when the fixture's
            threads outnumber the CPUs, so this mode pins the fixture to
            two CPUs with taskset and REFUSES to run on a host where it
            cannot -- see mode_stress.

Why Python and not perf-results/run_benchmarks.sh: that script times with
`/usr/bin/time -f "%e %M"`, which is GNU syntax. macOS ships the BSD time,
which has no -f, and Windows has no /usr/bin/time at all. This driver picks
the right peak-RSS probe per platform instead:

  macOS    BSD `/usr/bin/time -l`, "maximum resident set size" in BYTES
  Linux    GNU `/usr/bin/time -f ...%M`, peak RSS in KILOBYTES (x1024 here)
  Windows  GetProcessMemoryInfo(PeakWorkingSetSize) via ctypes against the
           child's Win32 handle, read after wait()

and falls back to the VM's own `[gc-stats] ... peak_rss_bytes=N` counter
(OBJECK_GC_STATS=1) when the OS probe is unavailable. That counter only
exists from commit 0a5e7ba008 on, so the v2026.9.4 arm of a comparison has
no VM number -- which is exactly why the OS probe is the primary source.
Every row records peak_rss_source, so an OS-measured peak is never silently
compared against a VM-reported one. The VM's number is kept alongside in
peak_rss_vm, together with the GC counters from the same line.

Nothing here writes to the repo. Results go to --out.

Usage:
  ./objeck_bench.py baseline --master DIR --base DIR --repo DIR --out DIR
  ./objeck_bench.py nursery  --master DIR --repo DIR --out DIR
  ./objeck_bench.py stress   --master DIR --repo DIR --out DIR

DIR for --master/--base is a deploy tree (the one holding bin/obc, bin/obr).
"""

import argparse
import csv
import ctypes
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

# Inputs copied from perf-results/run_benchmarks.sh so the numbers stay
# comparable with every baseline taken on the x64 box.
CLBG = {
    "nbody": "50000000",
    "binarytrees": "17",
    "spectralnorm": "5500",
    "fannkuchredux": "12",
    "fasta": "25000000",
    "mandelbrot": "4000",
}
PERF = {
    "bench_loop_invariant": "",
    "bench_cse": "",
    "bench_strength_ext": "",
    "bench_gc_churn": "",
    "bench_gc_large_heap": "18",
    "bench_array_intensive": "",
    "bench_matrix_multiply": "500",
    "bench_method_dispatch": "",
    "bench_copy_prop": "",
    "bench_dead_code": "",
    "bench_tco": "",
    "bench_spectralnorm_native": "",
}
# Allocation-heavy picks for the nursery sweep.
NURSERY_BENCHES = ["binarytrees", "bench_gc_churn", "bench_gc_large_heap"]
NURSERY_SIZES = ["16m", "32m", "64m", "128m"]

# Adaptive run count. Short benchmarks need more samples than long ones: at n=15
# bench_matrix_multiply (0.2 s) read +8.95% (p=0.042), and at n=60 the same pair
# read -1.58% (p=0.22) -- the instability was in the sampling, not in either build.
# Runs therefore scale to ~TARGET_SECONDS of wall time per build.
TARGET_SECONDS = 10.0
MIN_RUNS = 15
MAX_RUNS = 101

# #861 only reproduces with the fixture's threads confined to fewer CPUs than
# it has threads; two is what the 8/3600 Linux measurement used.
STRESS_CPUS = "0,1"
STRESS_MIN_HOST_CPUS = 3

EXE = ".exe" if os.name == "nt" else ""
SYS_TIME = "/usr/bin/time"
GC_STATS_RE = re.compile(r"\[gc-stats\]\s+(.*)")
# BSD time -l, bytes.
BSD_RSS_RE = re.compile(r"^\s*(\d+)\s+maximum resident set size", re.M)
# GNU time -f, our own marker so nothing else on stderr can match it. KILOBYTES.
GNU_RSS_FMT = "objeck_bench_peak_rss_kb=%M"
GNU_RSS_RE = re.compile(r"objeck_bench_peak_rss_kb=(\d+)")


def gc_stats_fields(stderr_text):
    """Parse the VM's single [gc-stats] line into {name: int}."""
    out = {}
    m = GC_STATS_RE.search(stderr_text or "")
    if not m:
        return out
    for tok in m.group(1).split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            try:
                out[k] = int(v)
            except ValueError:
                pass
    return out


def tool(deploy, name):
    """Path to obc/obr inside a deploy tree, with the Windows suffix."""
    return Path(deploy) / "bin" / (name + EXE)


def src_path(repo, name):
    for sub in ("programs/tests/clbg", "programs/tests/perf", "programs/regression"):
        p = Path(repo) / sub / (name + ".obs")
        if p.is_file():
            return p
    raise FileNotFoundError("no source for %s under %s" % (name, repo))


def compile_bench(deploy, repo, name, out_dir, tag):
    """Compile with THIS build's own obc; each build gets its own .obe."""
    src = src_path(repo, name)
    obe = Path(out_dir) / "obe" / tag / (name + ".obe")
    obe.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, OBJECK_LIB_PATH=str(Path(deploy) / "lib"))
    r = subprocess.run(
        [str(tool(deploy, "obc")), "-src", str(src), "-dest", str(obe), "-opt", "s3"],
        env=env, capture_output=True, text=True,
    )
    if r.returncode != 0:
        raise RuntimeError("compile failed for %s (%s):\n%s\n%s"
                           % (name, tag, r.stdout[-2000:], r.stderr[-2000:]))
    return obe


# ---- peak RSS --------------------------------------------------------------

_RSS_PROBE = None
_WIN_RSS = {}


class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
    """PSAPI's counter block; only PeakWorkingSetSize is read here."""
    _fields_ = [("cb", ctypes.c_uint32),
                ("PageFaultCount", ctypes.c_uint32),
                ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t)]


def win_rss_fn():
    """GetProcessMemoryInfo, or None when it cannot be bound."""
    if "fn" not in _WIN_RSS:
        _WIN_RSS["fn"] = None
        # K32GetProcessMemoryInfo is the kernel32 forwarder present since Win7;
        # psapi.dll carries the classic name. Either will do.
        for dll, name in (("kernel32", "K32GetProcessMemoryInfo"),
                          ("psapi", "GetProcessMemoryInfo")):
            try:
                fn = getattr(ctypes.WinDLL(dll, use_last_error=True), name)
            except (AttributeError, OSError):
                continue
            fn.argtypes = [ctypes.c_void_p,
                           ctypes.POINTER(PROCESS_MEMORY_COUNTERS),
                           ctypes.c_uint32]
            fn.restype = ctypes.c_int
            _WIN_RSS["fn"] = fn
            break
    return _WIN_RSS["fn"]


def win_peak_rss(handle):
    """Peak working set in bytes of a child, read through its Win32 handle.

    subprocess.Popen keeps that handle open until the Popen object is collected,
    and PSAPI still answers for a process that has already exited, so this is
    called after wait() and still sees the child's final peak.
    """
    fn = win_rss_fn()
    if fn is None or handle is None:
        return None
    counters = PROCESS_MEMORY_COUNTERS()
    counters.cb = ctypes.sizeof(counters)
    if not fn(ctypes.c_void_p(int(handle)), ctypes.byref(counters), counters.cb):
        return None
    return int(counters.PeakWorkingSetSize)


def detect_rss_probe():
    """Pick this host's peak-RSS source once: time_l / gnu_time / win_psapi / vm."""
    if sys.platform == "darwin":
        return "time_l" if Path(SYS_TIME).is_file() else "vm"
    if sys.platform.startswith("linux"):
        # The shell builtin `time` is a different program with no -f, so call
        # /usr/bin/time by absolute path and prove it understands the format.
        if Path(SYS_TIME).is_file():
            try:
                r = subprocess.run([SYS_TIME, "-f", GNU_RSS_FMT, sys.executable, "-c", ""],
                                   capture_output=True, text=True, timeout=60)
                if r.returncode == 0 and GNU_RSS_RE.search(r.stderr or ""):
                    return "gnu_time"
            except (OSError, subprocess.SubprocessError):
                pass
        return "vm"
    if sys.platform == "win32" and win_rss_fn() is not None:
        return "win_psapi"
    return "vm"


def rss_probe():
    global _RSS_PROBE
    if _RSS_PROBE is None:
        _RSS_PROBE = detect_rss_probe()
    return _RSS_PROBE


def run_once(deploy, obe, args, extra_env=None, obr_flags=(), pin_cpus=None,
             timeout=1800):
    """One timed run. Returns (wall_seconds, exit_code, fields, stderr).

    `fields` holds the [gc-stats] counters plus peak_rss_bytes (the OS number
    where this host has one, else the VM's), peak_rss_source naming where that
    came from, and peak_rss_vm keeping the VM's own number when it is there.
    """
    env = dict(os.environ,
               OBJECK_LIB_PATH=str(Path(deploy) / "lib"),
               OBJECK_GC_STATS="1")
    if extra_env:
        env.update(extra_env)
    cmd = [str(tool(deploy, "obr"))] + list(obr_flags) + [str(obe)]
    if args:
        cmd += args.split()
    if pin_cpus:
        cmd = ["taskset", "-c", pin_cpus] + cmd
    probe = rss_probe()
    # The wrapper's extra fork is constant and applies to every build, so a
    # master-vs-9.4 delta is unaffected by it.
    if probe == "time_l":
        cmd = [SYS_TIME, "-l"] + cmd
    elif probe == "gnu_time":
        cmd = [SYS_TIME, "-f", GNU_RSS_FMT] + cmd

    t0 = time.perf_counter()
    proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    try:
        _, err = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()
        return time.perf_counter() - t0, -99, {}, "TIMEOUT after %ds" % timeout
    wall = time.perf_counter() - t0

    fields = gc_stats_fields(err)
    vm_rss = fields.get("peak_rss_bytes")
    os_rss = None
    if probe == "time_l":
        m = BSD_RSS_RE.search(err or "")
        if m:
            os_rss = int(m.group(1))            # already bytes
    elif probe == "gnu_time":
        m = GNU_RSS_RE.search(err or "")
        if m:
            os_rss = int(m.group(1)) * 1024     # GNU %M is kilobytes
    elif probe == "win_psapi":
        os_rss = win_peak_rss(getattr(proc, "_handle", None))

    if os_rss is not None:
        fields["peak_rss_bytes"] = os_rss
        fields["peak_rss_source"] = probe
    elif vm_rss is not None:
        fields["peak_rss_bytes"] = vm_rss
        fields["peak_rss_source"] = "vm"
    else:
        fields.pop("peak_rss_bytes", None)
        fields["peak_rss_source"] = "none"
    if vm_rss is not None:
        fields["peak_rss_vm"] = vm_rss
    return wall, proc.returncode, fields, err


# ---- statistics ------------------------------------------------------------

def mann_whitney_p(a, b):
    """
    Two-sided Mann-Whitney U, normal approximation with tie correction.
    n=15 per group is comfortably enough for the approximation, and it makes
    no normality assumption -- which matters, because run-time distributions
    are right-skewed (a stray scheduler hit only ever makes a run slower).
    Returns None when either group is too small to say anything.
    """
    n1, n2 = len(a), len(b)
    if n1 < 5 or n2 < 5:
        return None
    merged = sorted([(v, 0) for v in a] + [(v, 1) for v in b])
    ranks = [0.0] * len(merged)
    i = 0
    tie_term = 0.0
    while i < len(merged):
        j = i
        while j + 1 < len(merged) and merged[j + 1][0] == merged[i][0]:
            j += 1
        avg = (i + j) / 2.0 + 1.0
        for k in range(i, j + 1):
            ranks[k] = avg
        t = j - i + 1
        if t > 1:
            tie_term += t ** 3 - t
        i = j + 1
    r1 = sum(rk for rk, (_, g) in zip(ranks, merged) if g == 0)
    u1 = r1 - n1 * (n1 + 1) / 2.0
    u = min(u1, n1 * n2 - u1)
    mu = n1 * n2 / 2.0
    n = n1 + n2
    sigma_sq = (n1 * n2 / 12.0) * ((n + 1) - tie_term / float(n * (n - 1)))
    if sigma_sq <= 0:
        return None
    z = (u - mu + 0.5) / (sigma_sq ** 0.5)
    # two-sided normal tail
    return max(0.0, min(1.0, 2.0 * (1.0 - 0.5 * (1.0 + math.erf(abs(z) / math.sqrt(2.0))))))


# ---- modes -----------------------------------------------------------------

def mode_baseline(a):
    benches = list(CLBG.items()) + list(PERF.items())
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    builds = [("master", a.master), ("v2026.9.4", a.base)]

    print("peak RSS source: %s" % rss_probe())
    print("compiling %d benchmarks with each build" % len(benches))
    obes = {}
    for tag, deploy in builds:
        for name, _ in benches:
            obes[(tag, name)] = compile_bench(deploy, a.repo, name, out, tag)

    raw_path = out / "baseline_raw.csv"
    raw = open(raw_path, "w", newline="")
    w = csv.writer(raw)
    w.writerow(["build", "benchmark", "run", "wall_s", "peak_rss_bytes",
                "peak_rss_source", "peak_rss_vm",
                "gc_minor", "gc_major", "gc_promoted_bytes",
                "pause_p50_us", "pause_p95_us", "pause_max_us", "exit"])

    samples = {}
    chosen = {}
    for name, args in benches:
        print("\n=== %s (args=%r) ===" % (name, args or "-"))
        # Warm-ups are discarded for measurement, but they are timed, so the run
        # count calibrates itself at no extra cost.
        warm = {}
        for tag, deploy in builds:
            ws = [run_once(deploy, obes[(tag, name)], args)[0] for _ in range(a.warmups)]
            if ws:
                warm[tag] = statistics.median(ws)
        runs = a.runs
        if a.adaptive and warm:
            # Calibrate on the SLOWER build so both columns get equal power and the
            # ABAB pairing stays symmetric; unequal n would sample one build better.
            t = max(warm.values())
            if t > 0:
                runs = max(MIN_RUNS, min(MAX_RUNS, int(math.ceil(TARGET_SECONDS / t))))
            print("  warm-up %.4fs on the slower build -> n=%d per build" % (t, runs))
        chosen[name] = runs
        # interleaved ABAB so any thermal or background drift hits both builds
        for i in range(1, runs + 1):
            for tag, deploy in builds:
                wall, code, gc, _ = run_once(deploy, obes[(tag, name)], args)
                if code != 0:
                    print("  !! %s %s run %d exited %d" % (tag, name, i, code))
                samples.setdefault((name, tag), []).append(wall)
                w.writerow([tag, name, i, "%.6f" % wall,
                            gc.get("peak_rss_bytes", ""), gc.get("peak_rss_source", ""),
                            gc.get("peak_rss_vm", ""), gc.get("minor", ""),
                            gc.get("major", ""), gc.get("promoted_bytes", ""),
                            gc.get("pause_p50_us", ""), gc.get("pause_p95_us", ""),
                            gc.get("pause_max_us", ""), code])
                raw.flush()
            print("  run %d/%d  master=%.3fs  9.4=%.3fs"
                  % (i, runs, samples[(name, "master")][-1],
                     samples[(name, "v2026.9.4")][-1]))
    raw.close()

    # summary: master vs 9.4, the >5% / p<0.01 question
    rows = []
    regressions = []
    for name, _ in benches:
        m = samples.get((name, "master"), [])
        b = samples.get((name, "v2026.9.4"), [])
        if not m or not b:
            continue
        mm, mb = statistics.median(m), statistics.median(b)
        delta = (mm - mb) / mb * 100.0 if mb else float("nan")
        p = mann_whitney_p(m, b)
        rows.append({"benchmark": name, "runs": len(m), "master_median_s": round(mm, 6),
                     "v2026_9_4_median_s": round(mb, 6),
                     "delta_pct": round(delta, 2),
                     "p_value": (round(p, 6) if p is not None else None),
                     "master_samples": [round(x, 6) for x in m],
                     "v2026_9_4_samples": [round(x, 6) for x in b]})
        if delta > 5.0 and p is not None and p < 0.01:
            regressions.append((name, delta, p))

    (out / "baseline_summary.json").write_text(json.dumps(rows, indent=2))
    print("\n%-28s %12s %12s %9s %10s" % ("benchmark", "master", "v2026.9.4", "delta%", "p"))
    for r in rows:
        print("%-28s %12.4f %12.4f %+9.2f %10s"
              % (r["benchmark"], r["master_median_s"], r["v2026_9_4_median_s"],
                 r["delta_pct"],
                 "%.4g" % r["p_value"] if r["p_value"] is not None else "-"))
    print("\nGATE (>5%% slower AND p<0.01): %s"
          % ("PASS -- no regression" if not regressions else "FAIL"))
    for name, d, p in regressions:
        print("  REGRESSION %s +%.2f%% (p=%.4g)" % (name, d, p))
    print("\nraw: %s\nsummary: %s" % (raw_path, out / "baseline_summary.json"))
    return 1 if regressions else 0


def mode_nursery(a):
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    path = out / "nursery_sweep.csv"
    print("peak RSS source: %s" % rss_probe())
    f = open(path, "w", newline="")
    w = csv.writer(f)
    w.writerow(["benchmark", "nursery", "run", "wall_s", "peak_rss_bytes",
                "peak_rss_source", "peak_rss_vm",
                "gc_minor", "gc_major", "gc_promoted_objects", "gc_promoted_bytes",
                "pause_p50_us", "pause_p95_us", "pause_max_us", "exit"])
    allargs = dict(CLBG)
    allargs.update(PERF)
    for name in NURSERY_BENCHES:
        obe = compile_bench(a.master, a.repo, name, out, "master")
        args = allargs.get(name, "")
        for size in NURSERY_SIZES:
            print("\n=== %s @ --nursery=%s ===" % (name, size))
            for _ in range(a.warmups):
                run_once(a.master, obe, args, obr_flags=["--nursery=" + size])
            for i in range(1, a.runs + 1):
                wall, code, gc, _ = run_once(a.master, obe, args,
                                             obr_flags=["--nursery=" + size])
                w.writerow([name, size, i, "%.6f" % wall,
                            gc.get("peak_rss_bytes", ""), gc.get("peak_rss_source", ""),
                            gc.get("peak_rss_vm", ""), gc.get("minor", ""),
                            gc.get("major", ""), gc.get("promoted_objects", ""),
                            gc.get("promoted_bytes", ""), gc.get("pause_p50_us", ""),
                            gc.get("pause_p95_us", ""), gc.get("pause_max_us", ""), code])
                f.flush()
                print("  %d/%d  %.3fs  rss=%s(%s) minor=%s major=%s p95=%sus"
                      % (i, a.runs, wall, gc.get("peak_rss_bytes", "?"),
                         gc.get("peak_rss_source", "?"),
                         gc.get("minor", "?"), gc.get("major", "?"),
                         gc.get("pause_p95_us", "?")))
    f.close()
    print("\nwrote %s" % path)
    return 0


def stress_confinement():
    """Can this host confine core_thread_gc_stress to fewer CPUs than threads?

    Returns (ok, reasons). Confinement needs all three: a Linux-style affinity
    API, taskset on PATH, and more than two usable CPUs -- pinning to two CPUs
    on a two-CPU host confines nothing.
    """
    reasons = []
    if hasattr(os, "sched_getaffinity"):
        cpus = len(os.sched_getaffinity(0))
    else:
        reasons.append("os.sched_getaffinity is missing, so this host has no "
                       "CPU-affinity API to confine the fixture with")
        cpus = os.cpu_count() or 0
    if shutil.which("taskset") is None:
        reasons.append("taskset is not on PATH")
    if cpus < STRESS_MIN_HOST_CPUS:
        reasons.append("only %d usable CPU(s) visible; pinning to %d would not "
                       "confine anything" % (cpus, len(STRESS_CPUS.split(","))))
    return (not reasons), reasons


def stress_refusal(reasons):
    print("REFUSING to run stress mode on this host (%s)." % sys.platform)
    print("")
    print("#861 is a GC race that only opens when core_thread_gc_stress's threads")
    print("OUTNUMBER the CPUs they run on: the fixture has to be preempted inside a")
    print("collection for the window to be hit, and a host with a free core per")
    print("thread almost never does that. Measured against a pre-fix build:")
    print("")
    print("  Linux, pinned to 2 CPUs with taskset ...  8 SIGSEGV / 3600 runs")
    print("  Linux, unpinned on a 32-thread box .....  0 / 900")
    print("  Windows, sequential ....................  0 / 600, and 0 / 400 pinned")
    print("")
    print("So an unconfined run has no power at all -- it reports zero failures on a")
    print("build already known to be broken. Printing '0 failures in N runs' from")
    print("such a host would publish a bound that means nothing, so this mode does")
    print("not produce one.")
    print("")
    print("This host cannot confine the run:")
    for r in reasons:
        print("  - %s" % r)
    print("")
    print("Run this mode on a Linux host with taskset and more than %d CPUs."
          % (STRESS_MIN_HOST_CPUS - 1))
    print("Do not substitute oversubscription (more fixture threads than CPUs")
    print("without pinning): that is under measurement separately and is not yet")
    print("validated as a replacement.")
    return 2


def mode_stress(a):
    """#861: core_thread_gc_stress at a 256k nursery, pinned, 4 configs x N runs."""
    ok, reasons = stress_confinement()
    if not ok:
        return stress_refusal(reasons)

    out = Path(a.out)
    (out / "crashes").mkdir(parents=True, exist_ok=True)
    obe = compile_bench(a.master, a.repo, "core_thread_gc_stress", out, "master")
    configs = []
    for verify in (False, True):
        for jit_off in (False, True):
            env = {"OBJECK_NURSERY": "256k"}
            if verify:
                env["OBJECK_GC_VERIFY"] = "1"
            configs.append({
                "name": "nursery256k%s%s" % ("+verify" if verify else "",
                                             "+jitoff" if jit_off else "+jitdefault"),
                "env": env,
                "flags": ["--jit=off"] if jit_off else [],
            })
    path = out / "stress_861.csv"
    f = open(path, "w", newline="")
    w = csv.writer(f)
    w.writerow(["config", "cpus", "run", "exit", "wall_s", "note"])
    print("pinning every run to CPUs %s (taskset)" % STRESS_CPUS)
    failures = 0
    for cfg in configs:
        print("\n=== %s : %d runs ===" % (cfg["name"], a.runs))
        for i in range(1, a.runs + 1):
            wall, code, _, err = run_once(a.master, obe, "", extra_env=cfg["env"],
                                          obr_flags=cfg["flags"], pin_cpus=STRESS_CPUS)
            note = ""
            if code != 0:
                failures += 1
                note = "FAILURE"
                p = out / "crashes" / ("%s_run%03d.txt" % (cfg["name"], i))
                p.write_text("exit=%d\nwall=%.3f\ncpus=%s\n\n%s"
                             % (code, wall, STRESS_CPUS, err))
                print("  !! run %d exit %d -> %s" % (i, code, p))
            w.writerow([cfg["name"], STRESS_CPUS, i, code, "%.6f" % wall, note])
            f.flush()
            if i % 25 == 0:
                print("  %d/%d done (%d failures so far)" % (i, a.runs, failures))
    f.close()
    total = len(configs) * a.runs
    print("\n%d failures in %d runs -> %s" % (failures, total,
          "REPRODUCED, tell the integrator" if failures else
          "clean; bounds this host PINNED TO %s under ~%.2f%% per run (rule of three)"
          % (STRESS_CPUS, 300.0 / total)))
    print("wrote %s" % path)
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["baseline", "nursery", "stress"])
    ap.add_argument("--master", required=True, help="deploy tree built from master")
    ap.add_argument("--base", help="deploy tree built from v2026.9.4 (baseline mode)")
    ap.add_argument("--repo", required=True, help="checkout holding programs/tests")
    ap.add_argument("--out", required=True, help="output directory (outside the repo)")
    ap.add_argument("--runs", type=int, default=None)
    ap.add_argument("--warmups", type=int, default=2)
    ap.add_argument("--no-adaptive", action="store_true",
                    help="keep the run count fixed instead of scaling short benchmarks up")
    a = ap.parse_args()
    explicit_runs = a.runs is not None
    if a.runs is None:
        a.runs = 200 if a.mode == "stress" else MIN_RUNS
    # Adaptive only in baseline mode, and never when the caller fixed --runs.
    a.adaptive = (a.mode == "baseline") and not a.no_adaptive and not explicit_runs
    if a.mode == "baseline" and not a.base:
        ap.error("baseline mode needs --base (the v2026.9.4 deploy tree)")
    return {"baseline": mode_baseline, "nursery": mode_nursery, "stress": mode_stress}[a.mode](a)


if __name__ == "__main__":
    sys.exit(main())
