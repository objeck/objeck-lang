"""Frame-scoped evaluation and hit-count breakpoints over DAP.

 1. evaluate answers from the frame the client selected, not always the top one
 2. ... for every frame on the stack, including the outermost
 3. evaluate with no frameId still answers from the top frame
 4. evaluate of a non-reference expression returns a value, not reinterpreted memory
 5. a string literal evaluates
 6. hitCondition makes a breakpoint stop on the Nth hit
 7. pause reports that it cannot pause instead of claiming success
 8. attach is refused rather than accepted into a session that never starts
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
SRC_FILE = os.path.join(SRC_DIR, "dap_frame_eval_test.obs")
PROG = os.path.join(SRC_DIR, "dap_frame_eval_test.obe")

BREAKPOINT_LINE = 33     # `marker := depth;` inside Inner()
LOOP_LINE = 17           # `counter := i;` -- executed five times

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



# ============================================
# Tests 1-3: evaluate honours frameId
#
# Every frame here has a local called 'depth' with a different value:
# Inner(3) <- Middle(2) <- Main(1). An evaluate that ignores frameId answers 3
# for all three and looks entirely plausible, which is why this went unnoticed
# -- a fixture with distinct names per frame cannot catch it.
# ============================================
s = Session()
if s.run_to_breakpoint():
    st = s.request("stackTrace", {"threadId": 1})
    frames = st.get("body", {}).get("stackFrames", []) if st else []
    check("stopped with three frames on the stack", len(frames) >= 3, f"got: {len(frames)}")

    if len(frames) >= 3:
        # stackTrace lists innermost first.
        seen = []
        for frame in frames[:3]:
            resp = s.request("evaluate", {
                "expression": "depth", "context": "watch", "frameId": frame["id"],
            })
            seen.append(resp.get("body", {}).get("result") if resp else None)

        check("evaluate answers from the selected frame", seen == ["3", "2", "1"],
              f"got: {seen} (all-equal means frameId was ignored)")
        check("evaluate reaches the outermost frame", seen[-1] == "1", f"got: {seen[-1]}")

    # No frameId at all must still mean the top frame. Frame indices run
    # bottom-up internally, so defaulting the absent case to 0 would silently
    # answer from the OUTERMOST frame instead.
    resp = s.request("evaluate", {"expression": "depth", "context": "watch"})
    check("evaluate without frameId uses the top frame",
          resp.get("body", {}).get("result") == "3" if resp else False,
          f"got: {resp.get('body') if resp else None}")

    # ============================================
    # Tests 4-5: expressions that are not variable references
    #
    # These were static_cast to Reference* and read through as a declaration.
    # ============================================
    resp = s.request("evaluate", {"expression": "1 + 2", "context": "watch"})
    check("evaluate computes an arithmetic expression",
          resp.get("body", {}).get("result") == "3" if resp else False,
          f"got: {resp.get('body') if resp else None}")

    resp = s.request("evaluate", {"expression": 'label_text = "inner"', "context": "watch"})
    check("evaluate compares a string",
          resp.get("body", {}).get("result") == "true" if resp else False,
          f"got: {resp.get('body') if resp else None}")

    s.close()
else:
    check("stopped at the breakpoint in Inner()", False, "no stopped event")
    s.close()


# ============================================
# Test 6: hitCondition
#
# The engine has had ignore counts all along; nothing bridged DAP's
# hitCondition onto them, so a hit-count breakpoint became unconditional and
# stopped on the FIRST pass.
# ============================================
s = Session()
s.request("launch", {"program": PROG, "sourceDir": SRC_DIR})
s.request("setBreakpoints", {
    "source": {"path": SRC_FILE},
    "breakpoints": [{"line": LOOP_LINE, "hitCondition": "3"}],
})
s.request("configurationDone")
if s.wait_event("stopped", timeout=20.0):
    resp = s.request("evaluate", {"expression": "i", "context": "watch"})
    got = resp.get("body", {}).get("result") if resp else None
    # Third hit of the loop body, so i is 2. Unconditional would give 0.
    check("hitCondition stops on the Nth hit, not the first", got == "2",
          f"got: {got} (0 means hitCondition was dropped)")
else:
    check("hitCondition breakpoint stops at all", False, "no stopped event")
s.close()


# ============================================
# Tests 7-8: requests that used to claim success while doing nothing
# ============================================
s = Session()
s.request("launch", {"program": PROG, "sourceDir": SRC_DIR})
s.request("setBreakpoints", {"source": {"path": SRC_FILE},
                             "breakpoints": [{"line": BREAKPOINT_LINE}]})
s.request("configurationDone")
s.wait_event("stopped", timeout=20.0)

# 'pause' has no capability gate, so the editor always shows the button. It
# used to answer success and do nothing at all.
resp = s.request("pause", {"threadId": 1})
check("pause does not claim success it cannot deliver",
      resp is not None and resp.get("success") is False,
      f"got: {resp}")
s.close()

# 'attach' fell to the unknown-command catch-all, which answers success. The
# session then hung forever with no debugger and no diagnosis.
s = Session()
resp = s.request("attach", {"program": PROG})
check("attach is refused rather than silently accepted",
      resp is not None and resp.get("success") is False,
      f"got: {resp}")
s.close()


print("")
print("========================================")
print(f"  Results: {passed} passed, {failed} failed")
print("========================================")
sys.exit(1 if failed else 0)
