"""Shared harness for the dap_stepin_targets / dap_goto / dap_cancel_progress tests.

A scripted DAP client over stdio against `obd --dap`, and one small Objeck program
that has what those requests need: several calls on one line, a method that is a
straight run of statements, and output that shows where execution went.
Not a test itself (run_dap_tests.py does not list it).
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

# What the program does, and so what its output says about the path taken:
#   Main:  x := Left() + Right(Left())  ->  1 + 11 = 12
#   Jump:  total gets +1, +10, +100 on three lines; skipping a line skips its share
# Markers (the trailing "# NAME" comments) are looked up, so editing the program
# does not mean renumbering the tests.
FIXTURE = """class DapMore {
  function : Left() ~ Int {
    return 1;    # LEFT_BODY
  }

  function : Right(a : Int) ~ Int {
    r := a + 10;    # RIGHT_BODY
    return r;
  }

  function : Jump() ~ Int {
    total := 0;    # JUMP_INIT
    total += 1;    # JUMP_A
    total += 10;    # JUMP_B
    total += 100;    # JUMP_C
    return total;    # JUMP_RET
  }

  function : Main(args : String[]) ~ Nil {
    x := 0;    # MAIN_INIT
    x := Left() + Right(Left());    # MAIN_CALLS
    y := Jump();    # MAIN_JUMP
    "x={$x} y={$y}"->PrintLine();    # MAIN_PRINT
  }
}
"""


def line_of(marker, text=FIXTURE):
    """The 1-based line of the fixture carrying '# marker'."""
    for number, line in enumerate(text.splitlines(), 1):
        if line.rstrip().endswith("# " + marker):
            return number
    raise KeyError(marker)


class Results:
    def __init__(self):
        self.passed = 0
        self.failed = 0

    def check(self, name, ok, detail=""):
        if ok:
            self.passed += 1
            print(f"  [PASS] {name}")
        else:
            self.failed += 1
            print(f"  [FAIL] {name}: {detail}")

    def finish(self):
        print("")
        print("========================================")
        print(f"  Results: {self.passed} passed, {self.failed} failed")
        print("========================================")
        sys.exit(1 if self.failed else 0)


def make_env(extra=None):
    env = os.environ.copy()
    env["OBJECK_LIB_PATH"] = LIB_PATH + os.sep
    native = os.path.join(LIB_PATH, "native")
    if sys.platform == "darwin":
        env["DYLD_LIBRARY_PATH"] = native + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
    elif os.name != "nt":
        env["LD_LIBRARY_PATH"] = native + os.pathsep + env.get("LD_LIBRARY_PATH", "")
    if extra:
        env.update(extra)
    return env


def compile_fixture(work, name, text=FIXTURE):
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
    """One obd --dap session. 'init' are extra initialize arguments (the client's
    capabilities); 'env' extra environment for obd. Every message is kept in
    'log', in order, as well as being offered to the wait_* helpers."""

    def __init__(self, init=None, env=None):
        self.proc = subprocess.Popen(
            [OBD, "--dap"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            env=make_env(env), bufsize=0, cwd=BIN_DIR,
        )
        self.events = []
        self.log = []
        self.lock = threading.Lock()
        self.seq = 1
        self.last_seq = 0
        threading.Thread(target=self._reader, daemon=True).start()
        args = {"adapterID": "objeck"}
        if init:
            args.update(init)
        self.send("initialize", args)
        self.init_response = self.wait_response("initialize")

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
                self.log.append(m)

    def send(self, command, args=None):
        """Sends a request without waiting; returns its seq."""
        seq = self.seq
        msg = {"seq": seq, "type": "request", "command": command}
        self.seq += 1
        self.last_seq = seq
        if args is not None:
            msg["arguments"] = args
        body = json.dumps(msg).encode()
        try:
            self.proc.stdin.write(b"Content-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body)
            self.proc.stdin.flush()
        except OSError:
            pass
        return seq

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

    def wait_response_to(self, seq, timeout=10.0):
        return self.wait_for(lambda m: m.get("type") == "response" and m.get("request_seq") == seq, timeout)

    def wait_event(self, event, timeout=10.0):
        return self.wait_for(lambda m: m.get("event") == event, timeout)

    def request(self, command, args=None, timeout=10.0):
        seq = self.send(command, args)
        return self.wait_response_to(seq, timeout)

    def events_named(self, name):
        with self.lock:
            return [m for m in self.log if m.get("event") == name]

    def start(self, obe, work, src, lines):
        self.request("launch", {"program": obe, "sourceDir": work})
        self.request("setBreakpoints", {"source": {"path": src}, "breakpoints": [{"line": n} for n in lines]})
        self.request("configurationDone")

    def stop_at(self, timeout=15.0):
        """Waits for the next stopped event; returns (event body, top frame)."""
        stopped = self.wait_event("stopped", timeout=timeout)
        if not stopped:
            return None, {}
        body = stopped.get("body", {})
        return body, self.top_frame(body.get("threadId", 1))

    def top_frame(self, thread_id=1):
        st = self.request("stackTrace", {"threadId": thread_id})
        frames = st.get("body", {}).get("stackFrames", []) if st else []
        return frames[0] if frames else {}

    def frames(self, thread_id=1):
        st = self.request("stackTrace", {"threadId": thread_id})
        return st.get("body", {}).get("stackFrames", []) if st else []

    def local(self, name, frame_id):
        sc = self.request("scopes", {"frameId": frame_id})
        for scope in sc.get("body", {}).get("scopes", []) if sc else []:
            if scope["name"] == "Locals":
                vs = self.request("variables", {"variablesReference": scope["variablesReference"]})
                for v in vs.get("body", {}).get("variables", []) if vs else []:
                    if v["name"] == name:
                        return v["value"]
        return None

    def output_text(self):
        with self.lock:
            return "".join(m.get("body", {}).get("output", "") for m in self.log if m.get("event") == "output")

    def finish(self, timeout=20.0, thread_id=1):
        """Continues the stopped program and carries on past any further stop until
        it ends; returns whether it terminated."""
        self.request("continue", {"threadId": thread_id})
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.events_named("terminated"):
                return True
            stopped = self.wait_for(lambda m: m.get("event") in ("stopped", "terminated"), timeout=0.5)
            if stopped and stopped.get("event") == "stopped":
                self.request("continue", {"threadId": stopped["body"].get("threadId", 1)})
            elif stopped:
                return True
        return bool(self.events_named("terminated"))

    def close(self):
        self.send("disconnect")
        self.wait_response("disconnect", timeout=3.0)
        try:
            self.proc.wait(timeout=8.0)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            return None
        return self.proc.returncode
