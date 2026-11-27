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
  metadata    what is started is what the method records say, not what the methods are called: a class the
              program wrote with the two names the compiler gives a served class (McpTools(McpService) and
              McpMain_<class>) is not a server, in the program or linked from a library; a tagged class
              linked from a library is one; an instance method named McpMain is refused with the reason
              instead of started with no instance; the compiler's entry with its mark removed is not found,
              and a function given the mark is; a mark the compiler cannot have written, a flag that is
              neither 0 nor 1, and an image that ends inside the flags are refused when the file is
              loaded, with or without --mcp
"""
import json, os, shutil, struct, subprocess, sys, tempfile, threading, time, urllib.request, zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "fuzz", "loader"))
import obe_image  # noqa: E402

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


# The class a program writes when it registers tools by hand: the two names the compiler gives a class with
# tagged functions, McpTools(McpService) and McpMain_<class>, and nothing tagged. The VM took it for a
# second server while it went by those names ("has 2 servers: Tools Utilities").
LOOKALIKE = r"""
use API.MCP;

class Tools {
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

class Utilities {
	function : McpTools(service : McpService) ~ Nil {
		"a look-alike registered"->ErrorLine();
	}

	function : McpMain_Utilities(args : String[]) ~ Nil {
		"a look-alike was started"->ErrorLine();
	}
}
"""

# The same class in a library the program links (the compiler names a bundled class's entry for the
# bundle too: Helpers.Utilities gives McpMain_Helpers_Utilities).
LOOKALIKE_LIB = r"""
use API.MCP;

bundle Helpers {
	class Utilities {
		function : McpTools(service : McpService) ~ Nil {
			"a look-alike registered"->ErrorLine();
		}

		function : McpMain_Helpers_Utilities(args : String[]) ~ Nil {
			"a look-alike was started"->ErrorLine();
		}
	}
}
"""

LOOKALIKE_LIB_USER = r"""
use API.MCP, Helpers;

class Tools {
	#~
	Add two integers.
	@tool
	~#
	function : Add(a : Int, b : Int) ~ Int {
		return a + b;
	}

	function : Main(args : String[]) ~ Nil {
		Utilities->McpTools(Nil);
	}
}
"""

# A class with a tagged function in a library: the entry the compiler wrote for it is marked in the
# library's method record, and the mark has to reach the program that links the class.
TAGGED_LIB = r"""
use API.MCP;

bundle Shelves {
	class Shelf {
		#~
		How many books the shelf holds.
		@tool
		~#
		function : Count() ~ Int {
			return 3;
		}
	}
}
"""

TAGGED_LIB_USER = r"""
use Shelves;

class Reader {
	function : Main(args : String[]) ~ Nil {
		Shelf->Count()->PrintLine();
	}
}
"""

# An ordinary program (no 'use API.MCP', nothing tagged, so obc has no reason to refuse it) whose McpMain
# is an instance method. 'obr --mcp' started it with no instance: its first use of a field was the VM's
# Nil-dereference error.
INSTANCE = r"""
class Greeter {
	@greeting : String;

	New() {
		@greeting := "hello";
	}

	method : public : McpMain(args : String[]) ~ Nil {
		"the instance method was started"->ErrorLine();
		@greeting->PrintLine();
	}

	function : Main(args : String[]) ~ Nil {
		Greeter->New()->McpMain(args);
	}
}
"""

ENTRY_SIGNATURE = ":McpMain:o.System.String*,"


def read_image(obe):
    """The decompressed image of a program obc wrote ([uncompressed size:4][zlib])."""
    with open(obe, "rb") as f:
        return bytearray(zlib.decompress(f.read()[4:]))


def write_image(dest, image):
    with open(dest, "wb") as f:
        f.write(struct.pack("<I", len(image)) + zlib.compress(bytes(image), 9))
    return dest


def method_records(image, ending):
    """(where it starts, its name) for each method record whose name ends with 'ending'."""
    _, classes, _ = obe_image.parse(bytes(image))
    return [(start, name) for cls in classes for name, start, _ in cls["methods"] if name.endswith(ending)]


def with_flags(obe, dest, changes, cut_at=None):
    """Writes 'dest': the program 'obe' with flag bytes of its method records changed. 'changes' is a list
    of (how a method's name ends, flag, value); each ending must name exactly one method. With 'cut_at', a
    (name ending, flag), the image also ends just before that byte. Programs obc cannot be made to write.
    None when a method is not found."""
    image = read_image(obe)
    for ending, flag, value in changes:
        found = method_records(image, ending)
        if len(found) != 1:
            return None
        image[obe_image.method_flag_at(found[0][0], flag)] = value
    if cut_at:
        found = method_records(image, cut_at[0])
        if len(found) != 1:
            return None
        image = image[:obe_image.method_flag_at(found[0][0], cut_at[1])]
    return write_image(dest, image)


def changed_entry(obe, dest, change):
    """Writes 'dest': the program 'obe' with its McpMain record changed, "virtual" (the flag that says a
    method has no body) or "returns" (its return type made Int). A program obc cannot be made to write."""
    if change == "virtual":
        return with_flags(obe, dest, [(ENTRY_SIGNATURE, "virtual", 1)])
    image = read_image(obe)
    for start, name in method_records(image, ENTRY_SIGNATURE):
        # [id:4][the flag bytes][name][return]: the return type's one letter
        at = obe_image.method_name_at(start) + 4 + len(name.encode("utf-8")) + 4
        assert image[at:at + 1] == b"n", image[at - 4:at + 1]
        image[at:at + 1] = b"i"
        return write_image(dest, image)
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
        # -lib names a library in the library directory, so the two libraries this test builds go into a
        # private copy of it (as check_obe_version_mismatch.py does)
        own_libs = os.path.join(work, "libs")
        os.makedirs(own_libs)
        for lib_name in os.listdir(env["OBJECK_LIB_PATH"]):
            if lib_name.endswith(".obl"):
                shutil.copy(os.path.join(env["OBJECK_LIB_PATH"], lib_name), own_libs)
        own_lib_env = dict(env)
        own_lib_env["OBJECK_LIB_PATH"] = own_libs

        def build(name, text, as_lib=False, libs=LIBS):
            """Compiles a program into the work directory, or a library into the private library directory."""
            src = os.path.join(work, name + ".obs")
            open(src, "w", encoding="utf-8").write(text)
            dest = os.path.join(own_libs, name + ".obl") if as_lib else os.path.join(work, name + ".obe")
            cmd = [obc, "-src", src, "-lib", libs, "-dest", dest] + (["-tar", "lib"] if as_lib else [])
            p = subprocess.run(cmd, capture_output=True, env=own_lib_env, cwd=work, timeout=300)
            if p.returncode != 0 or not os.path.exists(dest):
                print((p.stdout + p.stderr).decode("utf-8", "replace"))
                return None
            return dest

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
        lookalike = build("lookalike", LOOKALIKE)
        lookalike_lib = build("mcp_test_helpers", LOOKALIKE_LIB, as_lib=True)
        lookalike_user = lookalike_lib and build("lookalike_user", LOOKALIKE_LIB_USER, libs=LIBS + ",mcp_test_helpers")
        tagged_lib = build("mcp_test_shelves", TAGGED_LIB, as_lib=True)
        tagged_user = tagged_lib and build("tagged_user", TAGGED_LIB_USER, libs=LIBS + ",mcp_test_shelves")
        instance = build("instance", INSTANCE)
        if not all((shop, nothing, two, own, child, launcher, entry, no_body, returns, helper, overload,
                    lookalike, lookalike_lib, lookalike_user, tagged_lib, tagged_user, instance)):
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
        check("a program with two servers says which and how to serve both",
              code != 0 and "2 servers (one for each class with a tagged function): A B" in err and "McpMain(args : String[])" in err, out + err)
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

        # ---- what is started is what the method records say ----
        listing = ("\n".join([INIT, INITIALIZED, msg(2, "tools/list")]) + "\n").encode()

        def served(result, server, tool):
            code, out, err = result
            return (code == 0 and '"%s"' % server in out and '"%s"' % tool in out and "servers" not in err
                    and "look-alike" not in err)

        result = run(["--mcp", lookalike], listing)
        check("a class the program wrote with the names McpTools and McpMain_<class> is not a second server",
              served(result, "Tools", "add"), "rc=%s out=%s err=%s" % result)
        result = run(["--mcp", lookalike_user], listing)
        check("nor is one linked from a library", served(result, "Tools", "add"), "rc=%s out=%s err=%s" % result)
        result = run(["--mcp", tagged_user], listing)
        check("a tagged class linked from a library is served: its entry's mark comes through the library",
              served(result, "Shelves.Shelf", "count"), "rc=%s out=%s err=%s" % result)

        for label, flags, extra in modes:
            code, out, err = run(flags + ["--mcp", instance], extra_env=extra)
            check("an instance method named McpMain is not started, and the refusal says why (%s)" % label,
                  code == 1 and "has nothing to serve" in err and "Greeter (it is a method, which needs an instance)" in err
                  and "was started" not in err and "dereference" not in err, "rc=%s out=%s err=%s" % (code, out, err))
        code, out, err = run([instance])
        check("and without the flag it is an ordinary method",
              code == 0 and "the instance method was started" in err and out.strip() == "hello", "rc=%s out=%s err=%s" % (code, out, err))

        def crafted(name, source, changes, cut_at=None):
            made = with_flags(source, os.path.join(work, name + ".obe"), changes, cut_at)
            check("the crafted program '%s' could be made" % name, made is not None)
            return made

        # the names stay and the marks change: the names decide nothing
        unmarked = crafted("unmarked", shop, [(":McpMain_Shop:o.System.String*,", "mcp_entry", 0)])
        if unmarked:
            code, out, err = run(["--mcp", unmarked])
            check("the compiler's entry without its mark is not found: the name McpMain_<class> means nothing",
                  code == 1 and "has nothing to serve" in err, "rc=%s out=%s err=%s" % (code, out, err))
        marked = crafted("marked", helper, [(":McpMain_Test:o.System.String*,", "mcp_entry", 1)])
        if marked:
            code, out, err = run(["--mcp", marked])
            check("a function given the mark is an entry: the program then has two servers",
                  code == 1 and "2 servers (one for each class with a tagged function): Shop Other" in err,
                  "rc=%s out=%s err=%s" % (code, out, err))
        not_function = crafted("not_function", entry, [(ENTRY_SIGNATURE, "function", 0)])
        if not_function:
            code, out, err = run(["--mcp", not_function])
            check("a McpMain whose record says it is not a function is not started",
                  code == 1 and "Entry (it is a method, which needs an instance)" in err and "was started" not in err,
                  "rc=%s out=%s err=%s" % (code, out, err))
            code, out, err = run([not_function])
            check("and nothing else reads that flag: the program runs its Main as before",
                  code == 0 and out.strip() == "main", "rc=%s out=%s err=%s" % (code, out, err))

        # marks and flags the compiler cannot have written: the file is refused when it is loaded
        refused = (
            ("an entry mark on a function that returns a value", shop, [(":Add:i,i,", "mcp_entry", 1)], None,
             "marked as its class's MCP entry"),
            ("an entry mark on a method that is not a function", entry,
             [(ENTRY_SIGNATURE, "function", 0), (ENTRY_SIGNATURE, "mcp_entry", 1)], None, "marked as its class's MCP entry"),
            ("a second entry mark in one class", helper,
             [(":McpMain_Test:o.System.String*,", "mcp_entry", 1), (":McpMain_Other:o.System.String*,", "mcp_entry", 1)], None,
             "a second MCP entry in its class"),
            ("a function flag that is neither 0 nor 1", entry, [(ENTRY_SIGNATURE, "function", 2)], None, "method flag out of range"),
            ("an entry mark that is neither 0 nor 1", shop, [(":McpMain_Shop:o.System.String*,", "mcp_entry", 255)], None,
             "method flag out of range"),
            ("an image that ends before a method's function flag", entry, [], (ENTRY_SIGNATURE, "function"), "bytecode"),
            ("an image that ends before a method's entry mark", entry, [], (ENTRY_SIGNATURE, "mcp_entry"), "bytecode"),
        )
        for number, (about, source, changes, cut_at, message) in enumerate(refused):
            made = crafted("refused%d" % number, source, changes, cut_at)
            if not made:
                continue
            for flags in (["--mcp"], []):
                code, out, err = run(flags + [made])
                check("%s is refused when the file is loaded (%s)" % (about, "--mcp" if flags else "no flag"),
                      code == 1 and message in err and "was started" not in err and out.strip() == "",
                      "rc=%s out=%s err=%s" % (code, out, err))

        # An instance method whose record claims to be a function (a file obc did not write) is started as
        # the function it says it is. Its first use of the instance it does not have is the VM's error for
        # a Nil reference, in every mode; never a crash.
        liar = crafted("liar", instance, [(ENTRY_SIGNATURE, "function", 1)])
        if liar:
            for label, flags, extra in modes:
                code, out, err = run(flags + ["--mcp", liar], extra_env=extra)
                check("an instance method whose record says 'function' ends in the VM's Nil error, not a crash (%s)" % label,
                      code == 1 and "Nil" in err, "rc=%s out=%s err=%s" % (code, out, err))

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
