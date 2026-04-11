#!/usr/bin/env python3
"""DAP regression test: step-out and multi-type instance/class variables.

Tests:
  1. Step-out from an instance method returns to the caller
  2. Multiple instance variables of different types (Int, Float, String)
  3. Class variables of different types
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
    print("ERROR: could not find deploy directory", file=sys.stderr)
    sys.exit(2)

COMPILER = os.path.join(DEPLOY_DIR, "bin", "obc.exe" if os.name == "nt" else "obc")
OBD = os.path.join(DEPLOY_DIR, "bin", "obd.exe" if os.name == "nt" else "obd")
LIB_PATH = os.path.join(DEPLOY_DIR, "lib")

# Write a test source with multiple variable types
TEST_SRC = os.path.join(SCRIPT_DIR, "dap_types_test.obs")
TEST_BIN = os.path.join(SCRIPT_DIR, "dap_types_test.obe")

with open(TEST_SRC, "w") as f:
    f.write("""\
class Widget {
  @name : String;
  @value : Int;
  @ratio : Float;
  @tag_count : static : Int;

  New(name : String, value : Int, ratio : Float) {
    @name := name;
    @value := value;
    @ratio := ratio;
    @tag_count += 1;
  }

  method : public : GetInfo() ~ String {
    return "{$@name}: value={$@value}";
  }

  method : public : DoubleValue() ~ Nil {
    @value *= 2;
  }
}

class Main {
  function : Main(args : String[]) ~ Nil {
    w := Widget->New("alpha", 42, 3.14);
    info := w->GetInfo();
    info->PrintLine();
    w->DoubleValue();
    w2 := Widget->New("beta", 7, 2.71);
    w2->GetInfo()->PrintLine();
  }
}
""")

# Compile
env = os.environ.copy()
env["OBJECK_LIB_PATH"] = LIB_PATH

result = subprocess.run(
    [COMPILER, "-src", TEST_SRC, "-dest", TEST_BIN, "-debug"],
    capture_output=True, text=True, env=env,
    cwd=os.path.join(DEPLOY_DIR, "bin"),
)
if result.returncode != 0:
    print(f"Compile failed: {result.stderr}", file=sys.stderr)
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
    send("scopes", {"frameId": frame_id})
    resp = wait_response("scopes")
    if not resp:
        return {}
    return {s["name"]: s["variablesReference"] for s in resp.get("body", {}).get("scopes", [])}


def get_variables(var_ref):
    send("variables", {"variablesReference": var_ref})
    resp = wait_response("variables")
    if not resp:
        return {}
    return {v["name"]: v["value"] for v in resp.get("body", {}).get("variables", [])}


def get_top_frame():
    send("stackTrace", {"threadId": 1})
    resp = wait_response("stackTrace")
    if resp and resp.get("body", {}).get("stackFrames"):
        f = resp["body"]["stackFrames"][0]
        return f["id"], f.get("name", "")
    return None, None


# ============================================
# Initialize + Launch
# ============================================
send("initialize", {"adapterID": "objeck"})
check("init", wait_response("initialize") is not None)
check("initialized event", wait_event("initialized") is not None)

send("launch", {"program": TEST_BIN, "sourceDir": SCRIPT_DIR})
check("launch", wait_response("launch") is not None)

# Breakpoint on line 15: return "{$@name}..." inside GetInfo()
# and line 19: @value *= 2 inside DoubleValue()
send("setBreakpoints", {
    "source": {"path": TEST_SRC},
    "breakpoints": [{"line": 15}, {"line": 19}],
})
check("setBreakpoints", wait_response("setBreakpoints") is not None)

send("configurationDone")
check("configurationDone", wait_response("configurationDone") is not None)

# ============================================
# Test 1: Multi-type instance variables in GetInfo()
# ============================================
m = wait_event("stopped", timeout=5.0)
check("stopped in GetInfo", m is not None)

frame_id, frame_name = get_top_frame()
print(f"    Frame: {frame_name}")
check("frame is GetInfo", frame_name is not None and "GetInfo" in frame_name, f"got: {frame_name}")

scopes = get_scopes(frame_id)
check("Instance scope exists", "Instance" in scopes)
check("Class scope exists", "Class" in scopes)

if "Instance" in scopes:
    inst = get_variables(scopes["Instance"])
    print(f"    Instance vars: {inst}")

    check("@name is String", "@name" in inst, f"vars={list(inst.keys())}")
    check("@value == 42", inst.get("@value") == "42", f"got: {inst.get('@value')}")
    check("@ratio is Float (non-zero)", "@ratio" in inst and inst["@ratio"] != "0",
          f"got: {inst.get('@ratio')}")

if "Class" in scopes:
    cls = get_variables(scopes["Class"])
    print(f"    Class vars: {cls}")
    check("@tag_count == 1", cls.get("@tag_count") == "1", f"got: {cls.get('@tag_count')}")

# ============================================
# Test 2: Step-out from GetInfo() returns to Main
# ============================================
send("stepOut", {"threadId": 1})
wait_response("stepOut")

m = wait_for(lambda m: m.get("event") in ("stopped", "terminated"), timeout=8.0)
check("stopped after step-out", m is not None and m.get("event") == "stopped",
      f"got: {m.get('event') if m else 'timeout'}")

if m and m.get("event") == "stopped":
    frame_id2, frame_name2 = get_top_frame()
    print(f"    Frame after step-out: {frame_name2}")
    check("step-out returned to Main", frame_name2 is not None and "Main" in frame_name2,
          f"got: {frame_name2}")

    # Continue to DoubleValue breakpoint
    send("continue", {"threadId": 1})
    wait_response("continue")

    m = wait_for(lambda m: m.get("event") in ("stopped", "terminated"), timeout=5.0)
    if m and m.get("event") == "stopped":
        check("stopped in DoubleValue", True)

        frame_id3, frame_name3 = get_top_frame()
        print(f"    Frame: {frame_name3}")

        scopes3 = get_scopes(frame_id3)
        if "Instance" in scopes3:
            inst3 = get_variables(scopes3["Instance"])
            print(f"    Instance vars in DoubleValue: {inst3}")
            check("@value == 42 before double", inst3.get("@value") == "42",
                  f"got: {inst3.get('@value')}")

        # Step over the *= 2 line
        send("next", {"threadId": 1})
        wait_response("next")
        m = wait_for(lambda m: m.get("event") in ("stopped", "terminated"), timeout=8.0)

        if m and m.get("event") == "stopped":
            frame_id4, frame_name4 = get_top_frame()
            print(f"    Frame after step in DoubleValue: {frame_name4}")

            # If we returned to Main, continue to next breakpoint
            # (2nd GetInfo or DoubleValue for w2)
            # Either way, check that we didn't crash

            # Continue to see @tag_count increment after 2nd New()
            send("continue", {"threadId": 1})
            wait_response("continue")

            m = wait_for(lambda m: m.get("event") in ("stopped", "terminated"), timeout=5.0)
            if m and m.get("event") == "stopped":
                frame_id5, frame_name5 = get_top_frame()
                print(f"    Frame: {frame_name5}")

                scopes5 = get_scopes(frame_id5)
                if "Class" in scopes5:
                    cls5 = get_variables(scopes5["Class"])
                    print(f"    Class vars after 2nd New: {cls5}")
                    check("@tag_count == 2 after 2nd New()",
                          cls5.get("@tag_count") == "2",
                          f"got: {cls5.get('@tag_count')}")

# ============================================
# Cleanup
# ============================================
send("disconnect")
wait_response("disconnect")

try:
    proc.wait(timeout=3.0)
except subprocess.TimeoutExpired:
    proc.kill()

# Clean up temp files
for f in [TEST_SRC, TEST_BIN]:
    try:
        os.remove(f)
    except OSError:
        pass

print(f"\n  Results: {passed} passed, {failed} failed")
sys.exit(0 if failed == 0 else 1)
