"""DAP breakpoints are keyed by path, and have ids of their own.

Breakpoints were keyed by base name, so one set in src/b/util.obs also stopped the
program in src/a/util.obs, and the id the adapter returned was the line number, so
two files' breakpoints on one line shared an id. Each test fails on that adapter.

 1. a breakpoint in one of two same-named files stops only in that file
 2. the stack frame it stops in names that file, not the other one
 3. breakpoints on the same line of both files stop once in each, in order
 4. ids are unique across files and lines: the same line of two files has two ids
 5. setting a file's breakpoints again keeps the id of a line that stays, and a
    line that is new gets an id that was never used
 6. a path that does not exist on this machine still matches by base name
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


def same_file(a, b):
    def norm(p):
        return os.path.normcase(os.path.realpath(p))
    return bool(a) and bool(b) and norm(a) == norm(b)


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

    def set_breakpoints(self, path, lines):
        resp = self.request("setBreakpoints", {"source": {"path": path}, "breakpoints": [{"line": l} for l in lines]})
        return resp.get("body", {}).get("breakpoints", []) if resp else []

    def next_stop(self, timeout=20.0):
        """The next stopped or terminated event, and the top frame if it stopped."""
        ev = self.wait_for(lambda m: m.get("event") in ("stopped", "terminated"), timeout)
        if ev is None or ev.get("event") != "stopped":
            return ev, {}
        st = self.request("stackTrace", {"threadId": ev["body"].get("threadId", 1)})
        frames = st.get("body", {}).get("stackFrames", []) if st else []
        return ev, (frames[0] if frames else {})

    def close(self):
        self.send("disconnect")
        self.wait_response("disconnect", timeout=3.0)
        try:
            self.proc.wait(timeout=8.0)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        return self.proc.returncode


# two files with the same base name, the same layout, different classes
UTIL_TEMPLATE = """class Util%s {
  function : Run() ~ Int {
    x := %d;
    y := x + 1;
    return y;
  }
}
"""
UTIL_LINE = 3
UTIL_LINE2 = 4

MAIN = """class DapKeysMain {
  function : Main(args : String[]) ~ Nil {
    a := UtilA->Run();
    b := UtilB->Run();
    "{$a} {$b}"->PrintLine();
  }
}
"""

work = tempfile.mkdtemp(prefix="dap-keys-")
try:
    dir_a = os.path.join(work, "a")
    dir_b = os.path.join(work, "b")
    os.makedirs(dir_a)
    os.makedirs(dir_b)
    util_a = os.path.join(dir_a, "util.obs")
    util_b = os.path.join(dir_b, "util.obs")
    main_src = os.path.join(work, "keys_main.obs")
    obe = os.path.join(work, "keys_main.obe")
    for path, text in ((util_a, UTIL_TEMPLATE % ("A", 10)), (util_b, UTIL_TEMPLATE % ("B", 20)), (main_src, MAIN)):
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    rc = subprocess.run([OBC, "-src", ",".join([main_src, util_a, util_b]), "-dest", obe, "-debug"],
                        cwd=BIN_DIR, env=make_env(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if rc.returncode != 0 or not os.path.exists(obe):
        print("ERROR: could not compile the fixtures:\n" + rc.stdout.decode(errors="replace"), file=sys.stderr)
        sys.exit(2)

    def launch(s):
        s.request("launch", {"program": obe, "sourceDir": work})

    # =======================================================================
    # Tests 1 + 2: a breakpoint in b/util.obs
    # =======================================================================
    s = Session()
    launch(s)
    bps = s.set_breakpoints(util_b, [UTIL_LINE])
    check("the breakpoint in b/util.obs is verified", len(bps) == 1 and bps[0].get("verified") is True,
          f"got: {bps}")
    s.request("configurationDone")
    ev, frame = s.next_stop()
    check("it stops", ev is not None and ev.get("event") == "stopped", f"got: {ev}")
    check("it stops in b/util.obs, not in a/util.obs", "UtilB" in frame.get("name", ""),
          f"stopped in {frame.get('name')!r}")
    check("the frame names b/util.obs as its source",
          same_file(frame.get("source", {}).get("path", ""), util_b),
          f"got: {frame.get('source', {}).get('path')!r}")
    if ev is not None and ev.get("event") == "stopped":
        s.request("continue", {"threadId": ev["body"].get("threadId", 1)})
        ev2, frame2 = s.next_stop()
        check("the program then runs to the end without another stop",
              ev2 is not None and ev2.get("event") == "terminated", f"stopped again in {frame2.get('name')!r}")
    s.close()

    # =======================================================================
    # Tests 3 + 4: the same line of both files
    # =======================================================================
    s = Session()
    launch(s)
    bps_a = s.set_breakpoints(util_a, [UTIL_LINE, UTIL_LINE2])
    bps_b = s.set_breakpoints(util_b, [UTIL_LINE])
    ids = [b.get("id") for b in bps_a + bps_b]
    check("all three breakpoints are verified", all(b.get("verified") is True for b in bps_a + bps_b),
          f"got: {bps_a} {bps_b}")
    check("their ids are three different numbers", len(set(ids)) == 3 and None not in ids, f"ids: {ids}")
    same_line = [b.get("id") for b in bps_a + bps_b if b.get("line") == UTIL_LINE]
    check("the same line of the two files has two different ids", len(same_line) == 2 and same_line[0] != same_line[1],
          f"got: {same_line}")
    s.request("configurationDone")
    seen = []
    for _ in range(3):
        ev, frame = s.next_stop()
        if ev is None or ev.get("event") != "stopped":
            break
        name = frame.get("name", "")
        seen.append(("A" if "UtilA" in name else ("B" if "UtilB" in name else name), frame.get("line")))
        s.request("continue", {"threadId": ev["body"].get("threadId", 1)})
    check("each file's breakpoint stops once, in the order they run",
          seen == [("A", UTIL_LINE), ("A", UTIL_LINE2), ("B", UTIL_LINE)], f"got: {seen}")

    # =======================================================================
    # Test 5: setting a file's breakpoints again
    # =======================================================================
    again = s.set_breakpoints(util_a, [UTIL_LINE, 5])
    by_line = {b["line"]: b.get("id") for b in again}
    first_a = {b["line"]: b.get("id") for b in bps_a}
    check("a line that stays keeps its id", by_line.get(UTIL_LINE) == first_a.get(UTIL_LINE),
          f"was {first_a}, now {by_line}")
    check("a new line gets an id that was never used",
          by_line.get(5) is not None and by_line.get(5) not in ids, f"ids so far {ids}, new {by_line.get(5)}")
    s.close()

    # =======================================================================
    # Test 6: a path that is not on this machine
    # =======================================================================
    s = Session()
    launch(s)
    ghost = os.path.join(work, "no_such_dir", "util.obs")
    bps = s.set_breakpoints(ghost, [UTIL_LINE])
    check("a breakpoint for a file that is not here is still accepted",
          len(bps) == 1 and bps[0].get("verified") is True, f"got: {bps}")
    s.request("configurationDone")
    ev, frame = s.next_stop()
    check("and matches by base name, as it always did", ev is not None and ev.get("event") == "stopped"
          and "Util" in frame.get("name", ""), f"got: {ev} {frame.get('name')!r}")
    s.close()
finally:
    shutil.rmtree(work, ignore_errors=True)

print("")
print("========================================")
print(f"  Results: {passed} passed, {failed} failed")
print("========================================")
sys.exit(1 if failed else 0)
