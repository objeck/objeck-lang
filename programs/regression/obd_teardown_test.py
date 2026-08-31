#!/usr/bin/env python3
"""obd must not free the debuggee's program image while one of its threads is
parked in a syscall.

This is issue #681 one layer up. obd hosts the same VM as obr, so a debugged
program can leave threads live when Main returns -- a server sitting in Accept()
is the ordinary resting state, not an edge case -- and obd's ClearProgram frees
the bytecode image, the StackProgram, and the entire GC heap
(MemoryManager::Clear releases the young region, every old-generation object,
and the locks the VM takes on the way in). Whatever wakes that thread next runs
it against all three.

Two shapes, because they fail for different reasons and fixing one does not
imply the other:

  1. CLI: run the program, then -- with obd still sitting at its prompt and the
     run already torn down -- connect a client to the port the parked thread is
     on. The wake source here is an ordinary TCP connection, so this shape is
     not Windows-specific and no WSACleanup is involved. 6 access violations in
     6 runs before the fix.

  2. DAP: run to completion, then disconnect. Here the waker was the WSACleanup
     on the way out of main, exactly as in #681. 8 in 8 before the fix.

Shape 1 is the one with teeth for the drain/Abandon half of the fix: dropping
obd's exit-time WSACleanup on its own leaves it crashing, because a client
connect wakes the thread just as well.

Driven by run_dap_tests.py, which passes DAP_TEST_DEPLOY_DIR.
"""
import json
import os
import socket
import subprocess
import sys
import threading
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
PLATFORM = os.environ.get("DAP_TEST_PLATFORM", "x64")

DEPLOY_DIR = None
for candidate in [
    os.path.abspath(p) for p in [os.environ.get("DAP_TEST_DEPLOY_DIR")] if p
] + [
    os.path.join(REPO_ROOT, "core", "release", "deploy-" + PLATFORM),
    os.path.join(REPO_ROOT, "core", "release", "deploy"),
]:
    if os.path.isdir(candidate):
        DEPLOY_DIR = candidate
        break
if DEPLOY_DIR is None:
    print("ERROR: could not find deploy directory under core/release/", file=sys.stderr)
    sys.exit(2)

EXE = ".exe" if os.name == "nt" else ""
BIN_DIR = os.path.join(DEPLOY_DIR, "bin")
OBD = os.path.join(BIN_DIR, "obd" + EXE)
LIB_PATH = os.path.join(DEPLOY_DIR, "lib")

# The fixture is the VM-side #681 reproducer, reused rather than duplicated: the
# defect and the program shape are the same, only the host differs.
PROG = os.path.join(SCRIPT_DIR, "thread_accept_exit_test.obe")
PORT = 19993          # the port ParkedAcceptServer listens on
ITERATIONS = 2

if not os.path.exists(PROG):
    print("ERROR: " + PROG + " not found - run_dap_tests.py builds it first", file=sys.stderr)
    sys.exit(2)

passed = 0
failed = 0


def check(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
        print("  [PASS] " + name)
    else:
        failed += 1
        print("  [FAIL] " + name + ": " + str(detail))


def child_env():
    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = LIB_PATH + os.sep
    native = os.path.join(LIB_PATH, "native")
    if sys.platform == "darwin":
        env["DYLD_LIBRARY_PATH"] = native + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
    elif os.name != "nt":
        env["LD_LIBRARY_PATH"] = native + os.pathsep + env.get("LD_LIBRARY_PATH", "")
    return env


def describe(rc):
    """Name the exit code, so a failure reads as 'segfault' rather than a number."""
    if rc == 0:
        return "0"
    if not isinstance(rc, int):
        return str(rc)
    if os.name == "nt":
        return str(rc) + " (" + hex(rc & 0xFFFFFFFF) + ")"
    if rc < 0:
        return "signal " + str(-rc)
    return str(rc)


def wake_parked_thread():
    """Connect to the port the debuggee's thread is parked on, unblocking its
    accept(). Returns True if the connection was made."""
    for _ in range(4):
        try:
            sock = socket.create_connection(("127.0.0.1", PORT), timeout=2.0)
            try:
                sock.sendall(b"ping\r\n")
                sock.settimeout(1.0)
                try:
                    sock.recv(64)
                except OSError:
                    pass          # a halted thread answers nothing; that is fine
            finally:
                sock.close()
            return True
        except OSError:
            time.sleep(0.3)
    return False


# ============================================================
# Shape 1: CLI -- wake the parked thread after the run is torn down
# ============================================================
print("obd teardown with a thread parked in accept():")

for i in range(ITERATIONS):
    proc = subprocess.Popen(
        [OBD, "-bin", PROG, "-src", SCRIPT_DIR],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        env=child_env(), bufsize=0)
    proc.stdin.write(b"r\n")
    proc.stdin.flush()

    # Read until the program's own PASS line: that is what proves the fixture
    # really got a thread parked in Accept() rather than dying on its first
    # syscall, which would make a clean exit meaningless.
    out = b""
    deadline = time.time() + 90
    while b"PASS" not in out and time.time() < deadline and proc.poll() is None:
        char = proc.stdout.read(1)
        if not char:
            break
        out += char
    ran = b"PASS" in out

    woke = wake_parked_thread() if ran else False
    time.sleep(0.5)

    try:
        proc.stdin.write(b"q\n")
        proc.stdin.flush()
    except OSError:
        pass
    try:
        rc = proc.wait(timeout=60)
    except subprocess.TimeoutExpired:
        proc.kill()
        rc = "timeout"

    label = "CLI run " + str(i + 1) + ": "
    check(label + "program reached the parked-accept state", ran,
          out.decode("utf-8", "replace")[-300:])
    check(label + "client woke the parked thread", woke,
          "nothing was listening on " + str(PORT))
    check(label + "obd exited cleanly after the wake", rc == 0,
          "exit " + describe(rc))


# ============================================================
# Shape 2: DAP -- run to completion, then end the session
# ============================================================
class DapSession:
    """One obd --dap session."""

    def __init__(self):
        self.proc = subprocess.Popen(
            [OBD, "--dap"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, env=child_env(), bufsize=0)
        self.events = []
        self.lock = threading.Lock()
        self.seq = 1
        threading.Thread(target=self._reader, daemon=True).start()
        self.request("initialize", {"adapterID": "objeck"})

    def _read_msg(self):
        header = b""
        while not header.endswith(b"\r\n\r\n"):
            char = self.proc.stdout.read(1)
            if not char:
                return None
            header += char
        length = int(header.decode().split("Content-Length: ")[1].split("\r\n")[0])
        return json.loads(self.proc.stdout.read(length).decode("utf-8"))

    def _reader(self):
        while True:
            msg = self._read_msg()
            if msg is None:
                return
            with self.lock:
                self.events.append(msg)

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

    def wait_for(self, predicate, timeout=90.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self.lock:
                for msg in self.events:
                    if predicate(msg):
                        return msg
            time.sleep(0.05)
        return None

    def request(self, command, args=None, timeout=90.0):
        self.send(command, args)
        return self.wait_for(
            lambda m: m.get("command") == command and m.get("type") == "response", timeout)

    def program_output(self):
        with self.lock:
            return "".join(m.get("body", {}).get("output", "")
                           for m in self.events if m.get("event") == "output")


for i in range(ITERATIONS):
    session = DapSession()
    session.request("launch", {"program": PROG, "sourceDir": SCRIPT_DIR})
    session.request("configurationDone")
    session.wait_for(lambda m: m.get("event") == "terminated", timeout=120.0)
    ran = "PASS" in session.program_output()

    session.send("disconnect")
    session.wait_for(
        lambda m: m.get("command") == "disconnect" and m.get("type") == "response",
        timeout=30.0)
    try:
        rc = session.proc.wait(timeout=60)
    except subprocess.TimeoutExpired:
        session.proc.kill()
        rc = "timeout"

    label = "DAP run " + str(i + 1) + ": "
    check(label + "program reached the parked-accept state", ran,
          session.program_output()[-300:])
    check(label + "obd exited cleanly on disconnect", rc == 0,
          "exit " + describe(rc))


print("\n  obd_teardown_test: " + str(passed) + " passed, " + str(failed) + " failed")
sys.exit(1 if failed else 0)
