"""DAP transport and request-handling robustness.

Each test fails on the adapter as it was before the fix it names.

 1. a program's output is split across the adapter's 2048-byte reads in the middle
    of multi-byte characters: every character still arrives whole, and the
    adapter survives. The JSON encoder throws on a half character, the throw
    happened on an output reader thread that has no handler, and the whole
    process was lost to std::terminate.
 2. outgoing sequence numbers are unique and go out in order while the request
    thread and both output reader threads send at once. They were taken from a
    plain int before the frame lock, so two senders could draw the same number
    or put a later one on the wire first.
 3. a request the adapter does not implement is answered success=false with a
    message, not success=true.
 4. a breakpoint whose condition cannot be parsed comes back verified=false with
    a message, and does not stop the program on every hit.
 5. a breakpoint with a valid condition still verifies and still stops only
    when the condition holds.
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
    """Writes <name>.obs into 'work', compiles it with debug symbols, returns (src, obe)."""
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
        self.seqs = []            # every outgoing seq, in the order the frames arrived
        self.bad_frames = []
        self.lock = threading.Lock()
        self.seq = 1
        threading.Thread(target=self._reader, daemon=True).start()
        self.send("initialize", {"adapterID": "objeck"})
        self.wait_response("initialize")

    def _read_exact(self, n):
        data = b""
        while len(data) < n:
            chunk = self.proc.stdout.read(n - len(data))
            if not chunk:
                return None
            data += chunk
        return data

    def _read_msg(self):
        header = b""
        while not header.endswith(b"\r\n\r\n"):
            c = self.proc.stdout.read(1)
            if not c:
                return None
            header += c
        length = int(header.decode().split("Content-Length: ")[1].split("\r\n")[0])
        body = self._read_exact(length)
        if body is None:
            return None
        try:
            return json.loads(body.decode("utf-8"))
        except ValueError:
            self.bad_frames.append(body[:200])
            return {}

    def _reader(self):
        while True:
            m = self._read_msg()
            if m is None:
                return
            with self.lock:
                if "seq" in m:
                    self.seqs.append(m["seq"])
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

    def alive(self):
        return self.proc.poll() is None

    def close(self):
        self.send("disconnect")
        self.wait_response("disconnect", timeout=3.0)
        try:
            self.proc.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        return self.proc.returncode


def launch(s, obe, work):
    s.request("launch", {"program": obe, "sourceDir": work})


# ---------------------------------------------------------------------------
# fixtures
# ---------------------------------------------------------------------------
REPEATS = 1 << 15
TEXT_FIXTURE = """class DapRobustText {
  function : Main(args : String[]) ~ Nil {
    text := "é\U0001F600";
    each(k : 15) {
      text += text;
    };
    text->Print();
    "fin"->PrintLine();
  }
}
"""

LINES = 4000
FLOOD_FIXTURE = """class DapRobustFlood {
  function : Main(args : String[]) ~ Nil {
    each(i : %d) {
      "out line {$i}"->PrintLine();
      "err line {$i}"->ErrorLine();
    };
  }
}
""" % LINES

# line 6 is the body of the loop
LOOP_FIXTURE = """class DapRobustLoop {
  function : Main(args : String[]) ~ Nil {
    n := 0;
    while(n < 5) {
      n += 1;
      "n={$n}"->PrintLine();
    };
  }
}
"""
LOOP_LINE = 5

work = tempfile.mkdtemp(prefix="dap-robust-")
try:
    text_src, text_obe = compile_fixture(work, "robust_text", TEXT_FIXTURE)
    flood_src, flood_obe = compile_fixture(work, "robust_flood", FLOOD_FIXTURE)
    loop_src, loop_obe = compile_fixture(work, "robust_loop", LOOP_FIXTURE)
    if not (text_obe and flood_obe and loop_obe):
        print("ERROR: could not compile the fixtures", file=sys.stderr)
        sys.exit(2)

    # =======================================================================
    # Test 1: characters split across reads
    # =======================================================================
    s = Session()
    launch(s, text_obe, work)
    s.send("configurationDone")
    received = []
    terminated = False
    deadline = time.time() + 40
    while time.time() < deadline:
        m = s.wait_for(lambda x: x.get("event") in ("output", "terminated"), 1.0)
        if m is None:
            if not s.alive():
                break
            continue
        if m.get("event") == "terminated":
            terminated = True
            break
        received.append(m["body"].get("output", ""))
    text = "".join(received)
    check("the adapter survives output split inside multi-byte characters",
          s.alive() and terminated, f"alive={s.alive()} terminated={terminated} exit={s.proc.poll()}")
    check("every character of the output arrives whole",
          text.count("é\U0001F600") == REPEATS and "�" not in text,
          f"{text.count(chr(0xe9) + chr(0x1F600))} of {REPEATS} pairs, {text.count(chr(0xfffd))} replacement characters")
    check("every frame was valid UTF-8 JSON", not s.bad_frames, f"{s.bad_frames[:2]!r}")
    s.close()

    # =======================================================================
    # Test 2: outgoing seq numbers under concurrent senders
    # =======================================================================
    s = Session()
    launch(s, flood_obe, work)
    s.send("configurationDone")
    # answer requests from the request thread for as long as the readers send
    end = time.time() + 40
    done = False
    while time.time() < end and not done:
        s.send("threads")
        s.send("threads")
        m = s.wait_for(lambda x: x.get("event") == "terminated", 0.05)
        done = m is not None
    time.sleep(0.5)
    with s.lock:
        seqs = list(s.seqs)
    out_of_order = sum(1 for a, b in zip(seqs, seqs[1:]) if b != a + 1)
    check("the flood ran to its end", done and len(seqs) > LINES, f"terminated={done}, {len(seqs)} frames")
    check("outgoing seq is consecutive on the wire (no duplicate, none out of order)",
          out_of_order == 0 and len(seqs) == len(set(seqs)),
          f"{out_of_order} breaks in {len(seqs)} frames, {len(seqs) - len(set(seqs))} duplicates")
    s.close()

    # =======================================================================
    # Test 3: an unknown request is a failure
    # =======================================================================
    s = Session()
    resp = s.request("noSuchRequest", {})
    check("an unimplemented request answers success=false",
          resp is not None and resp.get("success") is False, f"got: {resp}")
    check("the failure says what was refused",
          resp is not None and "noSuchRequest" in str(resp.get("message", "")), f"got: {resp}")
    s.close()

    # =======================================================================
    # Tests 4 + 5: breakpoint conditions
    # =======================================================================
    s = Session()
    launch(s, loop_obe, work)
    resp = s.request("setBreakpoints", {"source": {"path": loop_src},
                                        "breakpoints": [{"line": LOOP_LINE, "condition": "n = = ("}]})
    bps = resp.get("body", {}).get("breakpoints", []) if resp else []
    check("a condition that does not parse is not verified",
          len(bps) == 1 and bps[0].get("verified") is False, f"got: {bps}")
    check("the unverified breakpoint carries a message",
          len(bps) == 1 and "condition" in str(bps[0].get("message", "")), f"got: {bps}")
    s.send("configurationDone")
    stopped = s.wait_for(lambda x: x.get("event") in ("stopped", "terminated"), 20.0)
    check("a breakpoint with an unparsable condition does not stop the program",
          stopped is not None and stopped.get("event") == "terminated", f"got: {stopped}")
    s.close()

    s = Session()
    launch(s, loop_obe, work)
    resp = s.request("setBreakpoints", {"source": {"path": loop_src},
                                        "breakpoints": [{"line": LOOP_LINE, "condition": "n = 3"}]})
    bps = resp.get("body", {}).get("breakpoints", []) if resp else []
    check("a valid condition is verified",
          len(bps) == 1 and bps[0].get("verified") is True, f"got: {bps}")
    s.send("configurationDone")
    stopped = s.wait_event("stopped", timeout=20.0)
    check("a valid condition stops when it holds", stopped is not None, "never stopped")
    if stopped:
        resp = s.request("evaluate", {"expression": "n", "context": "watch"})
        got = resp.get("body", {}).get("result") if resp else None
        check("the stop is on the iteration the condition names", got == "3", f"n={got}")
        s.request("continue", {"threadId": 1})
    s.close()
finally:
    shutil.rmtree(work, ignore_errors=True)

print("")
print("========================================")
print(f"  Results: {passed} passed, {failed} failed")
print("========================================")
sys.exit(1 if failed else 0)
