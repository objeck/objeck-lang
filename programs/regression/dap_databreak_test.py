#!/usr/bin/env python3
"""DAP regression test: data breakpoints (watchpoints).

A data breakpoint stops when a value changes rather than when execution
reaches a line. The underlying watch machinery existed for the CLI `watch`
command but was never reachable over DAP.

Tests:
  1. supportsDataBreakpoints is advertised
  2. dataBreakpointInfo mints a dataId for a variable in scope
  3. dataBreakpointInfo reports write-only access
  4. dataBreakpointInfo refuses when not stopped
  5. setDataBreakpoints verifies a watchable expression
  6. setDataBreakpoints rejects an unwatchable one
  7. execution stops when the watched value changes
  8. the stop names the watch and the old -> new transition
  9. the watched variable really did change at the stop
 10. a watch on an unmodified variable never fires
 11. clearing the watch list stops further firing
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
SRC_FILE = os.path.join(SRC_DIR, "dap_databreak_test.obs")
PROG = os.path.join(SRC_DIR, "dap_databreak_test.obe")

BREAKPOINT_LINE = 13     # `"start"->PrintLine();` -- before the loop mutates anything

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
    def __init__(self):
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
        self.request("initialize", {"adapterID": "objeck"})

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

    def request(self, command, args=None, timeout=10.0):
        self.send(command, args)
        return self.wait_for(
            lambda m: m.get("command") == command and m.get("type") == "response", timeout)

    def wait_event(self, event, timeout=10.0):
        return self.wait_for(lambda m: m.get("event") == event, timeout)

    def run_to_breakpoint(self):
        self.request("launch", {"program": PROG, "sourceDir": SRC_DIR})
        self.request("setBreakpoints",
                     {"source": {"path": SRC_FILE}, "breakpoints": [{"line": BREAKPOINT_LINE}]})
        self.request("configurationDone")
        return self.wait_event("stopped", timeout=20.0)

    def locals(self):
        st = self.request("stackTrace", {"threadId": 1})
        frame_id = st["body"]["stackFrames"][0]["id"]
        sc = self.request("scopes", {"frameId": frame_id})
        ref = 0
        for s in sc["body"]["scopes"]:
            if s["name"] == "Locals":
                ref = s["variablesReference"]
        if not ref:
            return {}
        resp = self.request("variables", {"variablesReference": ref})
        return {v["name"]: v["value"] for v in resp.get("body", {}).get("variables", [])}

    def close(self):
        self.send("disconnect")
        self.request("disconnect", timeout=3.0)
        try:
            self.proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            self.proc.kill()


# ============================================
# Test 1: capability
# ============================================
s = Session()
s.send("initialize", {"adapterID": "objeck"})
init = s.wait_for(lambda m: m.get("command") == "initialize" and m.get("type") == "response")
check("advertises supportsDataBreakpoints",
      (init.get("body", {}) if init else {}).get("supportsDataBreakpoints") is True)

# ============================================
# Test 4: dataBreakpointInfo before stopping
# ============================================
resp = s.request("dataBreakpointInfo", {"name": "counter"})
body = resp.get("body", {}) if resp else {}
check("dataBreakpointInfo refuses when not stopped", body.get("dataId") is None, f"got: {body}")
s.close()

# ============================================
# Tests 2, 3, 5-9: a watch that fires
# ============================================
s = Session()
if s.run_to_breakpoint():
    before = s.locals()
    check("counter starts at 0", before.get("counter") == "0", f"got: {before.get('counter')}")

    resp = s.request("dataBreakpointInfo", {"name": "counter"})
    body = resp.get("body", {}) if resp else {}
    data_id = body.get("dataId")
    check("dataBreakpointInfo mints a dataId", data_id == "counter", f"got: {body}")
    check("dataBreakpointInfo reports write access",
          body.get("accessTypes") == ["write"], f"got: {body.get('accessTypes')}")

    resp = s.request("setDataBreakpoints", {"breakpoints": [{"dataId": "counter"}]})
    bps = resp.get("body", {}).get("breakpoints", []) if resp else []
    check("setDataBreakpoints verifies a watchable expression",
          len(bps) == 1 and bps[0].get("verified") is True, f"got: {bps}")

    s.request("continue", {"threadId": 1})
    stopped = s.wait_event("stopped", timeout=20.0)
    reason = (stopped or {}).get("body", {}).get("reason")
    check("stops when the watched value changes", reason == "data breakpoint",
          f"got: {stopped}")

    if stopped:
        sbody = stopped.get("body", {})
        check("stop names the watch",
              "counter" in sbody.get("description", ""), f"got: {sbody.get('description')}")
        check("stop reports the old -> new transition",
              "->" in sbody.get("text", "") and "0" in sbody.get("text", ""),
              f"got: {sbody.get('text')}")

        after = s.locals()
        check("the watched variable really changed",
              after.get("counter") not in (None, "0"), f"got: {after.get('counter')}")
else:
    check("stops when the watched value changes", False, "never stopped")
s.close()

# ============================================
# Test 6: unwatchable expression
# ============================================
s = Session()
if s.run_to_breakpoint():
    resp = s.request("setDataBreakpoints", {"breakpoints": [{"dataId": "!!!"}]})
    bps = resp.get("body", {}).get("breakpoints", []) if resp else []
    check("setDataBreakpoints rejects an unwatchable expression",
          len(bps) == 1 and bps[0].get("verified") is False, f"got: {bps}")
else:
    check("setDataBreakpoints rejects an unwatchable expression", False, "never stopped")
s.close()

# ============================================
# Test 10: a variable that never changes must not fire
# ============================================
s = Session()
if s.run_to_breakpoint():
    s.request("setDataBreakpoints", {"breakpoints": [{"dataId": "steady"}]})
    s.request("continue", {"threadId": 1})

    stopped = s.wait_event("stopped", timeout=6.0)
    terminated = s.wait_event("terminated", timeout=6.0) if stopped is None else None
    check("a watch on an unmodified variable never fires",
          stopped is None and terminated is not None,
          f"stopped={stopped is not None} terminated={terminated is not None}")
else:
    check("a watch on an unmodified variable never fires", False, "never stopped")
s.close()

# ============================================
# Test 11: clearing the list stops further firing
# ============================================
s = Session()
if s.run_to_breakpoint():
    s.request("setDataBreakpoints", {"breakpoints": [{"dataId": "counter"}]})
    s.request("continue", {"threadId": 1})
    first = s.wait_event("stopped", timeout=20.0)

    if first:
        # replace the set with an empty one -- the request always carries the
        # complete list, so this removes the watch
        resp = s.request("setDataBreakpoints", {"breakpoints": []})
        check("clearing returns an empty breakpoint list",
              resp is not None and resp.get("body", {}).get("breakpoints") == [],
              f"got: {resp}")

        s.request("continue", {"threadId": 1})
        again = s.wait_event("stopped", timeout=6.0)
        terminated = s.wait_event("terminated", timeout=6.0) if again is None else None
        check("no further stops once the watch is cleared",
              again is None and terminated is not None,
              f"stopped_again={again is not None}")
    else:
        check("clearing returns an empty breakpoint list", False, "watch never fired")
else:
    check("clearing returns an empty breakpoint list", False, "never stopped")
s.close()

print(f"\n  Results: {passed} passed, {failed} failed")
sys.exit(0 if failed == 0 else 1)
