"""setVariable through a drill-down handle must not write somewhere else.

HandleSetVariable decoded handles with an open-ended class-scope range, so a
child handle (100000+) became frame index 96000+, which DbgFrameAt clamped to
the top frame: editing holder.count wrote the top-frame LOCAL named count and
reported success. The fixture has both -- a local 'count' and a 'holder' whose
field is also named count -- so a cross-write is observable.

 1. the object expands and exposes its field
 2. setVariable on the field either changes THAT field or is refused honestly
 3. the top-frame local of the same name is untouched either way
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
SRC_FILE = os.path.join(SRC_DIR, "dap_setvar_test.obs")
PROG = os.path.join(SRC_DIR, "dap_setvar_test.obe")

BREAKPOINT_LINE = 0      # resolved below by searching the fixture
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




with open(SRC_FILE, encoding="utf-8") as _fh:
    _lines = _fh.read().split("\n")
BREAKPOINT_LINE = next(i + 1 for i, l in enumerate(_lines) if "total := count + holder->GetCount() + values[1];" in l)

s = Session()
# passed explicitly: the method's default was bound when BREAKPOINT_LINE was
# still 0, so the breakpoint went to line 0 and the program ran to completion
stopped = s.run_to_breakpoint(BREAKPOINT_LINE)
check("stopped at the breakpoint", stopped is not None, "no stopped event")
if stopped:
    ref = s.locals_ref()
    pane = {v["name"]: v for v in s.variables(ref)}
    holder_ref = pane.get("holder", {}).get("variablesReference", 0)
    check("holder expands", holder_ref != 0, f"got: {pane.get('holder')}")

    before_local = pane.get("count", {}).get("value")
    check("local count starts at 1", before_local == "1", f"got: {before_local}")

    if holder_ref:
        children = s.variables(holder_ref)
        field = next((c for c in children if c["name"].lstrip("@") == "count"), None)
        check("holder exposes its count field", field is not None, f"children: {children}")
        field_before = field["value"] if field else None

        # The value is an expression evaluated in the stopped frame, exactly as
        # the CLI's 'set' evaluates it: local count is 1, so this stores 9.
        resp = s.request("setVariable", {"variablesReference": holder_ref,
                                         "name": field["name"] if field else "count", "value": "count + 8"})
        check("setVariable on the expanded field succeeds", bool(resp and resp.get("success")), f"got: {resp}")
        check("the response carries the stored value", bool(resp) and resp.get("body", {}).get("value") == "9",
              f"got: {resp.get('body') if resp else resp}")

        after_children = {c["name"].lstrip("@"): c["value"] for c in s.variables(holder_ref)}
        after_field = after_children.get("count")
        after_pane = {v["name"]: v["value"] for v in s.variables(s.locals_ref())}
        after_local = after_pane.get("count")

        check("setVariable changed the FIELD", after_field == "9",
              f"field before={field_before} after={after_field}")
        check("the top-frame local named count is untouched", after_local == "1",
              f"local count is now {after_local!r} (a cross-write means the old handle decode)")

        # a field the class does not have is refused by name, not written somewhere
        resp = s.request("setVariable", {"variablesReference": holder_ref, "name": "@nosuch", "value": "1"})
        check("an unknown field is refused and named",
              resp is not None and not resp.get("success") and "no field named '@nosuch'" in str(resp.get("message", "")),
              f"got: {resp}")

    # an element of an expanded Int[] is assignable through its handle
    values_ref = pane.get("values", {}).get("variablesReference", 0)
    check("the Int[] expands", values_ref != 0, f"got: {pane.get('values')}")
    if values_ref:
        elems = {c["name"]: c["value"] for c in s.variables(values_ref)}
        check("element [1] reads 4 before the write", elems.get("[1]") == "4", f"got: {elems}")
        resp = s.request("setVariable", {"variablesReference": values_ref, "name": "[1]", "value": "0x2a"})
        check("setVariable on an array element succeeds (hex literal)", bool(resp and resp.get("success")), f"got: {resp}")
        elems = {c["name"]: c["value"] for c in s.variables(values_ref)}
        check("element [1] reads 42 after the write", elems.get("[1]") == "42", f"got: {elems}")
        resp = s.request("setVariable", {"variablesReference": values_ref, "name": "[7]", "value": "1"})
        check("an out-of-range element is refused",
              resp is not None and not resp.get("success") and "outside the array" in str(resp.get("message", "")),
              f"got: {resp}")

    # a scope-level local through the same path: a signed literal and a refused string
    resp = s.request("setVariable", {"variablesReference": ref, "name": "count", "value": "-3"})
    check("a negative literal is stored in a local",
          bool(resp and resp.get("success")) and resp.get("body", {}).get("value") == "-3", f"got: {resp}")
    resp = s.request("setVariable", {"variablesReference": ref, "name": "count", "value": "\"text\""})
    check("a string value is refused with the shared message",
          resp is not None and not resp.get("success") and "only Int, Char and Float values" in str(resp.get("message", "")),
          f"got: {resp}")
    after_pane = {v["name"]: v["value"] for v in s.variables(s.locals_ref())}
    check("the refused write left the local at -3", after_pane.get("count") == "-3", f"got: {after_pane.get('count')}")
s.close()

print("")
print("========================================")
print(f"  Results: {passed} passed, {failed} failed")
print("========================================")
sys.exit(1 if failed else 0)
