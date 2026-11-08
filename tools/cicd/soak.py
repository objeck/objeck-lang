#!/usr/bin/env python3
"""Soak test: the deploy tree under mixed, sustained load for a fixed time.

    python3 tools/cicd/soak.py --tree core/release/deploy-x64 --leg linux-x64 --arch x64 \
        --minutes 330 --seed 20261107 --out soak-results

Two things run together for the whole time, on one machine:

  * the objeck.org site server (programs/examples/site_server.obs, the program the
    droplet runs) serving docs/web on a local port, under a steady client load of
    whole-file GETs, Range requests, bad ranges, missing paths and traversal
    attempts. Every body is compared with the file on disk. The server's resident
    memory is sampled where the platform reports it.
  * a loop of iterations, each the regression suite, a GC stress loop in both JIT
    modes (nightly_triage.py loop) and a batch of the differential fuzzer with a fresh
    seed.

The soak stops at the first failure (a wrong or missing response, a dead server, a
failed step), writes what it saw to --out, and exits 1; a clean run exits 0. A new
iteration starts only if the previous one's length still fits in the time left, so
the run ends inside --minutes. Python standard library only.
"""

import argparse
import hashlib
import http.client
import json
import os
import random
import socket
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
IS_WINDOWS = os.name == "nt"
EXE = ".exe" if IS_WINDOWS else ""
SITE_LIBS = "net,net_server,json,cipher"  # what the droplet compiles site_server.obs with
PORT = 8123
RSS_LIMIT_MB = 1024  # the systemd unit's MemoryMax on the droplet (--rss-limit-mb overrides)
MAX_NET_ERROR_RATE = 0.002
# A healthy site server holds about 200 mappings, 30-60 descriptors and under 500 handles. The
# connection-thread leak added four mappings per connection; these catch that class of leak
# long before the process dies.
MAX_MAPPINGS = 5000
MAX_DESCRIPTORS = 4000
MAX_HANDLES = 10000


def now():
    return time.time()


def stamp():
    return time.strftime("%H:%M:%S", time.gmtime())


class Soak:
    def __init__(self, args):
        self.args = args
        self.tree = os.path.abspath(args.tree)
        self.bin = os.path.join(self.tree, "bin")
        self.out = os.path.abspath(args.out)
        os.makedirs(self.out, exist_ok=True)
        self.start = now()
        self.deadline = self.start + args.minutes * 60
        self.failure = None
        self.fail_lock = threading.Lock()
        self.stop = threading.Event()
        self.log_file = open(os.path.join(self.out, "soak.log"), "a", encoding="utf-8")
        self.env = dict(os.environ)
        lib = os.path.join(self.tree, "lib")
        self.env["OBJECK_LIB_PATH"] = lib
        native = os.path.join(lib, "native")
        for var in ("PATH", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"):
            self.env[var] = native + os.pathsep + self.env.get(var, "")
        self.stats = {"requests": 0, "net_errors": 0, "bad": 0, "iterations": 0,
                      "rss_mb": [], "samples": [], "steps": []}
        self.stats_lock = threading.Lock()

    def log(self, msg):
        line = "[%s +%4dm] %s" % (stamp(), int((now() - self.start) / 60), msg)
        print(line, flush=True)
        self.log_file.write(line + "\n")
        self.log_file.flush()

    def fail(self, what):
        with self.fail_lock:
            if self.failure is None:
                self.failure = what
                self.log("FAIL: " + what)
        self.stop.set()

    # ------------------------------------------------------------------ site server

    def start_site(self):
        src = os.path.join(ROOT, "programs", "examples", "site_server.obs")
        work = os.path.join(self.out, "site-work")
        os.makedirs(work, exist_ok=True)
        exe = os.path.join(work, "site_server.obe")
        p = subprocess.run([os.path.join(self.bin, "obc" + EXE), "-src", src, "-lib", SITE_LIBS,
                            "-dest", exe], env=self.env, cwd=work, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=300)
        if p.returncode != 0 or not os.path.exists(exe):
            self.fail("site_server.obs did not compile: " + p.stdout.decode("utf-8", "replace")[-500:])
            return False
        self.web = os.path.join(ROOT, "docs", "web")
        self.server_out = open(os.path.join(self.out, "site_server.out"), "ab")
        self.server = subprocess.Popen([os.path.join(self.bin, "obr" + EXE), exe, self.web, str(PORT),
                                        "127.0.0.1"], env=self.env, cwd=work,
                                       stdout=self.server_out, stderr=subprocess.STDOUT)
        for _ in range(100):
            if self.server.poll() is not None:
                self.fail("site server exited at startup, code %s" % self.server.returncode)
                return False
            try:
                socket.create_connection(("127.0.0.1", PORT), 1).close()
                break
            except OSError:
                time.sleep(0.3)
        else:
            self.fail("site server never listened on %d" % PORT)
            return False
        # What the clients compare against: every file's bytes, read once.
        self.files = {}
        for base, _dirs, names in os.walk(self.web):
            for n in names:
                full = os.path.join(base, n)
                rel = "/" + os.path.relpath(full, self.web).replace(os.sep, "/")
                if n.startswith(".") or os.path.getsize(full) > 8 * 1024 * 1024:
                    continue
                with open(full, "rb") as f:
                    self.files[rel] = f.read()
        self.log("site server up, %d files from %s" % (len(self.files), self.web))
        return True

    def request(self, conn, method, path, headers=None):
        conn.request(method, path, headers=headers or {})
        r = conn.getresponse()
        body = r.read()
        return r.status, body, r

    def client(self, wid):
        rng = random.Random(self.args.seed * 1000 + wid)
        paths = sorted(self.files)
        conn = None
        while not self.stop.is_set():
            try:
                if conn is None or rng.random() < 0.3:
                    if conn:
                        conn.close()
                    conn = http.client.HTTPConnection("127.0.0.1", PORT, timeout=20)
                op = rng.random()
                path = rng.choice(paths)
                data = self.files[path]
                if op < 0.55:
                    status, body, _ = self.request(conn, "GET", path)
                    ok = status == 200 and body == data
                    what = "GET %s -> %s (%d bytes, want %d)" % (path, status, len(body), len(data))
                elif op < 0.80 and len(data) > 2:
                    a = rng.randrange(0, len(data) - 1)
                    b = rng.randrange(a, len(data))
                    status, body, _ = self.request(conn, "GET", path, {"Range": "bytes=%d-%d" % (a, b)})
                    ok = status == 206 and body == data[a:b + 1]
                    what = "Range %d-%d of %s -> %s (%d bytes)" % (a, b, path, status, len(body))
                elif op < 0.88:
                    status, body, _ = self.request(conn, "GET", path, {"Range": "bytes=%d-" % (len(data) + 10)})
                    ok = status == 416
                    what = "unsatisfiable Range on %s -> %s" % (path, status)
                elif op < 0.94:
                    status, body, _ = self.request(conn, "GET", "/no/such/page-%d.html" % rng.randrange(10**6))
                    ok = status == 404
                    what = "missing path -> %s" % status
                else:
                    status, body, _ = self.request(conn, "GET", "/../../etc/passwd")
                    ok = status in (400, 403, 404)
                    what = "traversal -> %s" % status
                with self.stats_lock:
                    self.stats["requests"] += 1
                    if not ok:
                        self.stats["bad"] += 1
                if not ok:
                    self.fail("wrong response: " + what)
                    return
                time.sleep(rng.random() * 0.05)
            except (OSError, http.client.HTTPException) as e:
                with self.stats_lock:
                    self.stats["requests"] += 1
                    self.stats["net_errors"] += 1
                if conn:
                    conn.close()
                conn = None
                time.sleep(0.2)

    def sample(self):
        """The site server's resources: memory, threads, and mappings/descriptors (Linux)
        or handles (Windows). A key is left out where the platform does not report it."""
        pid = self.server.pid
        out = {}
        if IS_WINDOWS:
            try:
                cmd = "$p = Get-Process -Id %d; '{0} {1} {2}' -f [int]($p.WorkingSet64 / 1MB), $p.HandleCount, $p.Threads.Count" % pid
                p = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", cmd],
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=30)
                parts = p.stdout.decode("utf-8", "replace").split()
                if len(parts) == 3:
                    out = {"rss_mb": int(parts[0]), "handles": int(parts[1]), "threads": int(parts[2])}
            except (OSError, subprocess.TimeoutExpired, ValueError):
                pass
            return out
        if sys.platform == "darwin":
            return out
        try:
            with open("/proc/%d/status" % pid, encoding="utf-8") as f:
                for line in f:
                    if line.startswith("VmRSS:"):
                        out["rss_mb"] = int(line.split()[1]) // 1024
                    elif line.startswith("Threads:"):
                        out["threads"] = int(line.split()[1])
            out["descriptors"] = len(os.listdir("/proc/%d/fd" % pid))
            with open("/proc/%d/maps" % pid, "rb") as f:
                out["mappings"] = sum(1 for _ in f)
        except OSError:
            pass
        return out

    def watch(self):
        """Server liveness and resources, every 30 seconds."""
        limits = (("rss_mb", self.args.rss_limit_mb, "resident memory (MB)"),
                  ("mappings", MAX_MAPPINGS, "memory mappings"),
                  ("descriptors", MAX_DESCRIPTORS, "file descriptors"),
                  ("handles", MAX_HANDLES, "handles"))
        while not self.stop.wait(30):
            if self.server.poll() is not None:
                self.fail("site server died, exit code %s" % self.server.returncode)
                return
            snap = self.sample()
            if not snap:
                continue
            with self.stats_lock:
                self.stats["samples"].append(dict(snap, minute=int((now() - self.start) / 60)))
                if "rss_mb" in snap:
                    self.stats["rss_mb"].append(snap["rss_mb"])
            for key, limit, what in limits:
                if key in snap and snap[key] > limit:
                    self.fail("site server %s is %d, past the limit of %d" % (what, snap[key], limit))
                    return

    # ------------------------------------------------------------------ iterations

    def step(self, name, cmd, cwd, timeout, env_extra=None):
        t0 = now()
        env = dict(self.env)
        env.update(env_extra or {})
        log_path = os.path.join(self.out, "iter%03d-%s.log" % (self.stats["iterations"] + 1, name))
        self.log("step %s: %s" % (name, " ".join(cmd)))
        try:
            with open(log_path, "wb") as f:
                p = subprocess.run(cmd, cwd=cwd, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=timeout)
            rc = p.returncode
        except subprocess.TimeoutExpired:
            rc = -999
        took = now() - t0
        self.stats["steps"].append({"step": name, "iteration": self.stats["iterations"] + 1,
                                    "rc": rc, "seconds": round(took)})
        self.log("step %s: rc=%s in %d s" % (name, rc, took))
        if rc != 0:
            tail = ""
            try:
                with open(log_path, "rb") as f:
                    tail = f.read()[-1500:].decode("utf-8", "replace")
            except OSError:
                pass
            self.fail("%s failed (rc=%s) in iteration %d; log tail:\n%s" %
                      (name, rc, self.stats["iterations"] + 1, tail))
        return rc == 0

    def iteration(self):
        a = self.args
        reg = os.path.join(ROOT, "programs", "regression")
        runner = ["cmd", "/d", "/c", "run_regression.cmd", a.arch] if IS_WINDOWS else ["bash", "run_regression.sh", a.arch]
        seed = a.seed + self.stats["iterations"] + 1
        steps = [
            ("regression", runner, reg, 3600, {"TEST_TIMEOUT": "300"}),
            ("gc-stress", [sys.executable, os.path.join(ROOT, "tools", "cicd", "nightly_triage.py"), "loop",
                           "--leg", a.leg, "--out", os.path.join(self.out, "stress"), "--bin", self.bin,
                           "--src-dir", os.path.join(ROOT, "programs", "regression"),
                           "--tests", "minor_gc_stress,core_thread_gc_stress,jit_gc_stress",
                           "--runs", "2", "--modes", "off,1", "--timeout", "300", "--budget", "1800"],
             ROOT, 2400, None),
            ("fuzz", [sys.executable, os.path.join(ROOT, "tools", "fuzz", "run_fuzz.py"), "--bin", self.bin,
                      "--leg", a.leg, "--count", "60", "--seed", str(seed), "-j", "2",
                      "--out", os.path.join(self.out, "fuzz-out")], ROOT, 2400, None),
        ]
        for name, cmd, cwd, timeout, extra in steps:
            if self.stop.is_set() or not self.step(name, cmd, cwd, timeout, extra):
                return False
        self.stats["iterations"] += 1
        return True

    def run(self):
        a = self.args
        self.log("soak start: leg=%s tree=%s minutes=%d seed=%d" % (a.leg, self.tree, a.minutes, a.seed))
        if not self.start_site():
            return self.finish()
        threads = [threading.Thread(target=self.client, args=(i,), daemon=True) for i in range(a.clients)]
        threads.append(threading.Thread(target=self.watch, daemon=True))
        for t in threads:
            t.start()
        last = 0.0
        while not self.stop.is_set() and not a.site_only:
            left = self.deadline - now()
            if left < max(last * 1.15, 60):
                self.log("%d s left, less than an iteration (%d s): stopping" % (left, last))
                break
            t0 = now()
            if not self.iteration():
                break
            last = now() - t0
            self.log("iteration %d done in %d s" % (self.stats["iterations"], last))
        # Whatever is left of the time runs the site load alone.
        while not self.stop.is_set() and now() < self.deadline:
            self.stop.wait(min(30, max(1, self.deadline - now())))
        return self.finish()

    def finish(self):
        self.stop.set()
        try:
            self.server.terminate()
            self.server.wait(10)
        except Exception:
            pass
        with self.stats_lock:
            s = self.stats
            ops, errs = s["requests"], s["net_errors"]
            if self.failure is None and ops < 1000:
                self.fail("only %d site requests were made" % ops)
            if self.failure is None and errs / max(ops, 1) > MAX_NET_ERROR_RATE:
                self.fail("%d of %d site requests failed at the network level (limit %.1f%%)" %
                          (errs, ops, MAX_NET_ERROR_RATE * 100))
            rss = s["rss_mb"]
        summary = {"leg": self.args.leg, "minutes_run": round((now() - self.start) / 60),
                   "minutes_planned": self.args.minutes, "iterations": s["iterations"],
                   "site_requests": ops, "site_net_errors": errs, "site_bad": s["bad"],
                   "rss_mb_first": rss[0] if rss else None, "rss_mb_max": max(rss) if rss else None,
                   "rss_mb_last": rss[-1] if rss else None, "rss_mb_series": rss, "resource_samples": s["samples"], "steps": s["steps"],
                   "result": "FAIL" if self.failure else "PASS", "failure": self.failure}
        with open(os.path.join(self.out, "soak-summary.json"), "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=2)
        self.log("%s: %d iterations, %d site requests (%d network errors, %d bad), rss first/max/last %s/%s/%s MB"
                 % (summary["result"], s["iterations"], ops, errs, s["bad"],
                    summary["rss_mb_first"], summary["rss_mb_max"], summary["rss_mb_last"]))
        peaks = {}
        for snap in s["samples"]:
            for key in ("threads", "mappings", "descriptors", "handles"):
                if key in snap:
                    peaks.setdefault(key, [snap[key], snap[key]])
                    peaks[key][1] = max(peaks[key][1], snap[key])
        if peaks:
            self.log("site server resources first/peak: " +
                     ", ".join("%s %d/%d" % (k, v[0], v[1]) for k, v in sorted(peaks.items())))
        return 1 if self.failure else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", required=True, help="deploy tree (bin/, lib/)")
    ap.add_argument("--leg", required=True, help="platform label, e.g. linux-x64")
    ap.add_argument("--arch", required=True, help="x64, arm64 or ARM64: the run_regression argument")
    ap.add_argument("--minutes", type=int, default=330)
    ap.add_argument("--seed", type=int, default=int(time.strftime("%Y%m%d", time.gmtime())))
    ap.add_argument("--clients", type=int, default=4, help="concurrent site clients")
    ap.add_argument("--rss-limit-mb", type=int, default=RSS_LIMIT_MB, help="fail if the site server passes this")
    ap.add_argument("--site-only", action="store_true", help="site load only, no iterations")
    ap.add_argument("--out", default="soak-results")
    sys.exit(Soak(ap.parse_args()).run())


if __name__ == "__main__":
    main()
