"""The debuggee must not share the DAP stdin.

obd --dap reads protocol requests from stdin. The program it runs inherited the
same fd 0, so a program that reads the console competed with the protocol reader:
it could swallow the bytes of a request (the editor then waits forever for the
response) and the adapter could swallow what the program meant to read. Now fd 0
is the null device for the program.

 1. every request sent while a console-reading program runs is answered
 2. the program's console read sees end-of-input at once, not the protocol
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



STDIN_FIXTURE = """use System.IO;

class DapStdin {
  function : Main(args : String[]) ~ Nil {
    line := Console->ReadLine();
    if(line = Nil | line->Size() = 0) {
      "stdin-eof"->PrintLine();
    }
    else {
      "stdin-got-data"->PrintLine();
    };
    i := 0;
    while(i < 2000000000) {
      i += 1;
    };
  }
}
"""


def outputs(s):
    with s.lock:
        return "".join(m.get("body", {}).get("output", "") for m in s.events if m.get("event") == "output")


work = tempfile.mkdtemp(prefix="dap-stdin-")
try:
    src, obe = compile_fixture(work, "stdin_prog", STDIN_FIXTURE)
    if not obe:
        print("ERROR: could not compile the fixture", file=sys.stderr)
        sys.exit(2)

    s = Session()
    s.start(obe, work)
    # the program is now blocked in (or past) its console read; hammer the protocol
    answered = 0
    total = 40
    for _ in range(total):
        if s.request("threads", timeout=3.0) is not None:
            answered += 1
        else:
            break
    check("every request sent while the program reads the console is answered", answered == total,
          f"{answered} of {total} answered")

    deadline = time.time() + 10.0
    while "stdin-" not in outputs(s) and time.time() < deadline:
        time.sleep(0.05)
    out = outputs(s)
    check("the program's console read sees end-of-input", "stdin-eof" in out, f"output: {out!r}")
    s.send("disconnect")
    try:
        s.proc.wait(timeout=8.0)
    except subprocess.TimeoutExpired:
        s.proc.kill()
finally:
    shutil.rmtree(work, ignore_errors=True)

print("")
print("========================================")
print(f"  Results: {passed} passed, {failed} failed")
print("========================================")
sys.exit(1 if failed else 0)
