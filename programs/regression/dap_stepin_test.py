#!/usr/bin/env python3
"""DAP regression test: verify stepIn doesn't crash the VM and that
disconnect after step+continue exits cleanly.

This catches two related bugs that hit `obd --dap` together:
  1. Program output (PrintLine) corrupted the JSON-RPC stream because
     stdout wasn't redirected - fixed by piping process stdout/stderr
     into reader threads that emit DAP `output` events.
  2. Disconnecting while the VM was stopped triggered a 0xC0000005
     access violation because the VM thread was detached and raced
     process teardown - fixed by joining the VM thread in Run().

Run from programs/regression/ via `python3 dap_stepin_test.py`.
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
SRC_FILE = os.path.join(SRC_DIR, "debugger_test.obs")
PROG = os.path.join(SRC_DIR, "debugger_test.obe")

if not os.path.exists(PROG):
    print(f"ERROR: {PROG} not found - run run_dap_tests.sh first to build it", file=sys.stderr)
    sys.exit(2)


def frame(msg):
    body = json.dumps(msg)
    return f"Content-Length: {len(body)}\r\n\r\n{body}".encode()


def read_msg(stream):
    header = b""
    while not header.endswith(b"\r\n\r\n"):
        c = stream.read(1)
        if not c:
            return None
        header += c
    length = int(header.decode().split("Content-Length: ")[1].split("\r\n")[0])
    body = stream.read(length)
    return json.loads(body.decode("utf-8"))


env = os.environ.copy()
env["OBJECK_LIB_PATH"] = LIB_PATH

proc = subprocess.Popen(
    [OBD, "--dap"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    env=env, bufsize=0,
)

events = []
events_lock = threading.Lock()


def reader():
    while True:
        m = read_msg(proc.stdout)
        if m is None:
            return
        with events_lock:
            events.append(m)


threading.Thread(target=reader, daemon=True).start()


def send(seq, command, args=None):
    msg = {"seq": seq, "type": "request", "command": command}
    if args is not None:
        msg["arguments"] = args
    proc.stdin.write(frame(msg))
    proc.stdin.flush()


def wait_for(predicate, timeout=5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        with events_lock:
            for i, m in enumerate(events):
                if predicate(m):
                    events.pop(i)
                    return m
        time.sleep(0.05)
    return None


passed = 0
failed = 0


def check(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
    else:
        failed += 1
        print(f"  [FAIL] {name}: {detail}")


send(1, "initialize", {"adapterID": "objeck"})
check("init", wait_for(lambda m: m.get("command") == "initialize" and m.get("type") == "response") is not None)
check("initialized event", wait_for(lambda m: m.get("event") == "initialized") is not None)

send(2, "launch", {"program": PROG, "sourceDir": SRC_DIR})
check("launch", wait_for(lambda m: m.get("command") == "launch" and m.get("type") == "response") is not None)

# Breakpoint at line 46: `result := Factorial(5);`
send(3, "setBreakpoints", {
    "source": {"path": SRC_FILE},
    "breakpoints": [{"line": 46}],
})
check("setBreakpoints", wait_for(lambda m: m.get("command") == "setBreakpoints" and m.get("type") == "response") is not None)

send(4, "configurationDone")
check("configurationDone", wait_for(lambda m: m.get("command") == "configurationDone" and m.get("type") == "response") is not None)
check("stopped at line 46", wait_for(lambda m: m.get("event") == "stopped", timeout=5.0) is not None)

# Step INTO Factorial
send(5, "stepIn", {"threadId": 1})
check("stepIn response", wait_for(lambda m: m.get("command") == "stepIn" and m.get("type") == "response") is not None)
check("stopped after stepIn", wait_for(lambda m: m.get("event") == "stopped", timeout=5.0) is not None)

# Continue past Factorial. The breakpoint at line 46 will refire when
# Factorial returns and control comes back to that line to complete the
# assignment - that's expected DAP behavior, send another continue.
send(6, "continue", {"threadId": 1})
check("continue response", wait_for(lambda m: m.get("command") == "continue" and m.get("type") == "response") is not None)

m = wait_for(lambda m: m.get("event") in ("terminated", "stopped"), timeout=5.0)
check("got terminated or second stop", m is not None, f"got: {m}")
if m and m.get("event") == "stopped":
    send(7, "continue", {"threadId": 1})
    wait_for(lambda m: m.get("command") == "continue" and m.get("type") == "response")
    wait_for(lambda m: m.get("event") == "terminated", timeout=5.0)

# Give the VM time to flush the post-Factorial print
time.sleep(0.5)

send(99, "disconnect")
check("disconnect response", wait_for(lambda m: m.get("command") == "disconnect" and m.get("type") == "response") is not None)

# Verify the post-Factorial print made it through
with events_lock:
    outputs = [e for e in events if e.get("event") == "output"]
combined = "".join(o["body"]["output"] for o in outputs if o["body"].get("category") == "stdout")
check("got Factorial(5)=120 in output", "Factorial(5)=120" in combined,
      f"got: {combined!r}")

# Process must exit cleanly - the join in Run() prevents the VM thread
# from racing process teardown.
try:
    rc = proc.wait(timeout=3.0)
    check("process exited cleanly", rc == 0, f"exit code {rc}")
except subprocess.TimeoutExpired:
    proc.kill()
    check("process exited cleanly", False, "obd hung after disconnect")

print(f"  Results: {passed} passed, {failed} failed")
sys.exit(0 if failed == 0 else 1)
