#!/usr/bin/env python3
"""Runs the baseline matrix: every host/configuration x repetitions, round-robin, with quiet-machine gating.

  BL=$HOME/baselines MODEL=/path/resnet34.onnx python3 run_matrix.py --ep cuda --reps 10 [--only substring] [--warm 20 --measure 60]

Each run appends one JSON object to results/raw.jsonl (resumable: finished (config, ep, rep) triples are
skipped). See README.md for what every field means.
"""
import argparse
import json
import os
import resource
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BL = os.environ.get("BL", os.path.expanduser("~/baselines"))
MODEL = os.environ.get("MODEL", "")
ORT = os.environ.get("ORT", os.path.expanduser("~/ortgpu-1.24.2/lib"))
CUDA_LIB = os.environ.get("CUDA_LIB", "/usr/local/cuda-12/lib64")
RESULTS = os.path.join(HERE, "results")
HZ = os.sysconf(os.sysconf_names["SC_CLK_TCK"])

JAVA_CP = f"{BL}/dl/onnxruntime_gpu-1.24.2.jar:{BL}/java/out"
DOTNET = f"{BL}/dotnet/sdk"


def configs():
    """(host, config, env, argv-prefix) -- argv gets '<model> <ep> <batch> <warm> <measure>' appended."""
    c = []
    for name, lib in (("parked", "lib_A"), ("unparked", "lib_B")):
        c.append(("objeck", name, {"OBJECK_LIB_PATH": f"{BL}/objeck/{lib}", "LD_LIBRARY_PATH": f"{ORT}:{CUDA_LIB}"},
                  [os.path.expanduser("~/onnxgpu/core/vm/obr"), f"{BL}/objeck/baseline.obe"]))
    # BoundSession with per-run upload and download (the bound_copy mode): the same host traffic as the other hosts,
    # without the per-call Float[] conversion that makes Session->Run slow. unparked = no APITools_Parked anywhere.
    for name, lib in (("bound-parked", "lib_A"), ("bound-unparked", "lib_C")):
        c.append(("objeck", name, {"OBJECK_LIB_PATH": f"{BL}/objeck/{lib}", "LD_LIBRARY_PATH": f"{ORT}:{CUDA_LIB}"},
                  [os.path.expanduser("~/onnxgpu/core/vm/obr"), f"{BL}/objeck/baseline_b.obe"]))
    jv = ["java", "-cp", JAVA_CP]
    c.append(("java", "g1", {"LD_LIBRARY_PATH": CUDA_LIB}, jv + ["-XX:+UseG1GC", "Baseline"]))
    c.append(("java", "zgc", {"LD_LIBRARY_PATH": CUDA_LIB}, jv + ["-XX:+UseZGC", "Baseline"]))
    c.append(("java", "parallel", {"LD_LIBRARY_PATH": CUDA_LIB}, jv + ["-XX:+UseParallelGC", "Baseline"]))
    for srv in (0, 1):
        for con in (0, 1):
            c.append(("dotnet", f"{'server' if srv else 'workstation'}-{'conc' if con else 'nonconc'}",
                      {"DOTNET_ROOT": DOTNET, "DOTNET_gcServer": str(srv), "DOTNET_gcConcurrent": str(con),
                       "LD_LIBRARY_PATH": CUDA_LIB, "DOTNET_CLI_TELEMETRY_OPTOUT": "1"},
                      [f"{DOTNET}/dotnet", f"{BL}/dotnet/build/out/baseline.dll"]))
    c.append(("go", "default", {"ORT_LIB": f"{ORT}/libonnxruntime.so", "LD_LIBRARY_PATH": f"{ORT}:{CUDA_LIB}"},
              [f"{BL}/go/build/baseline"]))
    c.append(("python", "default", {"LD_LIBRARY_PATH": CUDA_LIB},
              [f"{BL}/python/venv/bin/python", f"{HERE}/python/baseline.py"]))
    c.append(("cpp", "control", {"LD_LIBRARY_PATH": f"{ORT}:{CUDA_LIB}"}, [f"{BL}/cpp/baseline"]))

    # one documented 'best practice' setting per host (see README, section Tuning)
    heap = ["-Xms4g", "-Xmx4g", "-XX:+AlwaysPreTouch"]
    c.append(("java", "g1-tuned", {"LD_LIBRARY_PATH": CUDA_LIB},
              jv + ["-XX:+UseG1GC", "-XX:MaxGCPauseMillis=10"] + heap + ["Baseline"]))
    c.append(("java", "zgc-gen-tuned", {"LD_LIBRARY_PATH": CUDA_LIB},
              jv + ["-XX:+UseZGC", "-XX:+ZGenerational"] + heap + ["Baseline"]))
    c.append(("dotnet", "server-conc-gen0size", {"DOTNET_ROOT": DOTNET, "DOTNET_gcServer": "1", "DOTNET_gcConcurrent": "1",
                                                  "DOTNET_GCgen0size": "0x10000000", "LD_LIBRARY_PATH": CUDA_LIB,
                                                  "DOTNET_CLI_TELEMETRY_OPTOUT": "1"},
              [f"{DOTNET}/dotnet", f"{BL}/dotnet/build/out/baseline.dll"]))
    c.append(("go", "gogc400-memlimit", {"ORT_LIB": f"{ORT}/libonnxruntime.so", "LD_LIBRARY_PATH": f"{ORT}:{CUDA_LIB}",
                                        "GOGC": "400", "GOMEMLIMIT": "8GiB"}, [f"{BL}/go/build/baseline"]))
    c.append(("python", "switch-0.5ms", {"LD_LIBRARY_PATH": CUDA_LIB, "PY_SWITCH": "0.0005"},
              [f"{BL}/python/venv/bin/python", f"{HERE}/python/baseline.py"]))
    return c


def cpu_times():
    with open("/proc/stat") as f:
        v = list(map(int, f.readline().split()[1:]))
    idle = v[3] + v[4]
    return sum(v) - idle - (v[8] if len(v) > 8 else 0), time.time()  # busy jiffies (excluding guest double count)


def own_cpu_s():
    r = resource.getrusage(resource.RUSAGE_CHILDREN)
    return r.ru_utime + r.ru_stime


def loadavg():
    return [float(x) for x in open("/proc/loadavg").read().split()[:3]]


def smi(query):
    try:
        return subprocess.run(["nvidia-smi", f"--query-gpu={query}", "--format=csv,noheader,nounits"],
                              capture_output=True, text=True, timeout=20).stdout.strip()
    except Exception as e:  # noqa
        return f"error:{e}"


GPU_FIELDS = "clocks.sm,clocks.mem,clocks.max.sm,pstate,power.draw,temperature.gpu,utilization.gpu,memory.used"


def others_idle_cores(seconds=8):
    """Average busy cores over a quiet sampling window (nothing of ours running)."""
    j0, t0 = cpu_times()
    time.sleep(seconds)
    j1, t1 = cpu_times()
    return (j1 - j0) / HZ / (t1 - t0)


def wait_quiet(ep, max_wait=1200):
    """Block until the machine is quiet. CUDA runs: 1-min loadavg < 4 and others' CPU < 2 cores.
    CPU runs: our own previous 32-thread run keeps loadavg high for ~2 min, so only the instantaneous
    others-CPU rule is used (the loadavg is still recorded)."""
    t0 = time.time()
    while True:
        la = loadavg()
        busy = others_idle_cores()
        # start gate (looser than the 'quiet' label, which is decided afterwards from what the run actually saw)
        ok = busy < 3.0 and (la[0] < 6.0 or ep == "cpu")
        if ok:
            return la, busy, time.time() - t0
        if time.time() - t0 > max_wait:
            return la, busy, time.time() - t0
        time.sleep(15)


def run_one(host, cfg, env, argv, ep, batch, warm, measure, timeout):
    environ = dict(os.environ)
    environ.update(env)
    cmd = argv + [MODEL, ep, str(batch), str(warm), str(measure)]
    if cfg.startswith("bound-"):
        cmd.append("bound")
    if host == "python" and "PY_SWITCH" in env:
        pass  # handled inside baseline.py via the environment
    la_before, busy_before, waited = wait_quiet(ep)
    gpu_before = smi(GPU_FIELDS)
    base_mem = int(gpu_before.split(",")[-1]) if gpu_before.split(",")[-1].strip().isdigit() else 0
    peak = [base_mem]
    stop = threading.Event()

    def sampler():
        while not stop.is_set():
            v = smi("memory.used")
            if v.isdigit():
                peak[0] = max(peak[0], int(v))
            stop.wait(1.0)

    th = threading.Thread(target=sampler, daemon=True)
    th.start()
    j0, t0 = cpu_times()
    own0 = own_cpu_s()
    p = subprocess.run(cmd, env=environ, capture_output=True, text=True, timeout=timeout)
    j1, t1 = cpu_times()
    own1 = own_cpu_s()
    stop.set()
    th.join(timeout=5)
    wall = t1 - t0
    others = max(0.0, ((j1 - j0) / HZ - (own1 - own0)) / wall)
    la_after = loadavg()
    gpu_after = smi(GPU_FIELDS)
    res = None
    for line in p.stdout.splitlines():
        if line.startswith("RESULT "):
            res = json.loads(line[7:])
    return {
        "host": host, "config": cfg, "ep": ep, "batch": batch, "warm_s": warm, "measure_s": measure,
        "ts": time.strftime("%Y-%m-%dT%H:%M:%S"), "wall_s": wall, "rc": p.returncode,
        "load1_before": la_before[0], "load5_before": la_before[1], "others_cores_before": busy_before,
        "waited_s": waited, "load1_after": la_after[0], "others_cores_during": others, "own_cpu_cores": (own1 - own0) / wall,
        "gpu_before": gpu_before, "gpu_after": gpu_after, "gpu_mem_base_mib": base_mem, "gpu_mem_peak_mib": peak[0],
        "result": res, "stderr_tail": "" if res else (p.stderr or "")[-600:],
        "cmd": " ".join(cmd),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ep", default="cuda")
    ap.add_argument("--reps", type=int, default=10)
    ap.add_argument("--batch", type=int, default=32)
    ap.add_argument("--warm", type=int, default=20)
    ap.add_argument("--measure", type=int, default=60)
    ap.add_argument("--only", default="")
    ap.add_argument("--skip", default="")
    ap.add_argument("--retries", type=int, default=2)
    a = ap.parse_args()
    if not MODEL:
        sys.exit("set MODEL")
    os.makedirs(RESULTS, exist_ok=True)
    raw = os.path.join(RESULTS, "raw.jsonl")
    done = set()
    if os.path.exists(raw):
        for line in open(raw):
            r = json.loads(line)
            if r.get("result") and not r.get("noisy"):
                done.add((r["host"], r["config"], r["ep"], r["rep"]))
    clk = os.path.join(RESULTS, f"clocks_{a.ep}_{time.strftime('%Y%m%d_%H%M%S')}.txt")
    with open(clk, "w") as f:
        f.write(subprocess.run(["nvidia-smi", "-q", "-d", "CLOCK"], capture_output=True, text=True).stdout)
    skips = [s for s in a.skip.split(",") if s]
    onlys = [s for s in a.only.split(",") if s] or [""]
    cfgs = [c for c in configs() if any(o in f"{c[0]}/{c[1]}" for o in onlys)
            and not any(s in f"{c[0]}/{c[1]}" for s in skips)]
    for rep in range(1, a.reps + 1):
        for host, cfg, env, argv in cfgs:
            if (host, cfg, a.ep, rep) in done:
                continue
            for attempt in range(a.retries + 1):
                row = run_one(host, cfg, env, argv, a.ep, a.batch, a.warm, a.measure, a.warm + a.measure + 600)
                row["rep"] = rep
                row["attempt"] = attempt
                # 3 cores = under 10% of the 32 hardware threads; the two measured threads are never short of a core
                row["noisy"] = bool(row["others_cores_during"] > 3.0 or (row["load1_before"] >= 4.0 and a.ep == "cuda"))
                with open(raw, "a") as f:
                    f.write(json.dumps(row) + "\n")
                r = row["result"]
                print(f"[{row['ts']}] {host}/{cfg} {a.ep} rep{rep} try{attempt}: "
                      + (f"stall p99={r['stall_us']['p99']:.0f}us max={r['stall_us']['max']:.0f}us infer p50={r['infer_ms']['p50']:.1f}ms "
                         f"others={row['others_cores_during']:.2f} load1={row['load1_before']:.1f} noisy={row['noisy']}"
                         if r else f"FAILED rc={row['rc']} {row['stderr_tail'][-200:]}"), flush=True)
                if r and not row["noisy"]:
                    break
                if not r:
                    break
    print("MATRIX DONE", flush=True)


if __name__ == "__main__":
    main()
