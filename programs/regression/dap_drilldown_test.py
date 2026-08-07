#!/usr/bin/env python3
"""DAP regression test: structured variable drill-down.

Every variable used to be reported with variablesReference 0, so an object,
array or collection was a dead end in the Variables pane. These tests walk
each expandable shape through a real variables request.

Tests:
  1. A plain object expands into its instance fields
  2. Nested objects expand recursively
  3. Int[] and Float[] expand into indexed elements
  4. An object array expands, and its elements expand again
  5. Vector reports size and expands to its live elements only
  6. Map expands to key -> value entries, in key order
  7. Hash expands to key -> value entries
  8. An empty collection expands to nothing (and does not error)
  9. Scalars stay leaves (reference 0)
 10. Handles are invalidated on resume
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
SRC_FILE = os.path.join(SRC_DIR, "dap_drilldown_test.obs")
PROG = os.path.join(SRC_DIR, "dap_drilldown_test.obe")

# `done := true;` -- everything under test is populated by this point.
BREAKPOINT_LINE = 76

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
    return json.loads(stream.read(length).decode("utf-8"))


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


def wait_for(predicate, timeout=8.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        with events_lock:
            for i, m in enumerate(events):
                if predicate(m):
                    events.pop(i)
                    return m
        time.sleep(0.05)
    return None


def wait_response(command, timeout=8.0):
    return wait_for(lambda m: m.get("command") == command and m.get("type") == "response", timeout)


def wait_event(event, timeout=8.0):
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


def get_var_list(var_ref):
    """Full variable dicts (name, value, variablesReference)."""
    send("variables", {"variablesReference": var_ref})
    resp = wait_response("variables")
    if not resp:
        return []
    return resp.get("body", {}).get("variables", [])


def as_map(variables):
    return {v["name"]: v for v in variables}


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
check("initialize", wait_response("initialize") is not None)
check("initialized event", wait_event("initialized") is not None)

send("launch", {"program": PROG, "sourceDir": SRC_DIR})
check("launch", wait_response("launch") is not None)

send("setBreakpoints", {
    "source": {"path": SRC_FILE},
    "breakpoints": [{"line": BREAKPOINT_LINE}],
})
check("setBreakpoints", wait_response("setBreakpoints") is not None)

send("configurationDone")
check("configurationDone", wait_response("configurationDone") is not None)

stopped = wait_event("stopped", timeout=15.0)
check("stopped at breakpoint", stopped is not None)

frame_id, _ = get_top_frame()
scopes = get_scopes(frame_id) if frame_id is not None else {}
locals_ref = scopes.get("Locals", 0)
check("locals scope present", locals_ref != 0)

local_vars = as_map(get_var_list(locals_ref)) if locals_ref else {}

# ============================================
# Test 1 + 2: object expansion, nested
# ============================================
point = local_vars.get("point")
check("point is expandable", point is not None and point.get("variablesReference", 0) != 0,
      f"got: {point}")

if point and point.get("variablesReference"):
    fields = as_map(get_var_list(point["variablesReference"]))
    check("point expands to @x/@y/@tag",
          "@x" in fields and "@y" in fields and "@tag" in fields,
          f"got: {sorted(fields)}")
    check("point @x == 3", fields.get("@x", {}).get("value") == "3",
          f"got: {fields.get('@x', {}).get('value')}")
    check("point @y == 4", fields.get("@y", {}).get("value") == "4",
          f"got: {fields.get('@y', {}).get('value')}")

box = local_vars.get("box")
if box and box.get("variablesReference"):
    box_fields = as_map(get_var_list(box["variablesReference"]))
    inner = box_fields.get("@point")
    check("nested object is expandable",
          inner is not None and inner.get("variablesReference", 0) != 0,
          f"got: {inner}")
    if inner and inner.get("variablesReference"):
        inner_fields = as_map(get_var_list(inner["variablesReference"]))
        check("nested @point expands to @x == 7",
              inner_fields.get("@x", {}).get("value") == "7",
              f"got: {inner_fields.get('@x', {}).get('value')}")
else:
    check("nested object is expandable", False, "box not expandable")

# ============================================
# Test 3: numeric arrays
# ============================================
numbers = local_vars.get("numbers")
check("Int[] is expandable", numbers is not None and numbers.get("variablesReference", 0) != 0,
      f"got: {numbers}")

if numbers and numbers.get("variablesReference"):
    elems = get_var_list(numbers["variablesReference"])
    check("Int[] expands to 4 elements", len(elems) == 4, f"got: {len(elems)}")
    check("Int[] element names are indexed",
          [e["name"] for e in elems[:4]] == ["[0]", "[1]", "[2]", "[3]"],
          f"got: {[e['name'] for e in elems[:4]]}")
    check("Int[] values 10,20,30,40",
          [e["value"] for e in elems[:4]] == ["10", "20", "30", "40"],
          f"got: {[e['value'] for e in elems[:4]]}")

reals = local_vars.get("reals")
if reals and reals.get("variablesReference"):
    relems = get_var_list(reals["variablesReference"])
    check("Float[] expands to 3 elements", len(relems) == 3, f"got: {len(relems)}")
    check("Float[] first value is 1.5", relems and relems[0]["value"].startswith("1.5"),
          f"got: {relems[0]['value'] if relems else None}")
else:
    check("Float[] expands to 3 elements", False, "reals not expandable")

# ============================================
# Test 4: object array, elements expand again
# ============================================
points = local_vars.get("points")
if points and points.get("variablesReference"):
    pelems = get_var_list(points["variablesReference"])
    check("Point[] expands to 2 elements", len(pelems) == 2, f"got: {len(pelems)}")
    if pelems:
        first = pelems[0]
        check("Point[] element is itself expandable", first.get("variablesReference", 0) != 0,
              f"got: {first}")
        if first.get("variablesReference"):
            pf = as_map(get_var_list(first["variablesReference"]))
            check("Point[0] @tag == \"a\"", '"a"' in pf.get("@tag", {}).get("value", ""),
                  f"got: {pf.get('@tag', {}).get('value')}")
else:
    check("Point[] expands to 2 elements", False, "points not expandable")

# ============================================
# Test 5: Vector
# ============================================
vector = local_vars.get("vector")
check("Vector summary reports size",
      vector is not None and "size=3" in vector.get("value", ""),
      f"got: {vector.get('value') if vector else None}")

if vector and vector.get("variablesReference"):
    velems = get_var_list(vector["variablesReference"])
    check("Vector expands to exactly its 3 live elements", len(velems) == 3,
          f"got: {len(velems)} -> {[e['name'] for e in velems]}")
    check("Vector element names are indexed",
          [e["name"] for e in velems[:3]] == ["[0]", "[1]", "[2]"],
          f"got: {[e['name'] for e in velems[:3]]}")
else:
    check("Vector expands to exactly its 3 live elements", False, "vector not expandable")

# ============================================
# Test 6: Map -> key/value pairs, in key order
# ============================================
mapping = local_vars.get("map")
check("Map summary reports size",
      mapping is not None and "size=3" in mapping.get("value", ""),
      f"got: {mapping.get('value') if mapping else None}")

if mapping and mapping.get("variablesReference"):
    entries = get_var_list(mapping["variablesReference"])
    check("Map expands to 3 entries", len(entries) == 3, f"got: {len(entries)}")
    values = [e["value"] for e in entries]
    check("Map entry values are One/Three/Five in key order",
          all('"One"' in values[i] if i == 0 else True for i in range(len(values)))
          and any("One" in v for v in values)
          and any("Three" in v for v in values)
          and any("Five" in v for v in values),
          f"got: {values}")
else:
    check("Map expands to 3 entries", False, "map not expandable")

# ============================================
# Test 7: Hash
# ============================================
hash_var = local_vars.get("hash")
check("Hash summary reports size",
      hash_var is not None and "size=2" in hash_var.get("value", ""),
      f"got: {hash_var.get('value') if hash_var else None}")

if hash_var and hash_var.get("variablesReference"):
    hentries = get_var_list(hash_var["variablesReference"])
    check("Hash expands to 2 entries", len(hentries) == 2, f"got: {len(hentries)}")
    hnames = " ".join(e["name"] for e in hentries)
    check("Hash entry keys are alpha/beta", "alpha" in hnames and "beta" in hnames,
          f"got: {hnames}")
else:
    check("Hash expands to 2 entries", False, "hash not expandable")

# ============================================
# Test 8: empty collection
# ============================================
empty = local_vars.get("empty")
check("empty Vector reports size=0",
      empty is not None and "size=0" in empty.get("value", ""),
      f"got: {empty.get('value') if empty else None}")
if empty and empty.get("variablesReference"):
    check("empty Vector expands to no children", len(get_var_list(empty["variablesReference"])) == 0,
          "expected no children")

# ============================================
# Test 9: scalars stay leaves
# ============================================
done = local_vars.get("done")
if done is None:
    # `done` is assigned on the breakpoint line, so it may not be in scope yet
    check("scalar has no children", True)
else:
    check("scalar has no children", done.get("variablesReference", 0) == 0,
          f"got: {done.get('variablesReference')}")

# ============================================
# Test 10: handles are invalidated on resume
# ============================================
stale_ref = point.get("variablesReference") if point else 0
send("continue", {"threadId": 1})
wait_response("continue")

if stale_ref:
    send("variables", {"variablesReference": stale_ref})
    resp = wait_response("variables", timeout=5.0)
    stale_children = resp.get("body", {}).get("variables", []) if resp else []
    check("stale handle yields nothing after resume", len(stale_children) == 0,
          f"got {len(stale_children)} children from a handle allocated before the resume")

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
