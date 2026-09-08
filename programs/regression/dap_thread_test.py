"""Threads over DAP.

A spawned VM thread used to run with no debugger attached, so a breakpoint
inside a thread body never fired and the adapter answered every 'threads'
request with a fabricated {id: 1, "Main Thread"}. Each test fails on that build.

 1. a breakpoint inside Worker::Run produces a stopped event
 2. the stopped event names a real thread, not the hardcoded 1-or-nothing
 3. 'threads' lists more than one thread and includes the stopped one
 4. stackTrace for the stopped thread shows Run at the top
 5. evaluate in that frame yields a worker's own value
 6. allThreadsStopped is reported -- and is now actually true
"""


import json
import os
import subprocess
import sys
import threading
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
PLATFORM = os.environ.get("DAP_TEST_PLATFORM", "x64")

# A runner that already knows the deploy tree passes it down rather than
# letting each test guess; the probe below is the fallback for a direct run.
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

OBD = os.path.join(DEPLOY_DIR, "bin", "obd.exe" if os.name == "nt" else "obd")
LIB_PATH = os.path.join(DEPLOY_DIR, "lib")
SRC_DIR = SCRIPT_DIR
SRC_FILE = os.path.join(SRC_DIR, "debugger_thread_test.obs")
PROG = os.path.join(SRC_DIR, "debugger_thread_test.obe")

BREAKPOINT_LINE = 38     # `@total += step;` inside Worker::Run, hit by three threads

if not os.path.exists(PROG):
    print(f"ERROR: {PROG} not found - run run_dap_tests.sh first to build it", file=sys.stderr)
    sys.exit(2)

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


class Session:
    """One obd --dap session."""

    def __init__(self, client_caps=None):
        env = os.environ.copy()
        env["OBJECK_LIB_PATH"] = LIB_PATH
        self.proc = subprocess.Popen(
            [OBD, "--dap"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            env=env, bufsize=0,
        )
        self.events = []
        self.lock = threading.Lock()
        self.seq = 1
        threading.Thread(target=self._reader, daemon=True).start()

        args = {"adapterID": "objeck"}
        if client_caps:
            args.update(client_caps)
        self.send("initialize", args)
        self.wait_response("initialize")

    def _read_msg(self):
        header = b""
        while not header.endswith(b"\r\n\r\n"):
            c = self.proc.stdout.read(1)
            if not c:
                return None
            header += c
        length = int(header.decode().split("Content-Length: ")[1].split("\r\n")[0])
        return json.loads(self.proc.stdout.read(length).decode("utf-8"))

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
            time.sleep(0.05)
        return None

    def wait_response(self, command, timeout=10.0):
        return self.wait_for(lambda m: m.get("command") == command and m.get("type") == "response", timeout)

    def wait_event(self, event, timeout=10.0):
        return self.wait_for(lambda m: m.get("event") == event, timeout)

    def request(self, command, args=None, timeout=10.0):
        self.send(command, args)
        return self.wait_response(command, timeout)

    def run_to_breakpoint(self, line=BREAKPOINT_LINE):
        self.request("launch", {"program": PROG, "sourceDir": SRC_DIR})
        self.request("setBreakpoints", {"source": {"path": SRC_FILE}, "breakpoints": [{"line": line}]})
        self.request("configurationDone")
        return self.wait_event("stopped", timeout=20.0)

    def locals_ref(self):
        st = self.request("stackTrace", {"threadId": 1})
        frame_id = st["body"]["stackFrames"][0]["id"]
        sc = self.request("scopes", {"frameId": frame_id})
        for s in sc["body"]["scopes"]:
            if s["name"] == "Locals":
                return s["variablesReference"]
        return 0

    def variables(self, ref, extra=None):
        args = {"variablesReference": ref}
        if extra:
            args.update(extra)
        resp = self.request("variables", args)
        return resp.get("body", {}).get("variables", []) if resp else []

    def close(self):
        self.send("disconnect")
        self.wait_response("disconnect", timeout=3.0)
        try:
            self.proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            self.proc.kill()




s = Session()
stopped = s.run_to_breakpoint()
check("breakpoint inside a thread's Run method produces a stopped event",
      stopped is not None, "no stopped event: the thread ran with no debugger attached")

if stopped:
    sbody = stopped.get("body", {})
    tid = sbody.get("threadId")
    check("stopped event carries a thread id", isinstance(tid, int) and tid > 0, f"got: {sbody}")
    check("stopped event reports allThreadsStopped", sbody.get("allThreadsStopped") is True, f"got: {sbody}")

    resp = s.request("threads")
    threads = resp.get("body", {}).get("threads", []) if resp else []
    ids = [t.get("id") for t in threads]
    check("threads lists more than the one fabricated thread", len(threads) > 1,
          f"got: {threads} (a single 'Main Thread' means the old hardcoded answer)")
    check("the stopped thread is among the listed threads", tid in ids, f"stopped={tid} listed={ids}")
    check("a listed thread is named for Worker::Run",
          any("Worker->Run" in str(t.get("name", "")) for t in threads), f"got: {threads}")

    st = s.request("stackTrace", {"threadId": tid})
    frames = st.get("body", {}).get("stackFrames", []) if st else []
    top = frames[0].get("name", "") if frames else ""
    check("stackTrace for the stopped thread has Run on top", "Run" in top, f"got: {top!r}")

    resp = s.request("evaluate", {"expression": "step", "context": "watch"})
    got = resp.get("body", {}).get("result") if resp else None
    check("evaluate in the thread's frame yields a worker's own value", got in ("100", "200", "300"),
          f"got: {got}")

    s.request("continue", {"threadId": tid})
s.close()

print("")
print("========================================")
print(f"  Results: {passed} passed, {failed} failed")
print("========================================")
sys.exit(1 if failed else 0)
