#!/usr/bin/env python3
"""
'obr --mcp' and 'obr --mcp-http' (docs/design/MCP_FIRST_CLASS.md, Stage 3): the VM starts a program at its
McpMain instead of its Main, and the program's @tool, @resource and @prompt functions are served.

Usage: mcp_launch_test.py <bin_dir>

<bin_dir> holds obc and obr; the libraries are read from the sibling lib/ directory. No network beyond the
loopback interface is used.

Checks:
  stdio       initialize, tools/list, tools/call, resources/read, prompts/get, a refused call; stdout holds
              nothing but JSON-RPC lines; the process exits 0 when standard input ends; the same in the
              default mode, --jit=off and OBJECK_JIT_THRESHOLD=1
  fuzz        malformed JSON-RPC, a binary line, an empty line, a very long line, a message cut off by the end of
              standard input: the server answers or ignores, never crashes, and still exits 0
  http        --mcp-http=0 announces its port on standard error; initialize, tools/list and tools/call over POST
  options     --mcp-http=abc, --mcp with --mcp-http, --mcp-http-host alone, a program with nothing to serve and a
              program with two servers are refused with a message that names the problem
  unchanged   without the flags a tagged program runs its own Main
  child       an obr the served program starts (Runtime->Command) runs its own Main: the request is this
              process's, not the environment's; OBJECK_MCP in the environment asks for nothing
  entry       a McpMain with no body, or one that returns a value, is not started and the refusal says
              why (the compiler refuses both, so the two programs are made from a compiled image); a
              function the program named McpMain_<something> is not a second server; a class whose McpMain
              has another parameter list still gets the compiler's entry
"""
import json, os, subprocess, sys, tempfile, threading, time, urllib.request

EXE = ".exe" if os.name == "nt" else ""
LIBS = "gen_collect,json,net,net_server,cipher,misc,mcp"

SERVER = r'''
use API.MCP, Data.JSON, Collection;

class Shop {
	#~
	Add two integers.
	@tool
	@readonly
	@param a the first number
	@param b the second number
	~#
	function : Add(a : Int, b : Int) ~ Int {
		return a + b;
	}

	#~
	Reports progress, honours cancellation.
	@tool
	@param steps how many steps
	~#
	function : Steps(steps : Int, call : McpCall) ~ Int {
		done := 0;
		for(i := 0; i < steps; i += 1;) {
			if(call->IsCancelled()) {
				return done;
			};
			call->ReportProgress((i + 1)->As(Float), steps->As(Float), "step");
			done += 1;
		};
		return done;
	}

	#~
	The shop's opening hours.
	@resource shop://{day}/hours
	~#
	function : Hours(day : String) ~ String {
		return "{$day}: 9 to 5";
	}

	#~
	Ask about an item.
	@prompt
	@param item the item
	~#
	function : Ask(item : String) ~ String {
		return "How much is {$item}?";
	}

	function : Main(args : String[]) ~ Nil {
		"the shop program ran its own Main"->PrintLine();
	}
}
'''

NOTHING = r'''
class Hello {
	function : Main(args : String[]) ~ Nil {
		"hello"->PrintLine();
	}
}
'''

TWO = r'''
use API.MCP;

class A {
	#~
	@tool
	~#
	function : One() ~ Int {
		return 1;
	}
}

class B {
	#~
	@tool
	~#
	function : Two() ~ Int {
		return 2;
	}

	function : Main(args : String[]) ~ Nil {
		"two"->PrintLine();
	}
}
'''

OWN = r'''
use API.MCP;

class A {
	#~
	@tool
	~#
	function : One() ~ Int {
		return 1;
	}
}

class B {
	#~
	@tool
	~#
	function : Two() ~ Int {
		return 2;
	}

	function : McpMain(args : String[]) ~ Nil {
		server := McpService->New("both", "1.0");
		A->McpTools(server);
		B->McpTools(server);
		McpServe->Run(server, args);
	}

	function : Main(args : String[]) ~ Nil {
		"two"->PrintLine();
	}
}
'''

# An ordinary program: what an obr started by a served program must still run.
CHILD = r"""
class Child {
	function : Main(args : String[]) ~ Nil {
		count := args->Size();
		"the child ran its own Main with {$count} argument(s)"->ErrorLine();
	}
}
"""

# A McpMain the program wrote that starts another obr with the environment it was given. OBR and CHILD_OBE
# are filled in below.
LAUNCHER = r"""
class Launcher {
	function : McpMain(args : String[]) ~ Nil {
		code := Runtime->Command("OBR CHILD_OBE");
		"the child exited {$code}"->ErrorLine();
	}

	function : Main(args : String[]) ~ Nil {
		"the launcher ran its own Main"->PrintLine();
	}
}
"""

# A McpMain the program wrote. The compiler refuses one that is not 'function ... ~ Nil'
# (mcp_tool_tags_diag_test.py), so the two files the VM must still refuse, one whose entry has no body and
# one whose entry returns a value, are made from this program's compiled image (changed_entry below).
ENTRY = r"""
class Entry {
	function : McpMain(args : String[]) ~ Nil {
		"the entry was started"->ErrorLine();
	}

	function : Main(args : String[]) ~ Nil {
		"main"->PrintLine();
	}
}
"""

# A class with a tagged function and a method named McpMain that is not the entry (another parameter list):
# the compiler still writes the class its entry.
OVERLOAD = r"""
use API.MCP;

class Shop {
	#~
	Add two integers.
	@tool
	~#
	function : Add(a : Int, b : Int) ~ Int {
		return a + b;
	}

	function : McpMain(count : Int) ~ Int {
		return count + 1;
	}

	function : Main(args : String[]) ~ Nil {
		McpMain(1)->PrintLine();
	}
}
"""

# One server, and two functions of the program's own whose names begin as a generated entry's does: one in
# another class, one named for its own class in a class the compiler wrote nothing for.
HELPER = r"""
use API.MCP;

class Shop {
	#~
	Add two integers.
	@tool
	~#
	function : Add(a : Int, b : Int) ~ Int {
		return a + b;
	}

	function : Main(args : String[]) ~ Nil {
		"main"->PrintLine();
	}
}

class Other {
	function : McpMain_Test(args : String[]) ~ Nil {
		"a helper was started"->ErrorLine();
	}

	function : McpMain_Other(args : String[]) ~ Nil {
		"a helper was started"->ErrorLine();
	}
}
"""


def changed_entry(obe, dest, change):
    """Writes 'dest': the program 'obe' with its McpMain record changed, "virtual" (the flag that says a
    method has no body) or "returns" (its return type made Int). A program obc cannot be made to write."""
    import struct, zlib
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "fuzz", "loader"))
    import obe_image
    with open(obe, "rb") as f:
        image = bytearray(zlib.decompress(f.read()[4:]))
    _, classes, _ = obe_image.parse(bytes(image))
    for cls in classes:
        for name, start, _ in cls["methods"]:
            if name.endswith(":McpMain:o.System.String*,"):
                if change == "virtual":
                    image[start + 4] = 1
                else:
                    # [id:4][virtual, and/or, lambda][name][return]: the return type's one letter
                    at = start + 7 + 4 + len(name.encode("utf-8")) + 4
                    assert image[at:at + 1] == b"n", image[at - 4:at + 1]
                    image[at:at + 1] = b"i"
                with open(dest, "wb") as f:
                    f.write(struct.pack("<I", len(image)) + zlib.compress(bytes(image), 9))
                return dest
    return None


def msg(id_, method, params=None):
    m = {"jsonrpc": "2.0", "method": method}
    if id_ is not None:
        m["id"] = id_
    if params is not None:
        m["params"] = params
    return json.dumps(m)


INIT = msg(1, "initialize", {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "t", "version": "1"}})
INITIALIZED = msg(None, "notifications/initialized")


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = os.path.join(os.path.dirname(bin_dir), "lib")
    failures = []

    def check(name, cond, detail=""):
        print(("  [%s] %s%s" % ("PASS" if cond else "FAIL", name, "" if cond else "\n         " + str(detail)[-900:])).encode("ascii", "replace").decode("ascii"))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="mcp-launch-") as work:
        def build(name, text):
            src = os.path.join(work, name + ".obs")
            open(src, "w", encoding="utf-8").write(text)
            obe = os.path.join(work, name + ".obe")
            p = subprocess.run([obc, "-src", src, "-lib", LIBS, "-dest", obe], capture_output=True, env=env, cwd=work, timeout=300)
            if p.returncode != 0 or not os.path.exists(obe):
                print((p.stdout + p.stderr).decode("utf-8", "replace"))
                return None
            return obe

        def run(args, stdin=b"", timeout=120, extra_env=None):
            e = dict(env)
            if extra_env:
                e.update(extra_env)
            p = subprocess.run([obr] + args, input=stdin, capture_output=True, env=e, cwd=work, timeout=timeout)
            return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")

        shop = build("shop", SERVER)
        nothing = build("nothing", NOTHING)
        two = build("two", TWO)
        own = build("own", OWN)
        child = build("child", CHILD)
        # forward slashes: the path is inside an Objeck string literal, and both shells take them
        launcher = build("launcher", LAUNCHER.replace("OBR", obr.replace("\\", "/")).replace("CHILD_OBE", "child.obe"))
        entry = build("entry", ENTRY)
        no_body = entry and changed_entry(entry, os.path.join(work, "no_body.obe"), "virtual")
        returns = entry and changed_entry(entry, os.path.join(work, "returns.obe"), "returns")
        helper = build("helper", HELPER)
        overload = build("overload", OVERLOAD)
        if not all((shop, nothing, two, own, child, launcher, entry, no_body, returns, helper, overload)):
            check("the test programs compile", False)
            return 1

        # ---- stdio, in the three execution modes ----
        session = [INIT, INITIALIZED,
                   msg(2, "tools/list"),
                   msg(3, "tools/call", {"name": "add", "arguments": {"a": 17, "b": 25}}),
                   msg(4, "tools/call", {"name": "add", "arguments": {"a": 1, "b": "two"}}),
                   msg(5, "resources/read", {"uri": "shop://monday/hours"}),
                   msg(6, "prompts/get", {"name": "ask", "arguments": {"item": "tea"}}),
                   msg(7, "tools/call", {"name": "steps", "arguments": {"steps": 5}, "_meta": {"progressToken": "p"}}),
                   msg(8, "resources/templates/list"),
                   msg(9, "prompts/list")]
        modes = (("default", [], None), ("--jit=off", ["--jit=off"], None), ("OBJECK_JIT_THRESHOLD=1", [], {"OBJECK_JIT_THRESHOLD": "1"}))
        for label, flags, extra in modes:
            code, out, err = run(flags + ["--mcp", shop], ("\n".join(session) + "\n").encode(), extra_env=extra)
            lines = [l for l in out.splitlines() if l.strip()]
            parsed, only_json = {}, True
            for l in lines:
                try:
                    j = json.loads(l)
                    if "id" in j:
                        parsed[j["id"]] = j
                except ValueError:
                    only_json = False
            check("stdio (%s): exits 0 when standard input ends" % label, code == 0, "rc=%s err=%s" % (code, err))
            check("stdio (%s): standard output holds JSON-RPC lines only" % label, only_json and len(lines) >= 9, out[-500:])
            check("stdio (%s): initialize names the server and offers the three kinds" % label,
                  1 in parsed and parsed[1]["result"]["serverInfo"]["name"] == "Shop" and all(k in parsed[1]["result"]["capabilities"] for k in ("tools", "resources", "prompts")), parsed.get(1))
            names = sorted(t["name"] for t in parsed.get(2, {}).get("result", {}).get("tools", []))
            check("stdio (%s): tools/list" % label, names == ["add", "steps"], names)
            check("stdio (%s): tools/call add" % label, parsed.get(3, {}).get("result", {}).get("content", [{}])[0].get("text") == "42", parsed.get(3))
            check("stdio (%s): a wrong argument type is refused" % label, parsed.get(4, {}).get("result", {}).get("isError") is True, parsed.get(4))
            check("stdio (%s): resources/read fills the URI variable" % label, parsed.get(5, {}).get("result", {}).get("contents", [{}])[0].get("text") == "monday: 9 to 5", parsed.get(5))
            check("stdio (%s): prompts/get" % label, "How much is tea?" in json.dumps(parsed.get(6)), parsed.get(6))
            progress = [json.loads(l) for l in lines if '"notifications/progress"' in l]
            check("stdio (%s): progress notifications reach the client" % label, len(progress) == 5 and parsed.get(7, {}).get("result", {}).get("content", [{}])[0].get("text") == "5", (len(progress), parsed.get(7)))
            check("stdio (%s): templates and prompts are listed" % label, "shop://{day}/hours" in json.dumps(parsed.get(8)) and '"ask"' in json.dumps(parsed.get(9)), (parsed.get(8), parsed.get(9)))

        # ---- fuzz: whatever arrives on standard input ----
        hostile = [INIT, "", "   ", "{", "[]", "null", "42", '{"jsonrpc":"2.0"}', '{"jsonrpc":"2.0","id":[1],"method":"ping"}',
                   '{"jsonrpc":"2.0","id":"x","method":"tools/call","params":{"name":"add","arguments":[1,2]}}',
                   '{"jsonrpc":"2.0","id":"y","method":"tools/call","params":{"name":"nope"}}',
                   '{"jsonrpc":"2.0","id":"z","method":"resources/read","params":{"uri":"shop:///hours"}}',
                   '{"jsonrpc":"2.0","id":"w","method":"prompts/get","params":{"name":"ask"}}',
                   "\x00\x01\x02\xff\xfe" * 20, "x" * 5000000,
                   msg(99, "tools/call", {"name": "add", "arguments": {"a": 1, "b": 2}}),
                   '{"jsonrpc":"2.0","id":100,"method":"tools/call","params":{"name":"add","argu']   # cut off: no newline at the end
        data = ("\n".join(hostile)).encode("utf-8", "replace")
        code, out, err = run(["--mcp", shop], data, timeout=180)
        replies = []
        for l in out.splitlines():
            try:
                replies.append(json.loads(l))
            except ValueError:
                pass
        check("fuzz: the server survives hostile input and exits 0", code == 0, "rc=%s err=%s" % (code, err[-300:]))
        check("fuzz: a good request after the bad ones is still answered", any(r.get("id") == 99 and r.get("result", {}).get("content", [{}])[0].get("text") == "3" for r in replies), out[-600:])
        check("fuzz: malformed lines get JSON-RPC errors, not silence or a crash", any(r.get("error", {}).get("code") == -32700 for r in replies), out[-600:])

        # ---- http ----
        proc = subprocess.Popen([obr, "--mcp-http=0", shop], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, cwd=work)
        port = None
        errlines = []

        def read_err():
            for raw in proc.stderr:
                errlines.append(raw.decode("utf-8", "replace"))
        t = threading.Thread(target=read_err, daemon=True)
        t.start()
        deadline = time.time() + 60
        while time.time() < deadline and port is None:
            for l in list(errlines):
                if "mcp: serving http://127.0.0.1:" in l:
                    port = int(l.strip().rsplit(":", 1)[1].rstrip("/"))
            time.sleep(0.05)
        check("http: the port is announced on standard error", port is not None, "".join(errlines))
        if port:
            session = {}

            def post(body):
                headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream", "MCP-Protocol-Version": "2025-11-25"}
                headers.update(session)
                req = urllib.request.Request("http://127.0.0.1:%d/" % port, data=body.encode(), headers=headers)
                with urllib.request.urlopen(req, timeout=30) as r:
                    sid = r.headers.get("Mcp-Session-Id")
                    if sid:
                        session["Mcp-Session-Id"] = sid
                    return r.status, r.read().decode()
            try:
                st, body = post(INIT)
                check("http: initialize", st == 200 and "Shop" in body, body)
                post(INITIALIZED)
                st, body = post(msg(3, "tools/call", {"name": "add", "arguments": {"a": 20, "b": 22}}))
                check("http: tools/call", '"42"' in body.replace(" ", ""), body)
                st, body = post(msg(4, "resources/read", {"uri": "shop://friday/hours"}))
                check("http: resources/read", "friday: 9 to 5" in body, body)
            except Exception as e:
                check("http: requests", False, repr(e))
        proc.terminate()
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.kill()
            check("http: the server stops when terminated", False)
        else:
            check("http: the server stops when terminated", True)

        # ---- options and refusals ----
        code, out, err = run(["--mcp-http=abc", shop])
        check("--mcp-http=abc is refused with the expected values", code != 0 and "expected a port" in err, err)
        code, out, err = run(["--mcp", "--mcp-http=8080", shop])
        check("--mcp with --mcp-http is refused", code != 0 and "different transports" in err, err)
        code, out, err = run(["--mcp-http-host=0.0.0.0", shop])
        check("--mcp-http-host without --mcp-http is refused", code != 0 and "only with --mcp-http" in err, err)
        code, out, err = run(["--mcp", nothing])
        check("a program with nothing to serve says so", code != 0 and "has nothing to serve" in err and "@tool" in err, out + err)
        code, out, err = run(["--mcp", two])
        check("a program with two servers says which and how to serve both", code != 0 and "2 servers" in err and "McpMain(args : String[])" in err, out + err)
        code, out, err = run(["--mcp", own], ("\n".join([INIT, INITIALIZED, msg(2, "tools/list")]) + "\n").encode())
        check("a McpMain the program wrote serves both classes", code == 0 and '"one"' in out and '"two"' in out and "both" in out, out + err)

        # ---- the request belongs to the process that was given the flag ----
        # It used to be handed to the loader in the environment variable OBJECK_MCP, which the child
        # inherited: the child put "stdio" in front of its arguments and was refused for having nothing to
        # serve (or, with a McpMain of its own, served instead of running).
        code, out, err = run(["--mcp", launcher])
        check("an obr started by a served program runs its own Main, with its own arguments",
              code == 0 and "the child ran its own Main with 0 argument(s)" in err and "the child exited 0" in err
              and "nothing to serve" not in err, "rc=%s out=%s err=%s" % (code, out, err))
        code, out, err = run([child], extra_env={"OBJECK_MCP": "stdio"})
        check("OBJECK_MCP in the environment asks for nothing: only the flag does",
              code == 0 and "the child ran its own Main with 0 argument(s)" in err, "rc=%s out=%s err=%s" % (code, out, err))

        # ---- what is started is a function with a body that returns Nil ----
        code, out, err = run(["--mcp", entry])
        check("the unchanged entry is started", code == 0 and "the entry was started" in err, "rc=%s out=%s err=%s" % (code, out, err))
        for label, flags, extra in modes:
            code, out, err = run(flags + ["--mcp", no_body], extra_env=extra)
            check("a McpMain with no body is not started, and the refusal says why (%s)" % label,
                  code == 1 and "has nothing to serve" in err and "Entry (it has no body)" in err and "was started" not in err
                  and "dereference" not in err, "rc=%s out=%s err=%s" % (code, out, err))
            code, out, err = run(flags + ["--mcp", returns], extra_env=extra)
            check("a McpMain that returns a value is not started, and the refusal says why (%s)" % label,
                  code == 1 and "Entry (it returns a value)" in err and "was started" not in err,
                  "rc=%s out=%s err=%s" % (code, out, err))
        code, out, err = run(["--mcp", helper], ("\n".join([INIT, INITIALIZED, msg(2, "tools/list")]) + "\n").encode())
        check("a function the program named McpMain_<something> is not a second server",
              code == 0 and '"add"' in out and "servers" not in err and "a helper was started" not in err,
              "rc=%s out=%s err=%s" % (code, out, err))

        code, out, err = run(["--mcp", overload], ("\n".join([INIT, INITIALIZED, msg(2, "tools/list")]) + "\n").encode())
        check("a class whose McpMain is not the entry (another parameter list) still gets the compiler's entry",
              code == 0 and '"add"' in out and '"Shop"' in out, "rc=%s out=%s err=%s" % (code, out, err))
        code, out, err = run([overload])
        check("and that McpMain is an ordinary function", code == 0 and out.strip() == "2", "rc=%s out=%s err=%s" % (code, out, err))

        # ---- nothing changes without the flags ----
        code, out, err = run([shop])
        check("without the flags the program runs its own Main", code == 0 and "the shop program ran its own Main" in out, out + err)

    if failures:
        print("FAIL: %d check(s) failed" % len(failures))
        return 1
    print("PASS: obr --mcp and --mcp-http")
    return 0


if __name__ == "__main__":
    sys.exit(main())
