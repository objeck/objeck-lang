#!/usr/bin/env python3
"""DAP regression test: verify instance and class variable scopes appear
and contain correct values.

Tests:
  1. Instance variables visible inside instance methods with correct values
  2. Class (static) variables visible inside instance methods
  3. Instance variables update after stepping
  4. Step-over from instance method returns to caller
  5. Hover evaluate with and without @ prefix
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


def frame_msg(msg):
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

seq_counter = 1


def next_seq():
    global seq_counter
    s = seq_counter
    seq_counter += 1
    return s


def send(command, args=None):
    s = next_seq()
    msg = {"seq": s, "type": "request", "command": command}
    if args is not None:
        msg["arguments"] = args
    proc.stdin.write(frame_msg(msg))
    proc.stdin.flush()
    return s


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


def wait_response(command, timeout=5.0):
    return wait_for(lambda m: m.get("command") == command and m.get("type") == "response", timeout)


def wait_event(event, timeout=5.0):
    return wait_for(lambda m: m.get("event") == event, timeout)


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


def get_scopes(frame_id):
    """Request scopes for a frame, return dict of name -> variablesReference."""
    send("scopes", {"frameId": frame_id})
    resp = wait_response("scopes")
    if not resp:
        return {}
    return {s["name"]: s["variablesReference"] for s in resp.get("body", {}).get("scopes", [])}


def get_variables(var_ref):
    """Request variables for a reference, return dict of name -> value."""
    send("variables", {"variablesReference": var_ref})
    resp = wait_response("variables")
    if not resp:
        return {}
    return {v["name"]: v["value"] for v in resp.get("body", {}).get("variables", [])}


def get_top_frame():
    """Get the top stack frame (id, name)."""
    send("stackTrace", {"threadId": 1})
    resp = wait_response("stackTrace")
    if resp and resp.get("body", {}).get("stackFrames"):
        f = resp["body"]["stackFrames"][0]
        return f["id"], f.get("name", "")
    return None, None


def evaluate(expression, context="hover"):
    """Send evaluate request, return result string."""
    send("evaluate", {"expression": expression, "context": context})
    resp = wait_response("evaluate")
    if resp and resp.get("body"):
        return resp["body"].get("result", "<no result>")
    return "<no response>"


# ============================================
# Initialize + Launch
# ============================================
send("initialize", {"adapterID": "objeck"})
check("init", wait_response("initialize") is not None)
check("initialized event", wait_event("initialized") is not None)

send("launch", {"program": PROG, "sourceDir": SRC_DIR})
check("launch", wait_response("launch") is not None)

# Breakpoint on line 11: @count += 1 inside Counter::Increment()
send("setBreakpoints", {
    "source": {"path": SRC_FILE},
    "breakpoints": [{"line": 11}],
})
check("setBreakpoints", wait_response("setBreakpoints") is not None)

send("configurationDone")
check("configurationDone", wait_response("configurationDone") is not None)

# ============================================
# Test 1: First stop in Increment — instance vars
# ============================================
m = wait_event("stopped", timeout=5.0)
check("stopped at line 11 (1st Increment)", m is not None)

frame_id, frame_name = get_top_frame()
check("frame is Increment", frame_name is not None and "Increment" in frame_name, f"got: {frame_name}")

scopes = get_scopes(frame_id)
check("Instance scope exists", "Instance" in scopes, f"scopes={list(scopes.keys())}")
check("Class scope exists", "Class" in scopes, f"scopes={list(scopes.keys())}")

# Instance variables
if "Instance" in scopes:
    inst_vars = get_variables(scopes["Instance"])
    print(f"    Instance vars: {inst_vars}")
    check("@count found in instance", "@count" in inst_vars, f"vars={list(inst_vars.keys())}")
    if "@count" in inst_vars:
        check("@count == 0 before 1st increment", inst_vars["@count"] == "0",
              f"got: {inst_vars['@count']}")

# Class (static) variables
if "Class" in scopes:
    cls_vars = get_variables(scopes["Class"])
    print(f"    Class vars: {cls_vars}")
    check("@total_created found in class", "@total_created" in cls_vars,
          f"vars={list(cls_vars.keys())}")
    if "@total_created" in cls_vars:
        # One Counter->New() call happened before this Increment
        check("@total_created == 1", cls_vars["@total_created"] == "1",
              f"got: {cls_vars['@total_created']}")

# ============================================
# Test 2: Hover evaluate (with and without @ prefix)
# ============================================
result_at = evaluate("@count", "hover")
check("hover '@count' works", result_at != "<error>", f"got: {result_at}")

result_no_at = evaluate("count", "hover")
check("hover 'count' (auto @ prefix) works", result_no_at != "<error>", f"got: {result_no_at}")

if result_at != "<error>" and result_no_at != "<error>":
    check("hover with/without @ gives same value", result_at == result_no_at,
          f"@count={result_at}, count={result_no_at}")

# ============================================
# Test 3: Step-over — @count should update
# ============================================
send("next", {"threadId": 1})
wait_response("next")

m = wait_for(lambda m: m.get("event") in ("stopped", "terminated"), timeout=8.0)
check("stopped after step-over", m is not None and m.get("event") == "stopped",
      f"got: {m.get('event') if m else 'timeout'}")

if m and m.get("event") == "stopped":
    frame_id2, frame_name2 = get_top_frame()
    print(f"    Frame after step: {frame_name2}")

    scopes2 = get_scopes(frame_id2)

    # Step-over from the only line in Increment should return to Main (static)
    if "Instance" in scopes2:
        # Still in an instance method — check updated value
        inst_vars2 = get_variables(scopes2["Instance"])
        if "@count" in inst_vars2:
            check("@count updated after step", int(inst_vars2["@count"]) > 0,
                  f"got: {inst_vars2['@count']}")
    else:
        check("step-over returned to caller (Main)", "Locals" in scopes2,
              f"scopes={list(scopes2.keys())}")

    # Continue to next breakpoint hit (2nd Increment call)
    send("continue", {"threadId": 1})
    wait_response("continue")

    m = wait_for(lambda m: m.get("event") in ("stopped", "terminated"), timeout=5.0)
    if m and m.get("event") == "stopped":
        check("stopped at 2nd Increment", True)

        frame_id3, frame_name3 = get_top_frame()
        scopes3 = get_scopes(frame_id3)

        if "Instance" in scopes3:
            inst_vars3 = get_variables(scopes3["Instance"])
            print(f"    Instance vars (2nd stop): {inst_vars3}")
            if "@count" in inst_vars3:
                check("@count == 1 at 2nd Increment", inst_vars3["@count"] == "1",
                      f"got: {inst_vars3['@count']}")

        if "Class" in scopes3:
            cls_vars3 = get_variables(scopes3["Class"])
            print(f"    Class vars (2nd stop): {cls_vars3}")
            if "@total_created" in cls_vars3:
                check("@total_created still 1 (no new objects)", cls_vars3["@total_created"] == "1",
                      f"got: {cls_vars3['@total_created']}")

        # Continue to 3rd Increment
        send("continue", {"threadId": 1})
        wait_response("continue")

        m = wait_for(lambda m: m.get("event") in ("stopped", "terminated"), timeout=5.0)
        if m and m.get("event") == "stopped":
            check("stopped at 3rd Increment", True)

            frame_id4, _ = get_top_frame()
            scopes4 = get_scopes(frame_id4)

            if "Instance" in scopes4:
                inst_vars4 = get_variables(scopes4["Instance"])
                print(f"    Instance vars (3rd stop): {inst_vars4}")
                if "@count" in inst_vars4:
                    check("@count == 2 at 3rd Increment", inst_vars4["@count"] == "2",
                          f"got: {inst_vars4['@count']}")

            if "Class" in scopes4:
                cls_vars4 = get_variables(scopes4["Class"])
                if "@total_created" in cls_vars4:
                    check("@total_created still 1 before 2nd New()",
                          cls_vars4["@total_created"] == "1",
                          f"got: {cls_vars4['@total_created']}")

# ============================================
# Cleanup
# ============================================
send("disconnect")
wait_response("disconnect")

try:
    proc.wait(timeout=3.0)
except subprocess.TimeoutExpired:
    proc.kill()

print(f"\n  Results: {passed} passed, {failed} failed")
sys.exit(0 if failed == 0 else 1)
