"""DAP pause, and the debugger's per-instruction fast path.

Every VM thread passes through the debugger's hook on every instruction. The hook
used to take a global lock each time, and 'pause' answered an error because
nothing could interrupt a running program. Now the hook has a lock-free path for
"nothing to do", and a pause is a flag the first thread to run an instruction with
a source position takes. Each test fails on the previous adapter.

 1. pause stops a running program with reason "pause", on a real thread
 2. the stopped program is where it was running: a source line, and its loop variable
 3. the program runs on after continue and a second pause finds it further along
 4. pause is refused while stopped, and before the program runs
 5. pause stops a threaded program whose main thread is parked in Join(): the
    stop is on a worker, and every thread is listed
 6. a breakpoint set while the program runs (the fast path was off) takes effect
 7. a breakpoint removed while the program runs lets it run on to the end
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
PLATFORM = os.environ.get("DAP_TEST_PLATFORM", "x64")

DEPLOY_DIR = None
for candidate in [
    os.path.abspath(p) for p in [os.environ.get("DAP_TEST_DEPLOY_DIR")] if p
] + [
    os.path.join(REPO_ROOT, "core", "release", f"deploy-{PLATFORM}"),
    os.path.join(REPO_ROOT, "core", "release", "deploy"),
]:
    if os.path.isdir(candidate):
        DEPLOY_DIR = candidate
        break
if DEPLOY_DIR is None:
    print("ERROR: could not find deploy directory under core/release/", file=sys.stderr)
    sys.exit(2)

EXE = ".exe" if os.name == "nt" else ""
BIN_DIR = os.path.join(DEPLOY_DIR, "bin")
OBD = os.path.join(BIN_DIR, "obd" + EXE)
OBC = os.path.join(BIN_DIR, "obc" + EXE)
LIB_PATH = os.path.join(DEPLOY_DIR, "lib")

passed = 0
failed = 0


def check(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
        print(f"  [PASS] {name}")
    else:
        failed += 1
        print(f"  [FAIL] {name}: {detail}")


def make_env():
    env = os.environ.copy()
    env["OBJECK_LIB_PATH"] = LIB_PATH + os.sep
    native = os.path.join(LIB_PATH, "native")
    if sys.platform == "darwin":
        env["DYLD_LIBRARY_PATH"] = native + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
    elif os.name != "nt":
        env["LD_LIBRARY_PATH"] = native + os.pathsep + env.get("LD_LIBRARY_PATH", "")
    return env


def compile_fixture(work, name, text):
    src = os.path.join(work, name + ".obs")
    obe = os.path.join(work, name + ".obe")
    with open(src, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    rc = subprocess.run([OBC, "-src", src, "-dest", obe, "-debug"], cwd=BIN_DIR, env=make_env(),
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT).returncode
    if rc != 0 or not os.path.exists(obe):
        return src, None
    return src, obe


class Session:
    """One obd --dap session."""

    def __init__(self):
        self.proc = subprocess.Popen(
            [OBD, "--dap"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            env=make_env(), bufsize=0, cwd=BIN_DIR,
        )
        self.events = []
        self.lock = threading.Lock()
        self.seq = 1
        threading.Thread(target=self._reader, daemon=True).start()
        self.send("initialize", {"adapterID": "objeck"})
        self.wait_response("initialize")

    def _read_msg(self):
        header = b""
        while not header.endswith(b"\r\n\r\n"):
            c = self.proc.stdout.read(1)
            if not c:
                return None
            header += c
        length = int(header.decode().split("Content-Length: ")[1].split("\r\n")[0])
        body = b""
        while len(body) < length:
            chunk = self.proc.stdout.read(length - len(body))
            if not chunk:
                return None
            body += chunk
        return json.loads(body.decode("utf-8"))

    def _reader(self):
        while True:
            m = self._read_msg()
            if m is None:
                return
            with self.lock:
                self.events.append(m)

    def send(self, command, args=None):
        msg = {"seq": self.seq, "type": "request", "command": command}
        self.seq += 1
        if args is not None:
            msg["arguments"] = args
        body = json.dumps(msg).encode()
        try:
            self.proc.stdin.write(b"Content-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body)
            self.proc.stdin.flush()
        except OSError:
            pass

    def wait_for(self, predicate, timeout=10.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self.lock:
                for i, m in enumerate(self.events):
                    if predicate(m):
                        self.events.pop(i)
                        return m
            time.sleep(0.02)
        return None

    def wait_response(self, command, timeout=10.0):
        return self.wait_for(lambda m: m.get("command") == command and m.get("type") == "response", timeout)

    def wait_event(self, event, timeout=10.0):
        return self.wait_for(lambda m: m.get("event") == event, timeout)

    def request(self, command, args=None, timeout=10.0):
        self.send(command, args)
        return self.wait_response(command, timeout)

    def start(self, obe, work, breakpoints=None, src=None):
        self.request("launch", {"program": obe, "sourceDir": work})
        if breakpoints is not None:
            self.request("setBreakpoints", {"source": {"path": src}, "breakpoints": breakpoints})
        self.request("configurationDone")

    def top_frame(self, thread_id):
        st = self.request("stackTrace", {"threadId": thread_id})
        frames = st.get("body", {}).get("stackFrames", []) if st else []
        return frames[0] if frames else {}

    def local(self, name, frame_id):
        sc = self.request("scopes", {"frameId": frame_id})
        for scope in sc.get("body", {}).get("scopes", []) if sc else []:
            if scope["name"] == "Locals":
                vs = self.request("variables", {"variablesReference": scope["variablesReference"]})
                for v in vs.get("body", {}).get("variables", []) if vs else []:
                    if v["name"] == name:
                        return v["value"]
        return None

    def close(self):
        self.send("disconnect")
        self.wait_response("disconnect", timeout=3.0)
        try:
            self.proc.wait(timeout=8.0)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        return self.proc.returncode


# A loop long enough that it cannot finish during the test; disconnect ends it
SPIN_FIXTURE = """class DapPauseSpin {
  function : Main(args : String[]) ~ Nil {
    i := 0;
    while(i < 2000000000) {
      i += 1;
    };
  }
}
"""
SPIN_LINE = 5

THREADS_FIXTURE = """use System.Concurrency;

class Spinner from Thread {
  New(id : Int) {
    Parent("spinner-{$id}");
  }

  method : public : Run(param : System.Base) ~ Nil {
    n := 0;
    while(n < 2000000000) {
      n += 1;
    };
  }
}

class DapPauseThreads {
  function : Main(args : String[]) ~ Nil {
    workers := Spinner->New[3];
    each(i : workers) {
      workers[i] := Spinner->New(i + 1);
    };
    each(i : workers) {
      workers[i]->Execute(Nil);
    };
    each(i : workers) {
      workers[i]->Join();
    };
  }
}
"""

# ends by itself, quickly, once the loop is let go
SHORT_FIXTURE = """class DapPauseShort {
  function : Main(args : String[]) ~ Nil {
    i := 0;
    while(i < 5000000) {
      i += 1;
    };
    "done"->PrintLine();
  }
}
"""

work = tempfile.mkdtemp(prefix="dap-pause-")
try:
    spin_src, spin_obe = compile_fixture(work, "pause_spin", SPIN_FIXTURE)
    thr_src, thr_obe = compile_fixture(work, "pause_threads", THREADS_FIXTURE)
    short_src, short_obe = compile_fixture(work, "pause_short", SHORT_FIXTURE)
    if not (spin_obe and thr_obe and short_obe):
        print("ERROR: could not compile the fixtures", file=sys.stderr)
        sys.exit(2)

    # =======================================================================
    # Tests 1-4: pause a single-threaded loop
    # =======================================================================
    s = Session()
    resp = s.request("pause", {"threadId": 1})
    check("pause before the program runs is refused", resp is not None and resp.get("success") is False,
          f"got: {resp}")

    s.start(spin_obe, work)
    time.sleep(1.0)
    resp = s.request("pause", {"threadId": 1})
    check("pause is accepted while the program runs", resp is not None and resp.get("success") is True,
          f"got: {resp}")
    stopped = s.wait_event("stopped", timeout=15.0)
    body = stopped.get("body", {}) if stopped else {}
    check("the program stops with reason pause", body.get("reason") == "pause", f"got: {stopped}")
    check("every thread is reported stopped", body.get("allThreadsStopped") is True, f"got: {body}")

    tid = body.get("threadId", 1)
    frame = s.top_frame(tid)
    check("the stop is at a source line of the loop",
          frame.get("line", 0) > 0 and "DapPauseSpin" in frame.get("name", ""), f"got: {frame}")
    first = s.local("i", frame.get("id", 0))
    check("the loop variable shows the program was running", first is not None and int(first) > 0,
          f"i={first}")

    resp = s.request("pause", {"threadId": tid})
    check("pause is refused while already stopped", resp is not None and resp.get("success") is False,
          f"got: {resp}")

    s.request("continue", {"threadId": tid})
    time.sleep(0.5)
    s.request("pause", {"threadId": tid})
    stopped2 = s.wait_event("stopped", timeout=15.0)
    check("a second pause stops it again", stopped2 is not None and
          stopped2.get("body", {}).get("reason") == "pause", f"got: {stopped2}")
    if stopped2:
        frame2 = s.top_frame(stopped2["body"].get("threadId", 1))
        second = s.local("i", frame2.get("id", 0))
        check("the program ran on between the two pauses",
              first is not None and second is not None and int(second) > int(first),
              f"first={first} second={second}")
    s.close()

    # =======================================================================
    # Test 5: threads, main parked in Join()
    # =======================================================================
    s = Session()
    s.start(thr_obe, work)
    time.sleep(1.5)
    s.request("pause", {"threadId": 1})
    stopped = s.wait_event("stopped", timeout=15.0)
    body = stopped.get("body", {}) if stopped else {}
    check("a threaded program stops on pause", body.get("reason") == "pause", f"got: {stopped}")
    if stopped:
        tid = body.get("threadId", 1)
        frame = s.top_frame(tid)
        check("the stop is in a worker's Run, not in the thread parked in Join",
              "Spinner" in frame.get("name", "") and "Run" in frame.get("name", ""), f"got: {frame}")
        resp = s.request("threads")
        threads = resp.get("body", {}).get("threads", []) if resp else []
        check("every thread is listed", len(threads) >= 4 and tid in [t.get("id") for t in threads],
              f"got: {threads}")
    s.close()

    # =======================================================================
    # Tests 6 + 7: a breakpoint set and cleared while the program runs
    # =======================================================================
    s = Session()
    s.start(spin_obe, work)
    time.sleep(1.0)
    resp = s.request("setBreakpoints", {"source": {"path": spin_src}, "breakpoints": [{"line": SPIN_LINE}]})
    bps = resp.get("body", {}).get("breakpoints", []) if resp else []
    check("a breakpoint set while the program runs is verified",
          len(bps) == 1 and bps[0].get("verified") is True, f"got: {bps}")
    stopped = s.wait_event("stopped", timeout=15.0)
    check("it stops at the breakpoint", stopped is not None and
          stopped.get("body", {}).get("reason") == "breakpoint", f"got: {stopped}")
    s.close()

    s = Session()
    s.start(short_obe, work, breakpoints=[{"line": 5}], src=short_src)
    stopped = s.wait_event("stopped", timeout=15.0)
    check("the short program stops at its breakpoint", stopped is not None, "never stopped")
    if stopped:
        # remove the breakpoint and let it go: with nothing left to act on, it runs to the end
        s.request("setBreakpoints", {"source": {"path": short_src}, "breakpoints": []})
        s.request("continue", {"threadId": stopped["body"].get("threadId", 1)})
        ended = s.wait_for(lambda m: m.get("event") in ("stopped", "terminated"), timeout=60.0)
        check("with the breakpoint removed it runs to the end",
              ended is not None and ended.get("event") == "terminated", f"got: {ended}")
    s.close()
finally:
    shutil.rmtree(work, ignore_errors=True)

print("")
print("========================================")
print(f"  Results: {passed} passed, {failed} failed")
print("========================================")
sys.exit(1 if failed else 0)
