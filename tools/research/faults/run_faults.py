#!/usr/bin/env python3
"""Fault-injection driver. Runs scenarios x seeded repetitions against the Objeck harness programs
(fault_agents.obe, fault_infer.obe) and the Python baseline, samples the child from /proc, applies
the process-level faults (signals, closed stdin, /dev/full, clock step), and appends one CSV row per
repetition. Every child runs under a watchdog and in its own process group, so nothing is left behind.

  python3 run_faults.py --list
  python3 run_faults.py --only agents_tool_hang --reps 5
  python3 run_faults.py --suite all --reps 30 --csv results.csv

See README.md for the environment variables (paths to obr, the .obe files, the model).
"""
import argparse, csv, json, os, re, resource, signal, statistics, subprocess, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import faultlib

HOME = os.path.expanduser("~")
OBR = os.environ.get("FAULTS_OBR", HOME + "/fd/bin/obr")
LIBS = os.environ.get("FAULTS_LIB", HOME + "/fd/lib")
ORT_LD = os.environ.get("FAULTS_ORT_LD", HOME + "/ortgpu-1.24.2/lib")
BIN = os.environ.get("FAULTS_BIN", HERE)  # where fa.obe / fi.obe live
PY = os.environ.get("FAULTS_PY", HOME + "/faultvenv/bin/python")
MODEL = os.environ.get("FAULTS_MODEL", HOME + "/ft/data/bert/classifier/bert_one_label/model.onnx")
SHIM = os.environ.get("FAULTS_SHIM", HERE + "/clockshim.so")
WATCHDOG_GRACE = 15  # seconds past a program's own deadline before the driver kills the group

KV = re.compile(r"(\w+)=(\S*)")


def kv(text):
    return dict(KV.findall(text))


def uptime():
    try:
        return open("/proc/loadavg").read().split()[0]
    except OSError:
        return ""


def gpu_used_mb():
    try:
        out = subprocess.run(["nvidia-smi", "--query-gpu=memory.used", "--format=csv,noheader,nounits"],
                             capture_output=True, text=True, timeout=10).stdout.strip().splitlines()
        return int(out[0])
    except Exception:
        return None


class Child:
    """A process in its own group with a /proc sampler and a watchdog."""

    def __init__(self, cmd, env=None, stdin=subprocess.DEVNULL, limit_as=None, kill_after=60, cwd=None):
        e = dict(os.environ)
        e["OBJECK_LIB_PATH"] = LIBS
        # the Objeck runtime links the ONNX Runtime from ORT_LD; Python must keep its own copy
        e["LD_LIBRARY_PATH"] = "" if cmd[0] == PY else ORT_LD + ":" + LIBS + "/native"
        if env:
            e.update(env)

        def pre():
            os.setsid()
            if limit_as:
                resource.setrlimit(resource.RLIMIT_AS, (limit_as, limit_as))

        self.t0 = time.monotonic()
        self.p = subprocess.Popen(cmd, stdin=stdin, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  env=e, preexec_fn=pre, cwd=cwd or BIN)
        self.pgid = self.p.pid
        self.out, self.err = [], []
        self.peak_threads = self.peak_rss = self.last_threads = self.last_fds = 0
        self.watchdog_killed = False
        self.kill_after = kill_after
        self.stop = False
        self.threads = [threading.Thread(target=self._read, args=(self.p.stdout, self.out), daemon=True),
                        threading.Thread(target=self._read, args=(self.p.stderr, self.err), daemon=True),
                        threading.Thread(target=self._sample, daemon=True)]
        for t in self.threads:
            t.start()

    def _read(self, f, sink):
        for raw in iter(f.readline, b""):
            sink.append(raw.decode("utf-8", "replace").rstrip("\n"))

    def _sample(self):
        pid = self.p.pid
        while not self.stop and self.p.poll() is None:
            try:
                st = open("/proc/%d/status" % pid).read()
                th = int(re.search(r"Threads:\s+(\d+)", st).group(1))
                rss = int(re.search(r"VmHWM:\s+(\d+)", st).group(1)) // 1024
                fds = len(os.listdir("/proc/%d/fd" % pid))
                self.last_threads, self.last_fds = th, fds
                self.peak_threads = max(self.peak_threads, th)
                self.peak_rss = max(self.peak_rss, rss)
            except (OSError, AttributeError):
                pass
            time.sleep(0.01)

    def signal(self, sig):
        try:
            os.kill(self.p.pid, sig)
        except ProcessLookupError:
            pass

    def wait(self, timeout=None):
        timeout = self.kill_after if timeout is None else timeout
        try:
            self.p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.watchdog_killed = True
            self.kill_group()
            self.p.wait()
        self.finish()

    def kill_group(self):
        try:
            os.killpg(self.pgid, signal.SIGKILL)
        except ProcessLookupError:
            pass

    def finish(self):
        self.stop = True
        self.wall = time.monotonic() - self.t0
        for t in self.threads[:2]:
            t.join(timeout=3)
        # leftovers in the group (a leaked grandchild would show here)
        self.leftover = group_members(self.pgid)
        self.kill_group()
        self.rc = self.p.returncode

    def stdout(self):
        return self.out


def group_members(pgid):
    n = 0
    for d in os.listdir("/proc"):
        if d.isdigit():
            try:
                with open("/proc/%s/stat" % d) as f:
                    parts = f.read().rsplit(")", 1)[1].split()
                if int(parts[2]) == pgid and parts[0] != "Z":
                    n += 1
            except (OSError, IndexError, ValueError):
                pass
    return n


class Server:
    def __init__(self, mode, seed, rate, delay=3.0, hold=600.0, trickle=0.01):
        self.p = subprocess.Popen([sys.executable, os.path.join(HERE, "fake_llm.py"), "--mode", mode, "--seed", str(seed),
                                   "--rate", str(rate), "--delay", str(delay), "--hold", str(hold), "--trickle", str(trickle)],
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, preexec_fn=os.setsid)
        self.port = int(self.p.stdout.readline().split()[1])
        self.url = "http://127.0.0.1:%d" % self.port

    def close(self):
        try:
            os.killpg(self.p.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        self.p.wait()


def percentile(xs, q):
    if not xs:
        return ""
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))]


# ---------------------------------------------------------------------------------- scenarios

NETWORK = {"http500", "garbage", "truncjson", "wrongshape", "delay", "hold", "drop", "truncate", "rst", "slowloris"}
TOOL = {"tool_exception", "tool_fault", "tool_timeout", "tool_huge", "tool_hang", "cancel_coop", "cancel_uncoop"}


def agents_cmd(system, config, seed, n, runs, fault, rate, backend, limit, deadline, outdir=""):
    a = [str(seed), str(n), str(runs), fault, str(rate), backend, str(limit), str(deadline)]
    if outdir:
        a.append(outdir)
    if system == "objeck":
        return [OBR, os.path.join(BIN, "fa.obe")] + a
    return [PY, os.path.join(HERE, "baseline_agents.py"), "--config", config] + a


def infer_cmd(system, seed, workers, iters, fault, rate, ep, deadline, extra):
    a = [str(seed), str(workers), str(iters), fault, str(rate), ep, str(deadline), MODEL] + extra
    if system == "objeck":
        return [OBR, os.path.join(BIN, "fi.obe")] + a
    return [PY, os.path.join(HERE, "baseline_infer.py")] + a


def analyze_agents(ch, seed, n, runs, fault, rate, has_net):
    units = {}
    hangs, summary, saboteur = [], None, False
    disk = []
    for ln in ch.out:
        if ln.startswith("U "):
            d = kv(ln)
            if not all(k in d for k in ("a", "r", "inj", "out", "det", "silent", "ms", "cancel_ms")):
                continue  # a line torn by the process dying mid-write
            units[(int(d["a"]), int(d["r"]))] = d
        elif ln.startswith("HANG"):
            hangs.append(kv(ln))
        elif ln.startswith("SUMMARY"):
            summary = kv(ln)
        elif ln.startswith("DISK"):
            disk.append(kv(ln))
    row = {}
    row["summary_seen"] = int(summary is not None)
    row["stuck"] = int(summary["stuck"]) if summary else n
    row["finished"] = int(summary["finished"]) if summary else 0
    inj_units = [(a, r) for a in range(n) for r in range(runs) if faultlib.faulted(seed, a, r, rate)] if fault != "none" else []
    inj_set = set(inj_units)
    by_units = [(a, r) for a in range(n) for r in range(runs) if not faultlib.is_victim(a)]
    row["inj_expected"] = len(inj_units)
    row["inj_seen"] = sum(1 for u in inj_units if u in units)
    row["inj_lost"] = len(inj_units) - row["inj_seen"]
    iu = [units[u] for u in inj_units if u in units]
    row["inj_ok"] = sum(1 for d in iu if d["out"] == "ok")
    row["inj_detected"] = sum(1 for d in iu if d["det"] == "1")
    row["inj_error"] = sum(1 for d in iu if d["out"] == "error")
    row["inj_recovered"] = sum(1 for d in iu if d["out"] == "ok" and d["det"] == "1")
    row["inj_absorbed"] = sum(1 for d in iu if d["out"] == "ok" and d["det"] == "0")
    row["inj_wrong"] = sum(1 for d in iu if d["out"] == "wrong")
    ms = [int(d["ms"]) for d in iu]
    row["inj_ms_median"] = statistics.median(ms) if ms else ""
    row["inj_ms_p95"] = percentile(ms, 0.95)
    cms = [int(d["cancel_ms"]) for d in iu if int(d["cancel_ms"]) >= 0]
    row["cancel_ms_median"] = statistics.median(cms) if cms else ""
    row["by_expected"] = len(by_units)
    bu = [units[u] for u in by_units if u in units]
    row["by_seen"] = len(bu)
    row["by_ok"] = sum(1 for d in bu if d["out"] == "ok")
    bms = [int(d["ms"]) for d in bu]
    row["by_ms_max"] = max(bms) if bms else ""
    row["by_ms_p95"] = percentile(bms, 0.95)
    row["silent"] = sum(int(d["silent"]) for d in units.values())
    row["units_done"] = len(units)
    row["units_expected"] = n * runs
    row["inj_mismatch"] = sum(1 for d in iu if d["inj"] != "1") if fault in TOOL else ""
    if disk:
        row["disk_short"] = sum(int(d["short"]) for d in disk)
        row["disk_writes"] = sum(int(d["runs"]) for d in disk)
    return row


def analyze_infer(ch, workers, fault):
    ws, hangs, summary = {}, 0, None
    hog = []
    for ln in ch.out:
        if ln.startswith("W "):
            d = kv(ln)
            ws[int(d["id"])] = d
        elif ln.startswith("HANG"):
            hangs += 1
        elif ln.startswith("SUMMARY"):
            summary = kv(ln)
        elif ln.startswith("HOG"):
            hog.append(ln)
    row = {"summary_seen": int(summary is not None), "stuck": int(summary["stuck"]) if summary else workers,
           "finished": int(summary["finished"]) if summary else 0}
    by = [d for i, d in ws.items() if not faultlib.is_victim(i)]
    vic = [d for i, d in ws.items() if faultlib.is_victim(i)]
    row["by_expected"] = sum(1 for i in range(workers) if not faultlib.is_victim(i))
    row["by_seen"] = len(by)
    row["by_ok"] = sum(1 for d in by if int(d["wrong"]) == 0 and int(d["refused"]) == 0)
    row["by_wrong"] = sum(int(d["wrong"]) for d in by)
    row["by_refused"] = sum(int(d["refused"]) for d in by)
    row["by_ms_max"] = max([int(d["maxms"]) for d in by], default="")
    row["vic_injected"] = sum(int(d["injected"]) for d in vic)
    row["vic_clean"] = sum(int(d["inj_clean"]) for d in vic)
    row["vic_wrong"] = sum(int(d["wrong"]) for d in vic)
    row["vic_refused"] = sum(int(d["refused"]) for d in vic)
    row["silent"] = row["by_wrong"] + row["vic_wrong"]
    row["all_ok_calls"] = sum(int(d["ok"]) for d in ws.values())
    row["all_refused_calls"] = sum(int(d["refused"]) for d in ws.values())
    row["hog_last"] = hog[-1][:80] if hog else ""
    row["closed_seen"] = int(any(l.startswith("CLOSED") for l in ch.out))
    m = re.search(r"blocks_held=(\d+) refused_at=(-?\d+)", " ".join(hog))
    if m:
        row["gpu_blocks_held"], row["gpu_refused_at"] = m.group(1), m.group(2)
    m = re.search(r"blocks=(\d+)", hog[-1]) if hog else None
    if m:
        row["hog_blocks"] = m.group(1)
    return row


def common(ch, row, sc, rep, seed):
    row.update(scenario=sc["id"], system=sc["system"], config=sc.get("config", ""), fault=sc["fault"], kind=sc["kind"],
               ep=sc.get("ep", ""), rep=rep, seed=seed, rc=ch.rc, watchdog_killed=int(ch.watchdog_killed),
               wall_s=round(ch.wall, 3), threads_peak=ch.peak_threads, threads_last=ch.last_threads,
               fds_last=ch.last_fds, rss_peak_mb=ch.peak_rss, leftover_procs=ch.leftover, load1=uptime())
    err = "\n".join(ch.err)
    row["stderr_fatal"] = int(bool(re.search(r">>> (Divide|Unable|Out of|Invalid|Null|Array)", err)))
    row["stderr_head"] = err[:100].replace("\n", " | ")
    return row


def run_scenario(sc, rep, base_seed):
    seed = base_seed + rep
    kind = sc["kind"]
    fault = sc["fault"]
    gpu0 = gpu_used_mb() if sc.get("ep") == "cuda" else None
    if kind == "agents":
        n, runs, rate = sc.get("agents", 8), sc.get("runs", 8), sc.get("rate", 0.5)
        srv = None
        backend = "scripted"
        sfault = fault
        if fault in NETWORK:
            srv = Server(fault, seed, rate, delay=sc.get("delay", 3.0), hold=sc.get("hold", 600.0), trickle=sc.get("trickle", 0.01))
            backend = srv.url
            sfault = "none"
        elif sc.get("http_control"):
            srv = Server("none", seed, rate)
            backend = srv.url
        deadline = sc.get("deadline", 8)
        env = {}
        if fault.startswith("clock"):
            env = {"LD_PRELOAD": SHIM, "FAULTS_CLOCK_STEP_S": str(sc["step"])}
            sfault = "tool_timeout"
        ch = Child(agents_cmd(sc["system"], sc.get("config", ""), seed, n, runs, sfault, rate, backend, sc.get("limit", 0), deadline),
                   env=env, kill_after=deadline + WATCHDOG_GRACE)
        if fault.startswith("clock"):
            time.sleep(sc.get("step_at", 0.6))
            ch.signal(signal.SIGUSR1)
        ch.wait()
        if srv:
            srv.close()
        row = analyze_agents(ch, seed, n, runs, sfault, rate, srv is not None)
        if sfault == "none" and fault in NETWORK:
            # server-side faults: recompute the injected set with the server's own mode
            row.update(analyze_agents(ch, seed, n, runs, "http", rate, True))
        return common(ch, row, sc, rep, seed)
    if kind == "infer":
        w, iters, rate = sc.get("workers", 4), sc.get("iters", 300), sc.get("rate", 0.5)
        ep = sc.get("ep", "cpu")
        deadline = sc.get("deadline", 40)
        lim = None
        if fault == "oom_burst":
            lim = 14_000_000_000
        ch = Child(infer_cmd(sc["system"], seed, w, iters, fault, rate, ep, deadline, sc.get("extra", [])),
                   limit_as=lim, kill_after=deadline + WATCHDOG_GRACE)
        ch.wait()
        row = analyze_infer(ch, w, fault)
        row["skip"] = int(any(l.startswith("SKIP") for l in ch.out))
        if gpu0 is not None:
            time.sleep(1.5)
            g1 = gpu_used_mb()
            row["gpu_delta_mb"] = g1 - gpu0 if g1 is not None else ""
        return common(ch, row, sc, rep, seed)
    if kind == "process":
        return run_process(sc, rep, seed)
    raise SystemExit("bad kind " + kind)


def run_process(sc, rep, seed):
    fault = sc["fault"]
    row = {}
    prog = sc["prog"]
    ep = sc.get("ep", "cpu")
    gpu0 = gpu_used_mb() if ep == "cuda" else None

    def long_cmd(runs=1_000_000):
        if prog == "agents":
            return agents_cmd(sc["system"], sc.get("config", ""), seed, 8, runs, "none", 0.0, "scripted", 0, 300)
        return infer_cmd(sc["system"], seed, 4, 1_000_000, "none", 0.0, ep, 300, [])

    def short_cmd():
        if prog == "agents":
            return agents_cmd(sc["system"], sc.get("config", ""), seed, 8, 20, "none", 0.0, "scripted", 0, 30)
        return infer_cmd(sc["system"], seed, 4, 200, "none", 0.0, ep, 60, [])

    if fault in ("sigkill", "sigterm"):
        ch = Child(long_cmd(), kill_after=120)
        time.sleep(2.0)
        mid_gpu = gpu_used_mb() if gpu0 is not None else None
        t_sig = time.monotonic()
        ch.signal(signal.SIGKILL if fault == "sigkill" else signal.SIGTERM)
        try:
            ch.p.wait(timeout=10)
            row["exit_after_signal_s"] = round(time.monotonic() - t_sig, 3)
        except subprocess.TimeoutExpired:
            row["exit_after_signal_s"] = ">10"
        ch.wait(timeout=5)
        row["units_before_signal"] = sum(1 for l in ch.out if l.startswith(("U ", "W ")))
        row["graceful"] = int(any(l.startswith("SUMMARY") for l in ch.out) and ch.rc == 0)
        if gpu0 is not None:
            time.sleep(1.5)
            g1 = gpu_used_mb()
            row["gpu_mid_mb"] = (mid_gpu - gpu0) if mid_gpu is not None else ""
            row["gpu_delta_mb"] = (g1 - gpu0) if g1 is not None else ""
        # restart and finish a short run: recovery time and correctness
        t_r = time.monotonic()
        ch2 = Child(short_cmd(), kill_after=90)
        ch2.wait()
        row["restart_s"] = round(time.monotonic() - t_r, 3)
        row["restart_ok"] = int(ch2.rc == 0 and any(l.startswith("SUMMARY") for l in ch2.out) and not any(
            re.search(r"out=(wrong|error)|wrong=[1-9]|refused=[1-9]", l) for l in ch2.out))
        return common(ch, row, sc, rep, seed)
    if fault == "stdin_closed":
        feed = sc.get("feed", "")
        ch = Child(agents_cmd(sc["system"], sc.get("config", ""), seed, 1, 1, "stdin", 0, "scripted", 0, 10),
                   stdin=subprocess.PIPE, kill_after=20)
        try:
            if feed:
                ch.p.stdin.write(feed.encode())
            ch.p.stdin.close()
        except OSError:
            pass
        ch.wait()
        m = re.search(r"empty_reads=(\d+)", " ".join(ch.out))
        row["empty_reads"] = int(m.group(1)) if m else ""
        row["served"] = sum(1 for l in ch.out if l.startswith("SERVED"))
        row["eof_detected"] = int(bool(m) and int(m.group(1)) < 1000)
        return common(ch, row, sc, rep, seed)
    if fault == "disk_full":
        outdir = os.path.join("/tmp", "faults_full_%d_%d" % (os.getpid(), rep))
        os.makedirs(outdir, exist_ok=True)
        for a in range(8):
            p = os.path.join(outdir, "agent_%d.log" % a)
            if os.path.lexists(p):
                os.remove(p)
            os.symlink("/dev/full", p)
        ch = Child(agents_cmd(sc["system"], sc.get("config", ""), seed, 8, 8, "none", 0.0, "scripted", 0, 20, outdir), kill_after=40)
        ch.wait()
        for a in range(8):
            os.remove(os.path.join(outdir, "agent_%d.log" % a))
        os.rmdir(outdir)
        r = analyze_agents(ch, seed, 8, 8, "none", 0.0, False)
        row.update(r)
        return common(ch, row, sc, rep, seed)
    raise SystemExit("bad process fault " + fault)


def scenarios():
    S = []

    def add(**k):
        k["id"] = "_".join(str(k[x]) for x in ("kind", "system", "fault") if k.get(x)) + ("_" + k["config"] if k.get("config") else "") + (
            "_" + k["tag"] if k.get("tag") else "") + ("_" + k["ep"] if k.get("ep") else "")
        S.append(k)

    # ---- agents: controls and tool-level faults, both systems, with and without the timeout mitigation
    for system, configs in (("objeck", [("default", 0), ("limited", 100)]), ("python", [("naive", 0), ("guarded", 100)])):
        for cname, limit in configs:
            cfg = cname
            add(kind="agents", system=system, config=cfg, fault="none", limit=limit, tag="control")
            for f in ("tool_exception", "tool_fault", "tool_timeout", "tool_huge", "tool_hang", "cancel_coop", "cancel_uncoop"):
                dl = 6 if f == "tool_hang" else 8
                cap = 15 if f == "tool_hang" else None
                add(kind="agents", system=system, config=cfg, fault=f, limit=limit, deadline=dl, reps_cap=cap)
            for f in ("thread_fault", "thread_exit"):
                add(kind="agents", system=system, config=cfg, fault=f, limit=limit, runs=4000, agents=8, deadline=20, rate=0.0, reps_cap=10)
    # ---- agents against the loopback LLM server: model-call faults and network faults
    for system, configs in (("objeck", ["default"]), ("python", ["naive", "guarded"])):
        for cfg in configs:
            add(kind="agents", system=system, config=cfg, fault="none", http_control=True, tag="httpcontrol")
            for f in ("http500", "garbage", "truncjson", "wrongshape", "drop", "truncate", "rst"):
                add(kind="agents", system=system, config=cfg, fault=f, deadline=15)
            add(kind="agents", system=system, config=cfg, fault="delay", delay=2.0, deadline=20, reps_cap=15)
            add(kind="agents", system=system, config=cfg, fault="slowloris", trickle=0.01, deadline=30, reps_cap=10)
            add(kind="agents", system=system, config=cfg, fault="hold", hold=600.0, runs=4, deadline=80, reps_cap=8)
    # ---- inference
    for system in ("objeck", "python"):
        add(kind="infer", system=system, fault="none", ep="cpu", tag="control")
        for f in ("bad_shape", "close_inflight", "oom_burst", "thread_fault", "thread_exit"):
            add(kind="infer", system=system, fault=f, ep="cpu", iters=3000 if f.startswith("thread") else 300,
                reps_cap=10 if f == "oom_burst" else None)
    for f in ("none", "bad_shape", "close_inflight", "thread_fault"):
        add(kind="infer", system="objeck", fault=f, ep="cuda", iters=3000 if f.startswith("thread") else 300, tag="control" if f == "none" else "")
    add(kind="infer", system="objeck", fault="gpu_oom", ep="cuda", extra=["gpu_mem_limit=268435456"], reps_cap=10)
    # ---- process level
    for system, cfg in (("objeck", ""), ("python", "naive")):
        add(kind="process", system=system, config=cfg, fault="sigkill", prog="agents", reps_cap=10)
        add(kind="process", system=system, config=cfg, fault="sigterm", prog="agents", reps_cap=10)
        add(kind="process", system=system, config=cfg, fault="disk_full", prog="agents", reps_cap=10)
        add(kind="process", system=system, config=cfg, fault="stdin_closed", prog="agents", feed="", tag="empty", reps_cap=10)
        add(kind="process", system=system, config=cfg, fault="stdin_closed", prog="agents", feed="a\nb\n", tag="twolines", reps_cap=10)
        add(kind="process", system=system, config=cfg, fault="sigkill", prog="infer", tag="infer", ep="cpu", reps_cap=10)
        add(kind="process", system=system, config=cfg, fault="sigterm", prog="infer", tag="infer", ep="cpu", reps_cap=10)
    add(kind="process", system="objeck", fault="sigkill", prog="infer", tag="infer", ep="cuda", reps_cap=10)
    add(kind="process", system="objeck", fault="sigterm", prog="infer", tag="infer", ep="cuda", reps_cap=10)
    # ---- clock step on the wall clock, with a tool timeout mitigation in force
    for system, cfg in (("objeck", "limited"), ("python", "guarded")):
        for step in (3600, -3600):
            add(kind="agents", system=system, config=cfg, fault="clock_step", limit=100, step=step, tag="%+d" % step, runs=12, deadline=15, reps_cap=10)
    return S


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--only", default="", help="substring of the scenario id")
    ap.add_argument("--suite", default="")
    ap.add_argument("--reps", type=int, default=30)
    ap.add_argument("--seed", type=int, default=1000)
    ap.add_argument("--csv", default="results.csv")
    a = ap.parse_args()
    S = scenarios()
    if a.list:
        for s in S:
            print(s["id"])
        return
    sel = [s for s in S if (a.only in s["id"])] if a.only else (S if a.suite == "all" else [])
    if not sel:
        raise SystemExit("nothing selected (use --only or --suite all)")
    fields = None
    rows = []
    new = not os.path.exists(a.csv)
    with open(a.csv, "a", newline="") as f:
        w = None
        for sc in sel:
            reps = min(a.reps, sc["reps_cap"]) if sc.get("reps_cap") else a.reps
            for rep in range(reps):
                try:
                    row = run_scenario(sc, rep, a.seed)
                except Exception as e:  # a driver bug must not look like a result
                    row = {"scenario": sc["id"], "rep": rep, "driver_error": repr(e)[:200]}
                row["ts"] = int(time.time())
                if w is None:
                    fields = ["scenario", "system", "config", "fault", "kind", "ep", "rep", "seed", "rc", "watchdog_killed", "wall_s",
                              "summary_seen", "stuck", "finished", "units_done", "units_expected", "by_expected", "by_seen", "by_ok",
                              "by_ms_max", "by_ms_p95", "inj_expected", "inj_seen", "inj_lost", "inj_ok", "inj_detected", "inj_error",
                              "inj_recovered", "inj_absorbed", "inj_wrong", "inj_ms_median", "inj_ms_p95", "cancel_ms_median", "silent",
                              "inj_mismatch", "threads_peak", "threads_last", "fds_last", "rss_peak_mb", "leftover_procs", "load1",
                              "stderr_fatal", "stderr_head", "driver_error", "skip", "by_wrong", "by_refused", "vic_injected", "vic_clean",
                              "vic_wrong", "vic_refused", "all_ok_calls", "all_refused_calls", "closed_seen", "hog_last", "hog_blocks",
                              "gpu_blocks_held", "gpu_refused_at", "gpu_delta_mb", "gpu_mid_mb", "exit_after_signal_s", "units_before_signal",
                              "graceful", "restart_s", "restart_ok", "empty_reads", "served", "eof_detected", "disk_short", "disk_writes", "ts"]
                    w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
                    if new:
                        w.writeheader()
                w.writerow(row)
                f.flush()
                print("%s rep %d rc=%s %s" % (sc["id"], rep, row.get("rc"), row.get("driver_error", "")), flush=True)


if __name__ == "__main__":
    main()
