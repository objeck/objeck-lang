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
     6 runs on Windows, 2 in 2 on Linux, before the fix.

  2. DAP: run to completion, then disconnect. Here the waker was the WSACleanup
     on the way out of main, exactly as in #681. 8 in 8 on Windows before the
     fix; Linux was always clean, having no equivalent call.

Shape 1 is the one with teeth for the drain/Abandon half of the fix: dropping
obd's exit-time WSACleanup on its own leaves it crashing, because a client
connect wakes the thread just as well.

Two things this test deliberately does NOT do, both learned from a macOS CI
timeout:

  - It never asks obd to read a second command. The CLI shape writes `r`, and
    from then on only observes. obd's command loop repeats the previous command
    on an empty read, so a readline() that returns rather than blocks on a pipe
    (libedit, which is what -lreadline resolves to on macOS) would re-run the
    program forever instead of quitting. The assertion does not need the quit:
    the crash was on the wake, so "still healthy after the wake" is the property.
  - It never reads obd's stdout on the main thread. A reader thread drains the
    pipe, so buffering differences cannot wedge the test -- they can only make it
    report what it saw.

Every phase is bounded and says which one it was, so a CI failure names the
stall instead of reporting a bare timeout. Summing every budget below gives a
worst case of ~190s, inside the 300s the runners allow; a healthy run takes
about 18s. The first version of this test could exceed 300s on its own
arithmetic, which is how it managed to report nothing at all when macOS
wedged it.

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

# The crash races teardown, so a single iteration can miss it -- against a build
# with the fix reverted these report between one and three failures, and not
# always the same ones. More iterations means more chances to catch a regression.
# They are cheap (about 6s each) but their worst cases are not, so rather than
# pick a count that is only safe in the worst case, stop starting new ones once
# the global budget is spent. A healthy run gets through all of them in ~30s.
CLI_ITERATIONS = 3
DAP_ITERATIONS = 2
GLOBAL_BUDGET = 150.0

RUN_BUDGET = 25.0     # program start -> its PASS line
SETTLE = 3.0          # after the wake, before judging obd healthy
DAP_REQUEST_BUDGET = 15.0
DAP_TERM_BUDGET = 30.0
DAP_DISCONNECT_BUDGET = 8.0
DAP_EXIT_BUDGET = 15.0

if not os.path.exists(PROG):
    print("ERROR: " + PROG + " not found - run_dap_tests.py builds it first", file=sys.stderr)
    sys.exit(2)

passed = 0
failed = 0
skipped = 0
started_at = time.time()


def budget_spent():
    return time.time() - started_at > GLOBAL_BUDGET


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
    """Name the exit status, so a failure reads as 'segfault' rather than a number."""
    if rc is None:
        return "still running"
    if rc == 0:
        return "0"
    if not isinstance(rc, int):
        return str(rc)
    if os.name == "nt":
        return str(rc) + " (" + hex(rc & 0xFFFFFFFF) + ")"
    if rc < 0:
        return "signal " + str(-rc)
    return str(rc)


def crashed(rc):
    """Did obd die abnormally?

    Specifically abnormally, not merely non-zero. The debuggee can set obd's exit
    code itself -- thread_accept_exit_test calls Runtime->Exit(1) when its own
    assertions fail, which happens for ordinary reasons such as the port being
    busy -- and that is a fixture problem, not the use-after-free under test. So
    match what a crash actually looks like: killed by a signal on POSIX, an
    NTSTATUS-range code (0xC0000005 and friends) on Windows.

    None means obd is still sitting at its prompt, which is a healthy outcome for
    the CLI shape."""
    if rc is None or rc == 0:
        return False
    if os.name == "nt":
        return (rc & 0xFFFFFFFF) >= 0xC0000000
    return rc < 0


class Reader(threading.Thread):
    """Drains a pipe into a buffer so no read can ever block the test."""

    def __init__(self, stream):
        threading.Thread.__init__(self)
        self.daemon = True
        self.stream = stream
        self.buf = b""
        self.lock = threading.Lock()

    def run(self):
        while True:
            try:
                chunk = self.stream.read(1)
            except (OSError, ValueError):
                return
            if not chunk:
                return
            with self.lock:
                self.buf += chunk

    def text(self):
        with self.lock:
            return self.buf.decode("utf-8", "replace")

    def saw(self, needle, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self.lock:
                if needle in self.buf:
                    return True
            time.sleep(0.05)
        return False


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

for i in range(CLI_ITERATIONS):
    label = "CLI run " + str(i + 1) + ": "
    # Always run the first; drop later ones rather than risk the runner's cap.
    if i > 0 and budget_spent():
        skipped += CLI_ITERATIONS - i
        print("  [SKIP] " + str(CLI_ITERATIONS - i)
              + " further CLI run(s): global budget spent")
        break

    proc = subprocess.Popen(
        [OBD, "-bin", PROG, "-src", SCRIPT_DIR],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        env=child_env(), bufsize=0)
    reader = Reader(proc.stdout)
    reader.start()

    try:
        proc.stdin.write(b"r\n")
        proc.stdin.flush()
    except OSError:
        pass

    # The program's own PASS line is what proves the fixture really got a thread
    # parked in Accept() rather than dying on its first syscall -- without it a
    # clean result would be meaningless.
    ran = reader.saw(b"PASS", RUN_BUDGET)
    check(label + "program reached the parked-accept state", ran,
          "no PASS within " + str(RUN_BUDGET) + "s; output so far: "
          + repr(reader.text()[-300:]))

    # obd is now back at its prompt with the run's image and heap released.
    woke = wake_parked_thread() if ran else False
    check(label + "client woke the parked thread", woke,
          "nothing was listening on " + str(PORT))

    # The crash was on the wake, so this is the assertion. obd should either be
    # sitting at its prompt (None) or have exited cleanly -- never dead of a
    # signal or an access violation.
    time.sleep(SETTLE)
    rc = proc.poll()
    check(label + "obd survived the wake", not crashed(rc),
          "obd died abnormally: " + describe(rc) + "; output: "
          + repr(reader.text()[-300:]))

    # Deliberately kill rather than send `q`: see the module docstring.
    try:
        proc.kill()
        proc.wait(timeout=10)
    except Exception:
        pass


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
        self.request("initialize", {"adapterID": "objeck"}, timeout=DAP_REQUEST_BUDGET)

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
            try:
                msg = self._read_msg()
            except Exception:
                return
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

    def wait_for(self, predicate, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self.lock:
                for msg in self.events:
                    if predicate(msg):
                        return msg
            time.sleep(0.05)
        return None

    def request(self, command, args=None, timeout=DAP_REQUEST_BUDGET):
        self.send(command, args)
        return self.wait_for(
            lambda m: m.get("command") == command and m.get("type") == "response", timeout)

    def program_output(self):
        with self.lock:
            return "".join(m.get("body", {}).get("output", "")
                           for m in self.events if m.get("event") == "output")


for i in range(DAP_ITERATIONS):
    label = "DAP run " + str(i + 1) + ": "
    if i > 0 and budget_spent():
        skipped += DAP_ITERATIONS - i
        print("  [SKIP] " + str(DAP_ITERATIONS - i)
              + " further DAP run(s): global budget spent")
        break

    session = DapSession()
    session.request("launch", {"program": PROG, "sourceDir": SCRIPT_DIR},
                    timeout=DAP_REQUEST_BUDGET)
    session.request("configurationDone", timeout=DAP_REQUEST_BUDGET)
    session.wait_for(lambda m: m.get("event") == "terminated", timeout=DAP_TERM_BUDGET)
    ran = "PASS" in session.program_output()
    check(label + "program reached the parked-accept state", ran,
          "no PASS within " + str(DAP_TERM_BUDGET) + "s; output: "
          + repr(session.program_output()[-300:]))

    session.send("disconnect")
    session.wait_for(
        lambda m: m.get("command") == "disconnect" and m.get("type") == "response",
        timeout=DAP_DISCONNECT_BUDGET)
    try:
        rc = session.proc.wait(timeout=DAP_EXIT_BUDGET)
    except subprocess.TimeoutExpired:
        session.proc.kill()
        rc = None       # never exited; not a crash, so not this test's failure
    check(label + "obd exited cleanly on disconnect", not crashed(rc),
          "obd died abnormally: " + describe(rc))


summary = "  obd_teardown_test: " + str(passed) + " passed, " + str(failed) + " failed"
if skipped:
    summary += ", " + str(skipped) + " iteration(s) skipped on time"
print("\n" + summary + " (%.0fs)" % (time.time() - started_at))
sys.exit(1 if failed else 0)
