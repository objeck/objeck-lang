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
"""
import json, os, queue, subprocess, sys, tempfile, threading, time

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
