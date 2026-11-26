#!/usr/bin/env python3
"""
The MCP server library over its stdio transport: a scripted client in Python
starts a server process (mcp_stdio_fixture.obs, built with -lib mcp) and speaks
newline-delimited JSON-RPC to it, as Claude Desktop or Claude Code does.

Usage: mcp_stdio_test.py <bin_dir>

<bin_dir> holds obc and obr; the libraries are read from the sibling lib/
directory, as check_funcref_range.py does. No network is used.

What it covers, against a real process:
  the handshake and version negotiation; ping, tools/list, tools/call,
  resources/list, resources/templates/list, resources/read, prompts/list,
  prompts/get; malformed JSON, invalid requests, an unknown method, bad
  parameters; a tool that fails (isError) and one that raises; a request that is
  cancelled, with its progress notifications and no reply; a message of a
  megabyte in each direction and one over the limit; blank lines, CRLF line
  endings and non-ASCII text; that standard output carries nothing but JSON-RPC;
  that the server ends when standard input does; and the Objeck client library
  against the same server (the round trip in one run).

What an independent review of the library found, each against a real process:
  two messages on one line are a parse error, not one answer and one silence;
  a request id names one call at a time (a second tools/call under an id still
  at work is refused, and the id is free again afterwards); a cancellation
  written on the line after its request is heard, a hundred times out of a
  hundred; more calls at once than the server runs at once are all answered, and
  one that is still waiting can be cancelled; the threads that run the calls are
  joined (handles on Windows, address space on Linux do not grow with the number
  of calls); a UTF-16 line cut inside a character is a parse error; and
  over HTTP a cancellation reaches only the session it came from, and a refused
  initialize opens no session.
"""
import json, os, queue, subprocess, sys, tempfile, threading, time, urllib.error, urllib.request

EXE = ".exe" if os.name == "nt" else ""
LIBS = "gen_collect,json,net,net_server,cipher,misc,mcp"


class Server:
    """A server process and a reader thread that queues every line it writes."""

    def __init__(self, obr, obe, env, cwd):
        self.proc = subprocess.Popen([obr, obe, "--stdio"], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     env=env, cwd=cwd)
        self.lines = queue.Queue()
        self.raw = []
        threading.Thread(target=self._read, daemon=True).start()
        threading.Thread(target=lambda: self.proc.stderr.read(), daemon=True).start()

    def _read(self):
        for line in self.proc.stdout:
            self.raw.append(line)
            self.lines.put(line)
        self.lines.put(None)

    def send_raw(self, data):
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def send(self, message):
        self.send_raw(json.dumps(message).encode("utf-8") + b"\n")

    def recv(self, timeout=20):
        """The next line as JSON; None at the end of output or on a timeout."""
        try:
            line = self.lines.get(timeout=timeout)
        except queue.Empty:
            return None
        if line is None:
            return None
        return json.loads(line.decode("utf-8"))

    def recv_id(self, want, timeout=20):
        """Skips notifications until the reply with this id arrives."""
        end = time.time() + timeout
        while time.time() < end:
            message = self.recv(max(0.1, end - time.time()))
            if message is None:
                return None
            if message.get("id") == want:
                return message
        return None

    def call(self, id_, method, params=None, timeout=20):
        request = {"jsonrpc": "2.0", "id": id_, "method": method}
        if params is not None:
            request["params"] = params
        self.send(request)
        return self.recv_id(id_, timeout)

    def quiet(self, seconds):
        """True if nothing arrives within the time."""
        try:
            line = self.lines.get(timeout=seconds)
        except queue.Empty:
            return True
        self.lines.put(line)
        return False

    def close(self):
        try:
            self.proc.stdin.close()
        except OSError:
            pass
        try:
            return self.proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            return None


def run(cmd, env, cwd, timeout=300):
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, timeout=timeout)
    return p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace")


def resources(pid):
    """What a thread that is never joined keeps: (count, what is counted), None where it cannot be read."""
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes
        k = ctypes.windll.kernel32
        k.OpenProcess.restype = wintypes.HANDLE
        h = k.OpenProcess(0x1000, False, pid)   # PROCESS_QUERY_LIMITED_INFORMATION
        if not h:
            return None
        count = wintypes.DWORD()
        ok = k.GetProcessHandleCount(wintypes.HANDLE(h), ctypes.byref(count))
        k.CloseHandle(wintypes.HANDLE(h))
        return (count.value, "handles") if ok else None
    try:
        with open("/proc/%d/status" % pid) as f:
            for l in f:
                if l.startswith("VmSize:"):
                    return (int(l.split()[1]) // 1024, "MB of address space")
    except OSError:
        pass
    return None


def review_stdio(obr, obe, env, work, init, check):
    """Standard input and output: one message a line, ids, cancellation, the workers."""
    def call_add(id_, a, b):
        return {"jsonrpc": "2.0", "id": id_, "method": "tools/call", "params": {"name": "add", "arguments": {"a": a, "b": b}}}

    def slow(id_, token=None):
        params = {"name": "slow", "arguments": {}}
        if token:
            params["_meta"] = {"progressToken": token}
        return {"jsonrpc": "2.0", "id": id_, "method": "tools/call", "params": params}

    def cancel(id_):
        return {"jsonrpc": "2.0", "method": "notifications/cancelled", "params": {"requestId": id_}}

    def line(message):
        return json.dumps(message).encode("utf-8") + b"\n"

    s = Server(obr, obe, env, work)
    s.call(1, "initialize", init)

    # ---- one line is one message ----
    s.send_raw(json.dumps({"jsonrpc": "2.0", "id": 2, "method": "ping"}).encode() + json.dumps({"jsonrpc": "2.0", "id": 3, "method": "ping"}).encode() + b"\n")
    r = s.recv()
    check("two messages on one line are a parse error", r and r.get("error", {}).get("code") == -32700 and r["id"] is None, r)
    check("and neither is answered", s.quiet(0.5))
    r = s.call(4, "ping")
    check("and the next line is read intact", r and r["result"] == {}, r)

    # ---- a request id names one call at a time ----
    s.send(slow(90, "tok"))
    got = [s.recv(15), s.recv(15)]
    check("a slow call is in flight", all(m and m.get("method") == "notifications/progress" for m in got), got)
    s.send(call_add(90, 1, 2))
    r = s.recv(15)
    check("a second call under an id still at work is an invalid request", r and r.get("id") == 90 and r.get("error", {}).get("code") == -32600, r)
    s.send(cancel(90))
    check("the first call is still there to cancel, and is not answered", s.quiet(1.5))
    r = s.call(90, "tools/call", {"name": "add", "arguments": {"a": 40, "b": 2}})
    check("the id is free once the call has ended", r and r.get("result", {}).get("content", [{}])[0].get("text") == "42", r)

    # ---- more calls at once than the server runs at once ----
    s.send_raw(b"".join(line(call_add(1000 + i, i, 1)) for i in range(100)))
    answers = {}
    end = time.time() + 60
    while len(answers) < 100 and time.time() < end:
        m = s.recv(5)
        if m is None:
            break
        if isinstance(m.get("id"), int) and 1000 <= m["id"] < 1100:
            answers[m["id"]] = m.get("result", {}).get("content", [{}])[0].get("text")
    check("a hundred calls written at once are each answered", answers == {1000 + i: str(i + 1) for i in range(100)}, sorted(answers.items())[:5])

    s.send_raw(b"".join(line(slow(2000 + i)) for i in range(24)))
    r = s.call(5, "ping")
    check("the server answers while more calls wait than it runs at once", r and r["result"] == {}, r)
    s.send_raw(b"".join(line(cancel(2000 + i)) for i in range(24)))
    check("calls that were cancelled, running or still waiting, are not answered", s.quiet(1.5))
    r = s.call(6, "ping")
    check("and the server serves on", r and r["result"] == {}, r)

    # ---- the threads that ran the calls are joined ----
    before = resources(s.proc.pid)
    calls = 1500
    ok = True
    for i in range(calls):
        r = s.call(3000 + i, "tools/call", {"name": "add", "arguments": {"a": i, "b": 1}})
        ok = ok and bool(r) and r.get("result", {}).get("content", [{}])[0].get("text") == str(i + 1)
    after = resources(s.proc.pid)
    check("%d calls one after another are each answered" % calls, ok)
    if before is None or after is None:
        print("  [SKIP] what the process holds cannot be read on this system")
    else:
        check("and the process holds no more %s for them (%d before, %d after)" % (before[1], before[0], after[0]),
              after[0] - before[0] < calls // (10 if os.name == "nt" else 2), (before, after))
    check("the server ends when standard input does", s.close() == 0)

    # ---- a cancellation on the very next line ----
    # 'slow' works until it is cancelled, or for 15 seconds. A cancellation that is lost leaves it working: the
    # server then waits for it at the end of input and answers it. Heard, every call ends at once, unanswered.
    c = Server(obr, obe, env, work)
    c.call(1, "initialize", init)
    pairs = 100
    c.send_raw(b"".join(line(slow(100 + i)) + line(cancel(100 + i)) for i in range(pairs)))
    r = c.call(2, "ping")
    check("the server read the calls and their cancellations", r and r["result"] == {}, r)
    start = time.time()
    status = c.close()
    elapsed = time.time() - start
    answered = []
    while True:
        m = c.recv(1)
        if m is None:
            break
        if isinstance(m.get("id"), int) and m["id"] >= 100:
            answered.append(m["id"])
    check("a cancellation written on the line after its request is heard: none of %d calls is answered" % pairs, not answered, answered[:10])
    check("and none is left working when the input ends (%.1f s)" % elapsed, status == 0 and elapsed < 10, (status, elapsed))


def review_http(obr, obe, env, work, init, check):
    """Streamable HTTP: request ids are a session's own, and a session begins with an initialize that was answered."""
    proc = subprocess.Popen([obr, obe, "--http"], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env, cwd=work)
    errlines = []

    def read_err():
        for raw in proc.stderr:
            errlines.append(raw.decode("utf-8", "replace"))
    threading.Thread(target=read_err, daemon=True).start()
    port = None
    end = time.time() + 60
    while time.time() < end and port is None:
        for l in list(errlines):
            if "mcp: serving http://127.0.0.1:" in l:
                port = int(l.strip().rsplit(":", 1)[1].rstrip("/"))
        time.sleep(0.05)
    check("the fixture serves HTTP and announces its port", port is not None, "".join(errlines))
    if port is None:
        proc.kill()
        return

    def post(message, session=None, timeout=40):
        headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if session:
            headers["Mcp-Session-Id"] = session
        req = urllib.request.Request("http://127.0.0.1:%d/" % port, data=json.dumps(message).encode(), headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return r.status, r.headers.get("Mcp-Session-Id"), r.read().decode()
        except urllib.error.HTTPError as e:
            return e.code, e.headers.get("Mcp-Session-Id"), e.read().decode()

    try:
        initialize = {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": init}
        st, a, body = post(initialize)
        st2, b, body2 = post(initialize)
        check("two clients each get a session", st == 200 and st2 == 200 and a and b and a != b, (st, a, st2, b))

        st, sid, body = post({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        check("an initialize without a revision is answered with invalid params", "-32602" in body, (st, body))
        check("and opens no session", sid is None, sid)

        slow = {"jsonrpc": "2.0", "id": 7, "method": "tools/call", "params": {"name": "slow", "arguments": {}}}
        cancel = {"jsonrpc": "2.0", "method": "notifications/cancelled", "params": {"requestId": 7}}
        done = {}

        def run(name, session):
            done[name] = post(slow, session)
        ta = threading.Thread(target=run, args=("a", a), daemon=True)
        ta.start()
        time.sleep(0.5)
        tb = threading.Thread(target=run, args=("b", b), daemon=True)
        tb.start()
        time.sleep(0.5)

        st, _, body = post({"jsonrpc": "2.0", "id": 7, "method": "tools/call", "params": {"name": "add", "arguments": {"a": 1, "b": 2}}}, a)
        check("the id of a call still at work is refused within its session", st == 400 and "-32600" in body, (st, body))

        # both sessions have a request 7 in flight: the first client cancels its own
        post(cancel, a)
        ta.join(8)
        check("a client's cancellation stops its own call, which is not answered", not ta.is_alive() and done.get("a", (0,))[0] == 202, done.get("a"))
        time.sleep(0.5)
        check("and leaves the other client's call with the same id at work", tb.is_alive() and "b" not in done, done.get("b"))
        post(cancel, b)
        tb.join(8)
        check("which its own client then cancels", not tb.is_alive() and done.get("b", (0,))[0] == 202, done.get("b"))
        st, _, body = post({"jsonrpc": "2.0", "id": 8, "method": "ping"}, a)
        check("and the server serves on", st == 200 and '"result"' in body, (st, body))
    except Exception as e:
        check("the HTTP checks ran", False, repr(e))
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.kill()


def utf16_checks(w, init, check):
    """A POSIX VM handed UTF-16 (what a Windows console VM delivers by default) decodes it."""
    def wide(message):
        return (json.dumps(message) + "\n").encode("utf-16-le")
    w.send_raw(wide({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": init}))
    r = w.recv_id(1)
    check("a server whose input arrives as UTF-16 is answered", r and r["result"]["serverInfo"]["name"] == "stdio-fixture", r)
    text = "héllo 世界"
    w.send_raw(wide({"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {"name": "echo", "arguments": {"text": text}}}))
    r = w.recv_id(2)
    check("and non-ASCII text survives it", r and r["result"]["content"][0]["text"] == text, r)
    w.send_raw("{not json\n".encode("utf-16-le"))
    r = w.recv()
    check("and a parse error is still a parse error", r and r["error"]["code"] == -32700, r)
    check("and it ends when its input does", w.close() == 0)


def utf16_cut(w, init, check):
    """The input ends inside a character: the line is not what was sent, so it is not acted on."""
    def wide(message):
        return json.dumps(message).encode("utf-16-le")
    w.send_raw(wide({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": init}) + "\n".encode("utf-16-le"))
    r = w.recv_id(1)
    check("a second server whose input arrives as UTF-16 is answered", r and "result" in r, r)
    # a whole message, then one byte of the two that make its line ending
    w.send_raw(wide({"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {"name": "add", "arguments": {"a": 1, "b": 2}}}) + b"\x0a")
    status = w.close()
    replies = []
    while True:
        m = w.recv(2)
        if m is None:
            break
        replies.append(m)
    check("a UTF-16 line cut inside a character is a parse error", len(replies) == 1 and replies[0].get("error", {}).get("code") == -32700, replies)
    check("and the call on it is not run", not any(m.get("id") == 2 for m in replies), replies)
    check("and the server still ends cleanly", status == 0, status)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    here = os.path.dirname(os.path.abspath(__file__))
    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = os.path.join(os.path.dirname(bin_dir), "lib")

    failures = []

    def check(name, cond, detail=""):
        print("  [%s] %s%s" % ("PASS" if cond else "FAIL", name, "" if cond else "\n         " + str(detail)[-500:]))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="mcp-stdio-") as work:
        obe = os.path.join(work, "mcp_stdio_fixture.obe")
        code, out = run([obc, "-src", os.path.join(here, "mcp_stdio_fixture.obs"), "-lib",
                         "cipher,collect,xml,json," + LIBS, "-opt", "s3", "-dest", obe], env, work)
        check("the fixture compiles", code == 0 and os.path.isfile(obe), out)
        if code != 0:
            return 1

        s = Server(obr, obe, env, work)
        init = {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "pytest", "version": "1"}}

        # ---- the handshake ----
        r = s.call(1, "initialize", init)
        check("initialize is answered with our revision", r and r["result"]["protocolVersion"] == "2025-11-25", r)
        check("initialize names the server and its capabilities",
              r and r["result"]["serverInfo"]["name"] == "stdio-fixture"
              and {"tools", "resources", "prompts"} <= set(r["result"]["capabilities"]), r)
        s.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
        check("the initialized notification gets no reply", s.quiet(0.5))
        r = s.call(2, "initialize", dict(init, protocolVersion="2024-11-05"))
        check("an older revision is negotiated as asked", r and r["result"]["protocolVersion"] == "2024-11-05", r)
        r = s.call(3, "initialize", dict(init, protocolVersion="2099-01-01"))
        check("an unknown revision is answered with ours", r and r["result"]["protocolVersion"] == "2025-11-25", r)
        r = s.call(4, "initialize", {})
        check("initialize without a revision is invalid params", r and r["error"]["code"] == -32602, r)

        # ---- every method ----
        r = s.call(10, "ping")
        check("ping", r and r["result"] == {}, r)
        r = s.call(11, "tools/list")
        names = [t["name"] for t in r["result"]["tools"]] if r else []
        check("tools/list", names == ["add", "fails", "raises", "echo", "repeat", "slow"], r)
        r = s.call(12, "tools/call", {"name": "add", "arguments": {"a": 40, "b": 2}})
        check("tools/call", r and r["result"]["content"][0]["text"] == "42" and r["result"]["isError"] is False, r)
        r = s.call(13, "resources/list")
        check("resources/list", r and [x["uri"] for x in r["result"]["resources"]] == ["notes://readme"], r)
        r = s.call(14, "resources/templates/list")
        check("resources/templates/list", r and r["result"]["resourceTemplates"][0]["uriTemplate"] == "notes://{name}", r)
        r = s.call(15, "resources/read", {"uri": "notes://readme"})
        check("resources/read", r and r["result"]["contents"][0]["text"] == "hello readme", r)
        r = s.call(16, "resources/read", {"uri": "notes://abc"})
        check("resources/read through a template", r and r["result"]["contents"][0]["text"] == "note for notes://abc", r)
        r = s.call(17, "resources/read", {"uri": "notes://missing"})
        check("a resource that is not there is -32002", r and r["error"]["code"] == -32002, r)
        r = s.call(18, "prompts/list")
        check("prompts/list", r and r["result"]["prompts"][0]["name"] == "greet", r)
        r = s.call(19, "prompts/get", {"name": "greet", "arguments": {"name": "Ada"}})
        check("prompts/get", r and r["result"]["messages"][0]["content"]["text"] == "Say hello to Ada", r)

        # ---- the errors of JSON-RPC ----
        s.send_raw(b"{this is not json\n")
        r = s.recv()
        check("malformed JSON is a parse error with a null id", r and r["error"]["code"] == -32700 and r["id"] is None, r)
        s.send_raw(b'{"jsonrpc":"2.0","id":30,"method":"ping"\n')
        r = s.recv()
        check("truncated JSON is a parse error", r and r["error"]["code"] == -32700, r)
        s.send_raw(b"42\n")
        r = s.recv()
        check("a number is an invalid request", r and r["error"]["code"] == -32600, r)
        s.send_raw(b'{"jsonrpc":"1.0","id":31,"method":"ping"}\n')
        r = s.recv()
        check("a wrong jsonrpc version is an invalid request", r and r["error"]["code"] == -32600 and r["id"] == 31, r)
        r = s.call(32, "no/such/method")
        check("an unknown method is -32601", r and r["error"]["code"] == -32601, r)
        r = s.call(33, "tools/call", {"name": "nope"})
        check("an unknown tool is -32602", r and r["error"]["code"] == -32602, r)
        r = s.call(34, "tools/call", {})
        check("a call without a name is -32602", r and r["error"]["code"] == -32602, r)
        r = s.call(35, "tools/call", {"name": "add", "arguments": "x"})
        check("arguments that are not an object are -32602", r and r["error"]["code"] == -32602, r)
        r = s.call(36, "tools/call", {"name": "add", "arguments": {"a": 1}})
        check("a missing argument is an isError result", r and r["result"]["isError"] is True, r)
        r = s.call(37, "prompts/get", {"name": "greet"})
        check("a missing prompt argument is -32602", r and r["error"]["code"] == -32602, r)
        s.send({"jsonrpc": "2.0", "id": "abc", "method": "ping"})
        r = s.recv()
        check("a string id is echoed as a string", r and r["id"] == "abc", r)

        # ---- a tool that fails, and one that raises ----
        r = s.call(40, "tools/call", {"name": "fails"})
        check("a tool that fails is an isError result, not a protocol error",
              r and "result" in r and r["result"]["isError"] is True and r["result"]["content"][0]["text"] == "the disk is full", r)
        r = s.call(41, "tools/call", {"name": "raises"})
        check("a tool that raises is an isError result", r and "result" in r and r["result"]["isError"] is True, r)
        r = s.call(42, "ping")
        check("the server serves on after a tool raised", r and r["result"] == {}, r)

        # ---- blank lines, CRLF, non-ASCII ----
        s.send_raw(b'\n\n   \n{"jsonrpc":"2.0","id":50,"method":"ping"}\r\n')
        r = s.recv()
        check("blank lines are skipped and a CRLF line ending is accepted", r and r["id"] == 50, r)
        text = "héllo 世界 \U0001F600"
        r = s.call(51, "tools/call", {"name": "echo", "arguments": {"text": text}})
        check("non-ASCII text round-trips through a tool", r and r["result"]["content"][0]["text"] == text, r)

        # ---- cancellation and progress ----
        s.send({"jsonrpc": "2.0", "id": 60, "method": "tools/call",
                "params": {"name": "slow", "arguments": {}, "_meta": {"progressToken": "tok"}}})
        got = []
        for _ in range(2):
            m = s.recv(15)
            got.append(m)
        check("a call reports its progress as notifications/progress with the token",
              all(m and m.get("method") == "notifications/progress" and m["params"]["progressToken"] == "tok" for m in got)
              and [m["params"]["progress"] for m in got] == [1, 2] and got[0]["params"]["total"] == 3, got)
        s.send({"jsonrpc": "2.0", "id": 61, "method": "ping"})
        r = s.recv_id(61)
        check("the server answers while a call runs", r and r["result"] == {}, r)
        s.send({"jsonrpc": "2.0", "method": "notifications/cancelled", "params": {"requestId": 60, "reason": "test"}})
        check("a cancelled request is not answered", s.quiet(1.5), None)
        r = s.call(62, "ping")
        check("the server serves on after a cancellation", r and r["result"] == {}, r)

        # ---- large messages ----
        big = "0123456789abcdef" * 65536  # 1 MiB
        r = s.call(70, "tools/call", {"name": "echo", "arguments": {"text": big}}, timeout=120)
        check("a request of a megabyte is read whole",
              r and r["result"]["content"][0]["text"] == big and r["result"]["content"][1]["text"] == str(len(big)), str(r)[:200])
        r = s.call(71, "tools/call", {"name": "repeat", "arguments": {"count": 1048576}}, timeout=120)
        check("a reply of a megabyte is written whole", r and len(r["result"]["content"][0]["text"]) == 1048576, str(r)[:200])
        over = b'{"jsonrpc":"2.0","id":72,"method":"tools/call","params":{"name":"echo","arguments":{"text":"' + b"a" * (5 * 1024 * 1024) + b'"}}}\n'
        s.send_raw(over)
        r = s.recv(120)
        check("a line over the limit is a parse error", r and r["error"]["code"] == -32700, str(r)[:200])
        r = s.call(73, "ping")
        check("and the next message is read intact", r and r["id"] == 73 and r["result"] == {}, r)

        # ---- the channel carries nothing but JSON-RPC ----
        try:
            for line in list(s.raw):
                json.loads(line.decode("utf-8"))
            clean = True
        except ValueError as e:
            clean = False
        check("every line on standard output is JSON", clean)

        # ---- the end of input ----
        s.send({"jsonrpc": "2.0", "id": 80, "method": "ping"})
        status = s.close()
        r = None
        while True:
            m = s.recv(2)
            if m is None:
                break
            if m.get("id") == 80:
                r = m
        check("a request written just before the end of input is answered", r and r["result"] == {}, r)
        check("the server ends when standard input does", status == 0, status)

        # ---- standard input as UTF-16, which is what a Windows console VM delivers by default ----
        # (a Windows VM does that to every test above already, and decodes the bytes it is given as
        # UTF-8, so only a POSIX VM can be handed UTF-16 bytes directly)
        w = Server(obr, obe, env, work) if os.name != "nt" else None
        if w is None:
            print("  [SKIP] UTF-16 standard input: the Windows VM decodes it as UTF-8 and every test above ran in that mode")
        else:
            utf16_checks(w, init, check)
            utf16_cut(Server(obr, obe, env, work), init, check)

        # ---- what an independent review found ----
        review_stdio(obr, obe, env, work, init, check)
        review_http(obr, obe, env, work, init, check)

        # ---- the Objeck client against the same server, run once per request ----
        command = '"%s" "%s" --stdio' % (obr, obe)
        code, out = run([obr, obe, "--client", command], env, work, timeout=300)
        check("the Objeck client library works against a stdio server", code == 0 and "PASS: client" in out, out)

        # ---- the fixture run by the regression suite ends on its own ----
        code, out = run([obr, obe], env, work, timeout=60)
        check("the fixture without arguments ends without reading input", code == 0 and "PASS" in out, out)

    print("\n%d failure(s)" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
