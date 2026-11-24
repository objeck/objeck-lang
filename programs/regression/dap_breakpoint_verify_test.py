"""DAP breakpoints are verified against the program, not trusted.

setBreakpoints used to answer verified:true for any line above zero. A breakpoint
on a comment or blank line, or in a program built without -debug, showed as a
solid red dot in the editor and never fired. Now a line is bound to the next line
that carries an instruction, or refused with a reason; and because the editor sets
breakpoints BEFORE the program is loaded, the adapter re-checks them at load and
sends `breakpoint` (changed) events for the ones that moved or cannot bind.

 1. breakpoints set before launch on a comment line move to the next executable
    line (event), the program stops there, and an executable line is left alone
 2. breakpoints set while the program is loaded: a comment line and a blank line
    snap forward; a line past the end of the file is refused with a message
 3. a program built without -debug: a console output event says so, and the
    breakpoint is reported unverified
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


def compile_fixture(work, name, text, debug=True):
    src = os.path.join(work, name + ".obs")
    obe = os.path.join(work, name + ".obe")
    with open(src, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    flags = ["-debug"] if debug else []
    rc = subprocess.run([OBC, "-src", src, "-dest", obe] + flags, cwd=BIN_DIR, env=make_env(),
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



# line numbers are what the checks below name
FIXTURE = """class DapBpVerify {
  function : Main(args : String[]) ~ Nil {
    # a comment line
    x := 1;

    x += 1;
    "end"->PrintLine();
  }
}
"""
COMMENT_LINE, FIRST_STMT, BLANK_LINE, SECOND_STMT = 3, 4, 5, 6


def collect(s, pred, timeout=10.0):
    m = s.wait_for(pred, timeout=timeout)
    return m


work = tempfile.mkdtemp(prefix="dap-bpverify-")
try:
    src, obe = compile_fixture(work, "bp_verify", FIXTURE)
    nsrc, nobe = compile_fixture(work, "bp_nodebug", FIXTURE.replace("DapBpVerify", "DapBpNoDebug"), debug=False)
    if not (obe and nobe):
        print("ERROR: could not compile the fixtures", file=sys.stderr)
        sys.exit(2)

    # ---- 1. set before the program is loaded (the usual editor order)
    s = Session()
    s.request("launch", {"program": obe, "sourceDir": work})
    resp = s.request("setBreakpoints", {"source": {"path": src},
                                        "breakpoints": [{"line": COMMENT_LINE}, {"line": SECOND_STMT}]})
    bps = resp.get("body", {}).get("breakpoints", []) if resp else []
    check("before load both breakpoints are accepted", len(bps) == 2 and all(b.get("verified") for b in bps),
          f"got: {bps}")
    comment_id = bps[0].get("id") if bps else None
    s.request("configurationDone")
    moved = s.wait_for(lambda m: m.get("event") == "breakpoint" and
                       m.get("body", {}).get("breakpoint", {}).get("id") == comment_id, timeout=10.0)
    mb = moved.get("body", {}).get("breakpoint", {}) if moved else {}
    check("the comment-line breakpoint is moved to the next executable line by an event",
          mb.get("verified") is True and mb.get("line") == FIRST_STMT, f"got: {moved}")
    stopped = s.wait_event("stopped", timeout=15.0)
    tid = stopped.get("body", {}).get("threadId", 1) if stopped else 1
    check("the program stops at the moved breakpoint's line",
          stopped is not None and s.top_frame(tid).get("line") == FIRST_STMT,
          f"got: {s.top_frame(tid) if stopped else None}")
    extra = s.wait_for(lambda m: m.get("event") == "breakpoint" and
                       m.get("body", {}).get("breakpoint", {}).get("line") == SECOND_STMT, timeout=0.5)
    check("a breakpoint already on an executable line is not reported changed", extra is None, f"got: {extra}")

    # ---- 2. set while the program is loaded (stopped)
    resp = s.request("setBreakpoints", {"source": {"path": src},
                                        "breakpoints": [{"line": COMMENT_LINE}, {"line": BLANK_LINE}, {"line": 100}]})
    bps = resp.get("body", {}).get("breakpoints", []) if resp else []
    check("three breakpoints are answered", len(bps) == 3, f"got: {bps}")
    if len(bps) == 3:
        check("a comment line snaps to the next executable line",
              bps[0].get("verified") is True and bps[0].get("line") == FIRST_STMT, f"got: {bps[0]}")
        check("a blank line snaps to the next executable line",
              bps[1].get("verified") is True and bps[1].get("line") == SECOND_STMT, f"got: {bps[1]}")
        check("a line past the end of the code is not verified, with a reason",
              bps[2].get("verified") is False and bps[2].get("message"), f"got: {bps[2]}")
    s.send("disconnect")
    try:
        s.proc.wait(timeout=8.0)
    except subprocess.TimeoutExpired:
        s.proc.kill()

    # ---- 3. no -debug
    s = Session()
    s.request("launch", {"program": nobe, "sourceDir": work})
    resp = s.request("setBreakpoints", {"source": {"path": nsrc}, "breakpoints": [{"line": FIRST_STMT}]})
    bps = resp.get("body", {}).get("breakpoints", []) if resp else []
    nid = bps[0].get("id") if bps else None
    s.request("configurationDone")
    note = s.wait_for(lambda m: m.get("event") == "output" and
                      "-debug" in m.get("body", {}).get("output", ""), timeout=10.0)
    check("a program built without -debug gets a console warning",
          note is not None and note.get("body", {}).get("category") == "console", f"got: {note}")
    bad = s.wait_for(lambda m: m.get("event") == "breakpoint" and
                     m.get("body", {}).get("breakpoint", {}).get("id") == nid, timeout=5.0)
    check("its breakpoint is reported unverified",
          bad is not None and bad.get("body", {}).get("breakpoint", {}).get("verified") is False, f"got: {bad}")
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
