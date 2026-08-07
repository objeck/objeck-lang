#!/usr/bin/env python3
"""DAP regression test: protocol features beyond the core lifecycle.

Covers the capabilities and requests added on top of the original adapter:

  1. New capabilities are advertised
  2. breakpointLocations reports only lines that can hold a breakpoint
  3. breakpointLocations rejects a line with no instruction
  4. evaluate returns an expandable reference for an object
  5. evaluate expansion yields the same children as the Variables pane
  6. evaluate stays a leaf for a scalar
  7. indexedVariables is reported when the client supports paging
  8. variables honours start/count paging
  9. setExpression assigns a scalar
 10. setExpression refuses a non-assignable target
 11. exceptionInfo is refused when not stopped on an exception
 12. terminate shuts the session down and reports terminated
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
SRC_FILE = os.path.join(SRC_DIR, "dap_drilldown_test.obs")
PROG = os.path.join(SRC_DIR, "dap_drilldown_test.obe")

BREAKPOINT_LINE = 76     # `done := true;`
BLANK_LINE = 2           # blank line in the fixture -- no instruction

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
# Test 1: capabilities
# ============================================
s = Session()
s.send("initialize", {"adapterID": "objeck"})
init = s.wait_response("initialize")
caps = init.get("body", {}) if init else {}
for flag in ["supportsTerminateRequest", "supportsBreakpointLocationsRequest",
             "supportsExceptionInfoRequest", "supportsSetExpression",
             "supportsCompletionsRequest", "supportsModulesRequest",
             "supportsLoadedSourcesRequest"]:
    check(f"advertises {flag}", caps.get(flag) is True, f"got: {caps.get(flag)}")

check("declares completionTriggerCharacters",
      isinstance(caps.get("completionTriggerCharacters"), list)
      and len(caps["completionTriggerCharacters"]) > 0,
      f"got: {caps.get('completionTriggerCharacters')}")

# Before launch nothing can be validated, so every requested line is offered
# rather than telling the editor the file has no breakpointable lines.
resp = s.request("breakpointLocations", {
    "source": {"path": SRC_FILE}, "line": BLANK_LINE, "endLine": BLANK_LINE,
})
pre_locs = resp.get("body", {}).get("breakpoints", []) if resp else []
check("breakpointLocations is permissive before launch", len(pre_locs) == 1, f"got: {pre_locs}")

# ============================================
# Test 11: exceptionInfo when not on an exception
# ============================================
resp = s.request("exceptionInfo", {"threadId": 1})
check("exceptionInfo refused when not stopped on an exception",
      resp is not None and resp.get("success") is False, f"got: {resp}")
s.close()

# ============================================
# Tests 4-6: evaluate returns expandable references
# ============================================
s = Session()
if s.run_to_breakpoint():
    # Tests 2 + 3: with the program loaded, only real lines are offered
    resp = s.request("breakpointLocations", {
        "source": {"path": SRC_FILE}, "line": BREAKPOINT_LINE, "endLine": BREAKPOINT_LINE,
    })
    locs = resp.get("body", {}).get("breakpoints", []) if resp else []
    check("breakpointLocations reports an executable line",
          any(l.get("line") == BREAKPOINT_LINE for l in locs), f"got: {locs}")

    resp = s.request("breakpointLocations", {
        "source": {"path": SRC_FILE}, "line": BLANK_LINE, "endLine": BLANK_LINE,
    })
    blank_locs = resp.get("body", {}).get("breakpoints", []) if resp else []
    check("breakpointLocations rejects a blank line", len(blank_locs) == 0, f"got: {blank_locs}")

    ref = s.locals_ref()
    pane = {v["name"]: v for v in s.variables(ref)}

    resp = s.request("evaluate", {"expression": "point", "context": "watch"})
    body = resp.get("body", {}) if resp else {}
    ev_ref = body.get("variablesReference", 0)
    check("evaluate returns an expandable reference for an object", ev_ref != 0,
          f"got: {body}")

    if ev_ref:
        ev_children = {v["name"]: v["value"] for v in s.variables(ev_ref)}
        pane_ref = pane.get("point", {}).get("variablesReference", 0)
        pane_children = {v["name"]: v["value"] for v in s.variables(pane_ref)} if pane_ref else {}
        check("evaluate expansion matches the Variables pane",
              ev_children == pane_children and "@x" in ev_children,
              f"evaluate={ev_children} pane={pane_children}")

    resp = s.request("evaluate", {"expression": "counter", "context": "watch"})
    scalar_ref = resp.get("body", {}).get("variablesReference", 1) if resp else 1
    check("evaluate stays a leaf for a scalar", scalar_ref == 0, f"got: {scalar_ref}")
else:
    check("evaluate returns an expandable reference for an object", False, "never stopped")
s.close()

# ============================================
# Tests 7 + 8: paging
# ============================================
s = Session({"supportsVariablePaging": True})
if s.run_to_breakpoint():
    ref = s.locals_ref()
    pane = {v["name"]: v for v in s.variables(ref)}

    numbers = pane.get("numbers", {})
    check("indexedVariables reported for Int[] when client pages",
          numbers.get("indexedVariables") == 4, f"got: {numbers.get('indexedVariables')}")

    vector = pane.get("vector", {})
    check("indexedVariables reported for Vector",
          vector.get("indexedVariables") == 3, f"got: {vector.get('indexedVariables')}")

    num_ref = numbers.get("variablesReference", 0)
    if num_ref:
        page = s.variables(num_ref, {"start": 1, "count": 2})
        check("variables honours start/count paging",
              [v["name"] for v in page] == ["[1]", "[2]"], f"got: {[v['name'] for v in page]}")
        full = s.variables(num_ref)
        check("unpaged request still returns everything", len(full) == 4, f"got: {len(full)}")
else:
    check("indexedVariables reported for Int[] when client pages", False, "never stopped")
s.close()

# ============================================
# Tests 9 + 10: setExpression
# ============================================
s = Session()
if s.run_to_breakpoint():
    frame = s.request("stackTrace", {"threadId": 1})["body"]["stackFrames"][0]["id"]

    resp = s.request("setExpression", {"expression": "counter", "value": "42", "frameId": frame})
    ok = resp is not None and resp.get("success") is True
    check("setExpression assigns a scalar", ok, f"got: {resp}")

    resp = s.request("setExpression", {"expression": "point", "value": "1", "frameId": frame})
    check("setExpression refuses a non-assignable target",
          resp is not None and resp.get("success") is False, f"got: {resp}")
else:
    check("setExpression assigns a scalar", False, "never stopped")
s.close()

# ============================================
# Tests 13-17: completions, modules, loadedSources
# ============================================
s = Session()
if s.run_to_breakpoint():
    frame = s.request("stackTrace", {"threadId": 1})["body"]["stackFrames"][0]["id"]

    # locals of the fixture's Main: point, box, numbers, vector, map, ...
    resp = s.request("completions", {"text": "po", "column": 3, "frameId": frame})
    targets = resp.get("body", {}).get("targets", []) if resp else []
    labels = [t["label"] for t in targets]
    check("completions suggests a matching local",
          "point" in labels and "points" in labels, f"got: {labels}")
    check("completions filters out non-matching locals",
          all(l.startswith("po") for l in labels), f"got: {labels}")
    if targets:
        check("completions reports the replacement span",
              targets[0].get("start") == 0 and targets[0].get("length") == 2,
              f"got: start={targets[0].get('start')} length={targets[0].get('length')}")

    resp = s.request("completions", {"text": "zzz", "column": 4, "frameId": frame})
    check("completions returns nothing for an unknown prefix",
          len(resp.get("body", {}).get("targets", [])) == 0, f"got: {resp}")

    resp = s.request("modules", {})
    mods = resp.get("body", {}).get("modules", []) if resp else []
    check("modules reports the running program",
          len(mods) == 1 and mods[0].get("name", "").endswith(".obe"), f"got: {mods}")

    resp = s.request("loadedSources", {})
    srcs = resp.get("body", {}).get("sources", []) if resp else []
    names = [x.get("name", "") for x in srcs]
    check("loadedSources lists the fixture source",
          any("dap_drilldown_test.obs" in n for n in names), f"got: {names}")
else:
    check("completions suggests a matching local", False, "never stopped")
s.close()

# ============================================
# Test 12: terminate
# ============================================
s = Session()
if s.run_to_breakpoint():
    resp = s.request("terminate", {})
    check("terminate acknowledged", resp is not None and resp.get("success") is True, f"got: {resp}")
    check("terminated event emitted", s.wait_event("terminated", timeout=8.0) is not None)
else:
    check("terminate acknowledged", False, "never stopped")
s.close()

print(f"\n  Results: {passed} passed, {failed} failed")
sys.exit(0 if failed == 0 else 1)
