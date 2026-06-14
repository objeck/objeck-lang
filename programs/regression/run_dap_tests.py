#!/usr/bin/env python3
"""
DAP (Debug Adapter Protocol) regression tests for the Objeck debugger (obd --dap).

Drives `obd --dap` over stdio with framed JSON-RPC and asserts the editor-facing
features work: breakpoints, variables, setVariable, function breakpoints,
logpoints, restart, and exception breakpoints.

Usage:
  python3 run_dap_tests.py <bin_dir>

<bin_dir> must contain obc(.exe) and obd(.exe). The script compiles the test
programs in this directory with debug symbols, then runs the scenarios.
Exit code 0 = all passed, 1 = at least one failed.
"""
import json
import os
import queue
import subprocess
import sys
import threading
import time

REG_DIR = os.path.dirname(os.path.abspath(__file__))
PASS = 0
FAIL = 0


def log_result(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print(f"  [PASS] {name}")
    else:
        FAIL += 1
        print(f"  [FAIL] {name}  {detail}")


class DapClient:
    """Minimal DAP stdio client with a background reader."""

    def __init__(self, obd, source_dir):
        self.proc = subprocess.Popen([obd, "--dap"], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE)
        self.q = queue.Queue()
        self.seq = 0
        self.source_dir = source_dir
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        buf = b""
        while True:
            c = self.proc.stdout.read(1)
            if not c:
                break
            buf += c
            while b"\r\n\r\n" in buf:
                head, rest = buf.split(b"\r\n\r\n", 1)
                try:
                    n = int(head.split(b"Content-Length: ")[1])
                except (IndexError, ValueError):
                    buf = rest
                    continue
                if len(rest) < n:
                    break
                body, buf = rest[:n], rest[n:]
                try:
                    self.q.put(json.loads(body))
                except json.JSONDecodeError:
                    pass

    def send(self, command, args=None):
        self.seq += 1
        body = json.dumps({"seq": self.seq, "type": "request",
                           "command": command, "arguments": args or {}}).encode()
        try:
            self.proc.stdin.write(
                f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
            self.proc.stdin.flush()
        except OSError:
            pass

    def wait_for(self, pred, timeout=15):
        end = time.time() + timeout
        while time.time() < end:
            try:
                msg = self.q.get(timeout=max(0.01, end - time.time()))
            except queue.Empty:
                break
            if pred(msg):
                return msg
        return None

    def wait_response(self, command, timeout=15):
        return self.wait_for(lambda m: m.get("type") == "response"
                             and m.get("command") == command, timeout)

    def wait_stopped(self, timeout=15):
        return self.wait_for(lambda m: m.get("event") == "stopped", timeout)

    def close(self):
        self.send("disconnect")
        self.wait_response("disconnect", 5)
        try:
            return self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            return "TIMEOUT"


def compile_test(obc, src, dest, bin_dir):
    log = os.path.join(REG_DIR, "results",
                       os.path.basename(dest) + "_compile.log")
    os.makedirs(os.path.join(REG_DIR, "results"), exist_ok=True)
    # run from bin_dir so obc resolves its ../lib path
    with open(log, "wb") as f:
        rc = subprocess.run([obc, "-src", src, "-dest", dest, "-debug"],
                            stdout=f, stderr=subprocess.STDOUT,
                            cwd=bin_dir).returncode
    return rc == 0 and os.path.exists(dest)


def run_core_scenarios(obd, src_obs, obe, source_dir):
    """breakpoint -> variables -> setVariable -> function bp -> logpoint -> restart."""
    c = DapClient(obd, source_dir)
    c.send("initialize", {"adapterID": "objeck"})
    init = c.wait_response("initialize")
    caps = (init or {}).get("body", {})
    log_result("initialize capabilities", bool(init) and
               caps.get("supportsSetVariable") and
               caps.get("supportsFunctionBreakpoints") and
               caps.get("supportsRestartRequest") and
               caps.get("supportsLogPoints") and
               "exceptionBreakpointFilters" in caps,
               str(caps))

    c.send("launch", {"program": obe, "sourceDir": source_dir})
    c.wait_response("launch")

    # conditional breakpoint at line 51 (n == 3) + logpoint at 54
    c.send("setBreakpoints", {"source": {"path": src_obs}, "breakpoints": [
        {"line": 51, "condition": "n = 3"},
        {"line": 54, "logMessage": "fact n={n}"},
    ]})
    c.wait_response("setBreakpoints")
    c.send("configurationDone")

    st = c.wait_stopped()
    log_result("breakpoint stop", bool(st) and st["body"]["reason"] == "breakpoint")

    c.send("stackTrace", {"threadId": 1})
    stk = c.wait_response("stackTrace")
    frames = (stk or {}).get("body", {}).get("stackFrames", [])
    fid = frames[0]["id"] if frames else 0
    log_result("stackTrace frames", len(frames) >= 1 and frames[0]["line"] == 51)

    c.send("scopes", {"frameId": fid})
    sc = c.wait_response("scopes")
    locref = sc["body"]["scopes"][0]["variablesReference"] if sc else 0
    c.send("variables", {"variablesReference": locref})
    v = c.wait_response("variables")
    nval = next((x["value"] for x in (v or {}).get("body", {}).get("variables", [])
                 if x["name"] == "n"), None)
    log_result("variables (n == 3)", nval == "3", f"got {nval}")

    c.send("setVariable", {"variablesReference": locref, "name": "n", "value": "9"})
    sv = c.wait_response("setVariable")
    log_result("setVariable", bool(sv) and sv.get("success")
               and sv.get("body", {}).get("value") == "9")

    c.send("variables", {"variablesReference": locref})
    v2 = c.wait_response("variables")
    nval2 = next((x["value"] for x in (v2 or {}).get("body", {}).get("variables", [])
                  if x["name"] == "n"), None)
    log_result("setVariable readback", nval2 == "9", f"got {nval2}")

    # restart: should stop again at the same conditional breakpoint
    c.send("restart")
    c.wait_response("restart")
    st2 = c.wait_stopped()
    log_result("restart re-stop", bool(st2) and st2["body"]["reason"] == "breakpoint")
    c.send("stackTrace", {"threadId": 1})
    stk2 = c.wait_response("stackTrace")
    log_result("restart stackTrace usable",
               bool(stk2) and len(stk2.get("body", {}).get("stackFrames", [])) >= 1)

    # logpoints fire while running to completion
    logs = []
    c.send("continue", {"threadId": 1})
    end = time.time() + 8
    while time.time() < end:
        m = c.wait_for(lambda x: x.get("event") in ("output", "terminated"), 8)
        if not m:
            break
        if m.get("event") == "output":
            logs.append(m["body"].get("output", ""))
        else:
            break
    log_result("logpoints emitted", any("fact n=" in s for s in logs),
               f"{logs[:3]}")

    code = c.close()
    log_result("clean exit", code == 0, f"exit={code}")


def run_function_breakpoint(obd, obe, source_dir):
    c = DapClient(obd, source_dir)
    c.send("initialize", {"adapterID": "objeck"})
    c.wait_response("initialize")
    c.send("launch", {"program": obe, "sourceDir": source_dir})
    c.wait_response("launch")
    c.send("setFunctionBreakpoints", {"breakpoints": [{"name": "Main->Factorial"}]})
    fb = c.wait_response("setFunctionBreakpoints")
    verified = (fb or {}).get("body", {}).get("breakpoints", [{}])
    log_result("function breakpoint verified",
               bool(verified) and verified[0].get("verified") is True)
    c.send("configurationDone")
    st = c.wait_stopped()
    ok = bool(st) and st["body"]["reason"] == "breakpoint"
    c.send("stackTrace", {"threadId": 1})
    stk = c.wait_response("stackTrace")
    name = (stk or {}).get("body", {}).get("stackFrames", [{}])[0].get("name", "")
    log_result("function breakpoint stop in Factorial", ok and "Factorial" in name)
    c.send("continue", {"threadId": 1})
    c.wait_for(lambda m: m.get("event") == "terminated", 8)
    c.close()


def run_exception_breakpoint(obd, obe, source_dir):
    c = DapClient(obd, source_dir)
    c.send("initialize", {"adapterID": "objeck"})
    init = c.wait_response("initialize")
    filters = (init or {}).get("body", {}).get("exceptionBreakpointFilters", [])
    log_result("exception filter advertised",
               any(f.get("filter") == "uncaught" for f in filters))
    c.send("launch", {"program": obe, "sourceDir": source_dir})
    c.wait_response("launch")
    c.send("setExceptionBreakpoints", {"filters": ["uncaught"]})
    c.wait_response("setExceptionBreakpoints")
    c.send("configurationDone")
    st = c.wait_stopped()
    log_result("exception breakpoint stop",
               bool(st) and st["body"]["reason"] == "exception")
    if st:
        c.send("continue", {"threadId": 1})
    c.wait_for(lambda m: m.get("event") == "terminated", 8)
    c.close()


def main():
    if len(sys.argv) < 2:
        print("usage: run_dap_tests.py <bin_dir>")
        return 2
    bin_dir = sys.argv[1]
    exe = ".exe" if os.name == "nt" else ""
    obc = os.path.join(bin_dir, "obc" + exe)
    obd = os.path.join(bin_dir, "obd" + exe)
    if not os.path.exists(obd):
        print(f"ERROR: obd not found at {obd}")
        return 1

    print("========================================")
    print("  Objeck DAP Test Suite")
    print("========================================")

    core_src = os.path.join(REG_DIR, "debugger_test.obs")
    core_obe = os.path.join(REG_DIR, "dap_debugger_test.obe")
    exc_src = os.path.join(REG_DIR, "dap_exception_test.obs")
    exc_obe = os.path.join(REG_DIR, "dap_exception_test.obe")

    if not compile_test(obc, core_src, core_obe, bin_dir):
        print("ERROR: failed to compile debugger_test.obs")
        return 1
    if not compile_test(obc, exc_src, exc_obe, bin_dir):
        print("ERROR: failed to compile dap_exception_test.obs")
        return 1

    print("\nCore scenarios (breakpoint/variables/setVariable/logpoint/restart):")
    run_core_scenarios(obd, core_src, core_obe, REG_DIR)
    print("\nFunction breakpoints:")
    run_function_breakpoint(obd, core_obe, REG_DIR)
    print("\nException breakpoints:")
    run_exception_breakpoint(obd, exc_obe, REG_DIR)

    for f in (core_obe, exc_obe):
        try:
            os.remove(f)
        except OSError:
            pass

    print("\n========================================")
    print(f"  Results: {PASS} passed, {FAIL} failed")
    print("========================================")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
