#!/usr/bin/env python3
"""DAP regression test: verify program stdout flows through as DAP `output`
events when the user is stopped at a breakpoint and continues.

This catches the bug where `obd --dap` had no stdout redirection — the
running program's PrintLine wrote raw text into the JSON-RPC stream,
corrupted the protocol, and wedged the debugger.

Run from programs/regression/ via `python3 dap_print_test.py`.
"""
import json
import os
import queue
import subprocess
import sys
import threading
import time

# Compute paths relative to this script's location so the test runs from
# any cwd. Layout: <repo>/programs/regression/dap_print_test.py
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
PLATFORM = os.environ.get("DAP_TEST_PLATFORM", "x64")

# Locate deploy directory the same way run_dap_tests.sh does.
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
    """Read one DAP framed message; returns dict or None on EOF."""
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

# Background reader posts every received message to a queue, AND records
# every output event in a side list so we don't lose them when wait_for()
# drains the queue waiting for a request response.
msgs = queue.Queue()
all_outputs = []
all_outputs_lock = threading.Lock()


def reader():
    while True:
        m = read_msg(proc.stdout)
        if m is None:
            return
        with all_outputs_lock:
            if m.get("event") == "output":
                all_outputs.append(m["body"])
        msgs.put(m)


threading.Thread(target=reader, daemon=True).start()


def wait_for(predicate, timeout=5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            m = msgs.get(timeout=0.1)
        except queue.Empty:
            continue
        if predicate(m):
            return m
    return None


def send(seq, command, args=None):
    msg = {"seq": seq, "type": "request", "command": command}
    if args is not None:
        msg["arguments"] = args
    proc.stdin.write(frame(msg))
    proc.stdin.flush()


passed = 0
failed = 0


def check(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
    else:
        failed += 1
        print(f"  [FAIL] {name}: {detail}")


# 1. initialize
send(1, "initialize", {"adapterID": "objeck", "linesStartAt1": True, "columnsStartAt1": True})
check("initialize response", wait_for(lambda m: m.get("command") == "initialize" and m.get("type") == "response") is not None)
check("initialized event", wait_for(lambda m: m.get("event") == "initialized") is not None)

# 2. launch
send(2, "launch", {"program": PROG, "sourceDir": SRC_DIR})
check("launch response", wait_for(lambda m: m.get("command") == "launch" and m.get("type") == "response") is not None)

# 3. setBreakpoints at line 46 (right before `result := Factorial(5);`)
send(3, "setBreakpoints", {
    "source": {"path": SRC_FILE},
    "breakpoints": [{"line": 46}],
})
check("setBreakpoints response", wait_for(lambda m: m.get("command") == "setBreakpoints" and m.get("type") == "response") is not None)

# 4. configurationDone -> program starts
send(4, "configurationDone")
check("configurationDone response", wait_for(lambda m: m.get("command") == "configurationDone" and m.get("type") == "response") is not None)
check("stopped at breakpoint", wait_for(lambda m: m.get("event") == "stopped", timeout=5.0) is not None)

# 5. continue - program runs to completion calling multiple PrintLines
send(5, "continue", {"threadId": 1})
check("continue response", wait_for(lambda m: m.get("command") == "continue" and m.get("type") == "response") is not None)

# 6. THE KEY CHECK: program output should arrive as DAP output events
time.sleep(2.0)
with all_outputs_lock:
    snapshot = list(all_outputs)
output_chunks = [b["output"] for b in snapshot if b.get("category") in ("stdout", "stderr")]
combined = "".join(output_chunks)
check("received output events", len(output_chunks) > 0, f"got {len(output_chunks)} chunks")
check("output contains 'Sum='", "Sum=" in combined, f"got: {combined!r}")
check("output contains 'Factorial(5)=120'", "Factorial(5)=120" in combined, f"got: {combined!r}")

# 7. disconnect cleanly
send(99, "disconnect")
check("disconnect response", wait_for(lambda m: m.get("command") == "disconnect" and m.get("type") == "response", timeout=3.0) is not None)

# 8. process should exit cleanly (no fast-fail)
try:
    rc = proc.wait(timeout=3.0)
    check("process exited cleanly", rc == 0, f"exit code {rc}")
except subprocess.TimeoutExpired:
    proc.kill()
    check("process exited cleanly", False, "obd hung after disconnect")

print(f"  Results: {passed} passed, {failed} failed")
sys.exit(0 if failed == 0 else 1)
