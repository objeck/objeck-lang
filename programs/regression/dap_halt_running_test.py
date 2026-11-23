"""DAP disconnect / terminate / restart of a program that is RUNNING.

The debugger hook only looked at the disconnect flag in its "resume after a stop"
branch. A program with no breakpoint returns from the hook on its fast path, so a
running (spinning, sleeping or serving) program ignored disconnect and terminate,
and obd sat in vm_thread.join() until the editor killed it. Every check here waits
for obd to exit BY ITSELF: the process is never killed unless the check has failed.

 1. disconnect of a spinning program with no breakpoints ends obd
 2. terminate of a spinning program ends obd
 3. disconnect of a program asleep in a blocking call (no halt can reach it) still
    ends obd, via the watchdog, and the output printed before it arrives
 4. restart of a spinning program runs it again from the start
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



def exits_by_itself(s, timeout):
    """True if the obd process ends on its own within timeout; kills it otherwise."""
    try:
        s.proc.wait(timeout=timeout)
        return True
    except subprocess.TimeoutExpired:
        s.proc.kill()
        s.proc.wait()
        return False


SPIN_FIXTURE = """class DapHaltSpin {
  function : Main(args : String[]) ~ Nil {
    "started"->PrintLine();
    i := 0;
    while(i < 2000000000) {
      i += 1;
    };
  }
}
"""

SLEEP_FIXTURE = """use System.Concurrency;

class DapHaltSleep {
  function : Main(args : String[]) ~ Nil {
    "before-sleep"->PrintLine();
    Thread->Sleep(600000);
    "after-sleep"->PrintLine();
  }
}
"""


def outputs(s):
    with s.lock:
        return "".join(m.get("body", {}).get("output", "") for m in s.events if m.get("event") == "output")


work = tempfile.mkdtemp(prefix="dap-halt-")
try:
    spin_src, spin_obe = compile_fixture(work, "halt_spin", SPIN_FIXTURE)
    sleep_src, sleep_obe = compile_fixture(work, "halt_sleep", SLEEP_FIXTURE)
    if not (spin_obe and sleep_obe):
        print("ERROR: could not compile the fixtures", file=sys.stderr)
        sys.exit(2)

    # 1. disconnect while the program spins
    s = Session()
    s.start(spin_obe, work)
    time.sleep(1.0)
    s.send("disconnect")
    check("disconnect of a spinning program ends obd by itself", exits_by_itself(s, 8.0),
          "obd was still running 8 s after disconnect")

    # 2. terminate while the program spins
    s = Session()
    s.start(spin_obe, work)
    time.sleep(1.0)
    s.send("terminate")
    check("terminate of a spinning program ends obd by itself", exits_by_itself(s, 8.0),
          "obd was still running 8 s after terminate")

    # 3. disconnect while the program sleeps in a blocking call
    s = Session()
    s.start(sleep_obe, work)
    deadline = time.time() + 10.0
    while "before-sleep" not in outputs(s) and time.time() < deadline:
        time.sleep(0.05)
    time.sleep(0.5)
    t0 = time.time()
    s.send("disconnect")
    ok = exits_by_itself(s, 8.0)
    check("disconnect of a sleeping program ends obd by itself", ok,
          "obd was still running 8 s after disconnect")
    check("it does not take the whole sleep", time.time() - t0 < 8.0, f"took {time.time() - t0:.1f}s")

    # 4. restart while the program spins
    s = Session()
    s.start(spin_obe, work)
    deadline = time.time() + 10.0
    while outputs(s).count("started") < 1 and time.time() < deadline:
        time.sleep(0.05)
    check("the spinning program printed its first line", outputs(s).count("started") >= 1, outputs(s))
    s.send("restart")
    deadline = time.time() + 10.0
    while outputs(s).count("started") < 2 and time.time() < deadline:
        time.sleep(0.05)
    check("restart of a spinning program runs it again", outputs(s).count("started") >= 2,
          f"output: {outputs(s)!r}")
    s.send("disconnect")
    exits_by_itself(s, 8.0)
finally:
    shutil.rmtree(work, ignore_errors=True)

print("")
print("========================================")
print(f"  Results: {passed} passed, {failed} failed")
print("========================================")
sys.exit(1 if failed else 0)
