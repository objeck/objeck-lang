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

Three things about how the CLI shape is driven, all of them learned the hard way
from a macOS CI failure:

  - On POSIX it uses a pty, not a pipe. macOS showed exactly what a pipe costs:
    obd printed its banner and then sat there, never processing the `r`, because
    readline() does not hand back a line from a non-tty there (-lreadline
    resolves to libedit). run_debugger_tests.sh has always driven obd's CLI
    through `expect`, which allocates a pty, so a pipe was never the supported
    shape on POSIX -- this test was the outlier, not obd.
  - It never asks obd to read a second command. It sends `r` and from then on
    only observes, then kills the process. obd's command loop repeats the
    previous command on an empty read, so any input handling that hands back
    an empty line would re-run the program in a loop rather than quit. The
    assertion needs no quit anyway: the crash was on the wake, so "still healthy
    after the wake" is the property being tested.
  - It never reads obd's output on the main thread. A reader thread drains it,
    so buffering differences cannot wedge the test -- they can only change what
    it reports.

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
BASE_PORT = 19993     # the fixture's default; each iteration gets its own

# Every iteration runs on its own port. Sharing one is not safe on Windows:
# SO_REUSEADDR there permits binding a port that already has a LIVE listener --
# unlike POSIX, where it only relaxes TIME_WAIT -- so an obd left over from an
# earlier iteration and the new one both bind successfully, and the client can be
# handed the stale listener. It connects, nothing echoes, and the iteration fails
# for a reason that has nothing to do with the defect. Reproduced 4 times in 10
# cycles while chasing exactly that, with the fixture printing "ok: client
# connected" immediately followed by "FAIL: server echoed the request".
#
# Probing the port first cannot substitute for this. A connect probe only answers
# "is anyone accepting"; a bind probe succeeds against a live listener for the
# very reason above. Both were tried, and the bind probe made it worse.
_next_port = [BASE_PORT]


def take_port():
    port = _next_port[0]
    _next_port[0] += 1
    return port

# The crash races teardown, so a single iteration can miss it -- against a build
# with the fix reverted these report between one and three failures, and not
# always the same ones. More iterations means more chances to catch a regression.
# They are cheap (about 6s each) but their worst cases are not, so rather than
# pick a count that is only safe in the worst case, stop starting new ones once
# the global budget is spent. A healthy run gets through all of them in ~30s.
CLI_ITERATIONS = 3
DAP_ITERATIONS = 2
GLOBAL_BUDGET = 150.0

READY_BUDGET = 15.0   # obd start -> the end of its banner
RUN_BUDGET = 25.0     # `r` -> the program's PASS line
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


class CliDriver:
    """Runs obd's interactive CLI and drains its output into a buffer.

    On POSIX this has to be a pty, not a pipe. obd reads commands through
    readline(), which does not hand a line back from a non-tty on macOS, where
    -lreadline resolves to libedit -- obd printed its banner and then sat there,
    never processing the `r`. A pipe was never the supported shape:
    run_debugger_tests.sh has always driven obd's CLI through `expect`, which
    allocates a pty, on every POSIX platform. Windows has no pty and reads via
    std::getline, where a pipe is fine.

    Reading happens on a thread so a blocked or buffered read can never wedge the
    test -- it can only change what gets reported.
    """

    def __init__(self, argv, env):
        self.buf = b""
        self.lock = threading.Lock()
        self.master = None

        if os.name == "nt":
            self.proc = subprocess.Popen(
                argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, env=env, bufsize=0)
        else:
            import pty
            self.master, slave = pty.openpty()
            self.proc = subprocess.Popen(
                argv, stdin=slave, stdout=slave, stderr=slave, env=env,
                close_fds=True)
            os.close(slave)

        threading.Thread(target=self._read_loop, daemon=True).start()

    def _read_loop(self):
        while True:
            try:
                if self.master is None:
                    chunk = self.proc.stdout.read(1)
                else:
                    chunk = os.read(self.master, 4096)
            except (OSError, ValueError):
                return          # EIO on the master once the child is gone
            if not chunk:
                return
            with self.lock:
                self.buf += chunk

    def send(self, data):
        try:
            if self.master is None:
                self.proc.stdin.write(data)
                self.proc.stdin.flush()
            else:
                os.write(self.master, data)
        except OSError:
            pass

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

    def close(self):
        try:
            self.proc.kill()
            self.proc.wait(timeout=10)
        except Exception:
            pass
        if self.master is not None:
            try:
                os.close(self.master)
            except OSError:
                pass


def wake_parked_thread(port):
    """Connect to the port the debuggee's thread is parked on, unblocking its
    accept(). Returns True if the connection was made."""
    for _ in range(4):
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=2.0)
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

    port = take_port()
    cli = CliDriver([OBD, "-bin", PROG, "-src", SCRIPT_DIR,
                     "-args", str(port)], child_env())

    # Wait until obd has finished loading before sending, so the command cannot
    # land before the command loop is reading. Deliberately NOT the "> " prompt:
    # readline writes that to the terminal itself and it never reaches the pty
    # master, so waiting on it only ever burns the full budget -- measured at
    # ~13s of dead time per iteration. The last banner line does arrive.
    cli.saw(b"source file path:", READY_BUDGET)
    time.sleep(0.5)
    cli.send(b"r\n")

    # The program's own PASS line is what proves the fixture really got a thread
    # parked in Accept() rather than dying on its first syscall -- without it a
    # clean result would be meaningless.
    ran = cli.saw(b"PASS", RUN_BUDGET)
    check(label + "program reached the parked-accept state", ran,
          "no PASS within " + str(RUN_BUDGET) + "s; output so far: "
          + repr(cli.text()[-300:]))

    # obd is now back at its prompt with the run's image and heap released.
    woke = wake_parked_thread(port) if ran else False
    check(label + "client woke the parked thread", woke,
          "nothing was listening on " + str(port))

    # The crash was on the wake, so this is the assertion. obd should either be
    # sitting at its prompt (None) or have exited cleanly -- never dead of a
    # signal or an access violation.
    time.sleep(SETTLE)
    rc = cli.proc.poll()
    check(label + "obd survived the wake", not crashed(rc),
          "obd died abnormally: " + describe(rc) + "; output: "
          + repr(cli.text()[-300:]))

    # Deliberately killed rather than sent `q`: see the module docstring.
    cli.close()


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

    port = take_port()
    session = DapSession()
    session.request("launch", {"program": PROG, "sourceDir": SCRIPT_DIR,
                              "args": str(port)},
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


# All-skipped would otherwise be 0 passed / 0 failed, i.e. a green no-op.
if passed == 0 and failed == 0:
    failed += 1
    print("  [FAIL] no iteration of either shape actually ran")

summary = "  obd_teardown_test: " + str(passed) + " passed, " + str(failed) + " failed"
if skipped:
    summary += ", " + str(skipped) + " iteration(s) skipped on time"
print("\n" + summary + " (%.0fs)" % (time.time() - started_at))
sys.exit(1 if failed else 0)
