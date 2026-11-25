#!/usr/bin/env python3
"""
The typed client generator for MCP servers (tools/mcp/mcp_stubs.obs, which is API.MCP's McpStubGenerator
behind a command line), end to end:

  1. mcp_stubs/weather_server.obs, a typed server declared with McpToolSpec, is compiled;
  2. mcp_stubs is compiled and run against it over stdio and writes WeatherClient.obs;
  3. that source equals the checked-in golden file mcp_stubs/WeatherClient.golden, byte for byte, and a
     second run gives the same bytes (the output depends only on the tool list);
  4. the generated class compiles together with mcp_stubs/use_stub.obs, a program that makes typed calls
     (an optional parameter left out, arrays, a Bool, a JSON argument, a structured result, isError giving
     Nil and GetError) against the server, and its checks pass, in the default mode and with the JIT off;
  5. the command line's errors: no source, both sources, an unknown option, a missing value, a server
     that cannot be reached.

Usage: mcp_stubs_test.py <bin_dir>

<bin_dir> holds obc and obr; the libraries are read from the sibling lib/ directory. No network is used.
To refresh the golden file after a deliberate change to the generator, run this test with MCP_STUBS_UPDATE=1.
"""
import os, subprocess, sys, tempfile

EXE = ".exe" if os.name == "nt" else ""
LIBS = "gen_collect,json,net,net_server,cipher,misc,mcp"


def run(cmd, env, cwd, timeout=300):
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, timeout=timeout)
    return p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace")


def quote(path):
    return '"%s"' % path if " " in path else path


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.dirname(os.path.dirname(here))
    data = os.path.join(here, "mcp_stubs")
    golden = os.path.join(data, "WeatherClient.golden")
    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = os.path.join(os.path.dirname(bin_dir), "lib")

    failures = []

    def check(name, cond, detail=""):
        print("  [%s] %s%s" % ("PASS" if cond else "FAIL", name, "" if cond else "\n         " + str(detail)[-600:]))
        if not cond:
            failures.append(name)

    def read(path):
        with open(path, "rb") as f:
            return f.read().replace(b"\r\n", b"\n")

    with tempfile.TemporaryDirectory(prefix="mcp-stubs-") as work:
        server = os.path.join(work, "weather_server.obe")
        code, out = run([obc, "-src", os.path.join(data, "weather_server.obs"), "-lib", LIBS, "-dest", server], env, work)
        check("the typed server compiles", code == 0 and os.path.isfile(server), out)
        tool = os.path.join(work, "mcp_stubs.obe")
        code2, out2 = run([obc, "-src", os.path.join(repo, "tools", "mcp", "mcp_stubs.obs"), "-lib", LIBS, "-dest", tool], env, work)
        check("the generator compiles", code2 == 0 and os.path.isfile(tool), out2)
        if code != 0 or code2 != 0:
            return 1

        command = "%s %s --stdio" % (quote(obr), quote(server))
        stub = os.path.join(work, "WeatherClient.obs")
        code, out = run([obr, tool, "--cmd", command, "--class", "WeatherClient", "--out", stub], env, work)
        check("the generator reaches the server over stdio and writes the stub", code == 0 and os.path.isfile(stub), out)
        if code != 0:
            return 1
        check("it reports the tools it wrote", "wrote 6 tool(s)" in out, out)

        if os.environ.get("MCP_STUBS_UPDATE") == "1":
            with open(golden, "wb") as f:
                f.write(read(stub))
            print("  golden file refreshed")
        check("the stub equals the golden file", read(stub) == read(golden), "run with MCP_STUBS_UPDATE=1 to refresh after a deliberate change")

        again = os.path.join(work, "again.obs")
        code, out = run([obr, tool, "--cmd", command, "--class", "WeatherClient", "--out", again], env, work)
        check("a second run gives the same bytes", code == 0 and read(again) == read(stub), out)

        source = read(stub).decode("utf-8")
        for needle in ("method : public : Forecast(city : String) ~ String {",
                       "method : public : Forecast(city : String, days : Int, units : String) ~ String {",
                       "method : public : Convert(values : Float[], factor : Float) ~ String {",
                       "method : public : LookupJson(city : String) ~ JsonElement {",
                       "One of: metric, imperial",
                       "Destructive: the tool may destroy or overwrite data."):
            check("the stub has: " + needle, needle in source)

        use = os.path.join(work, "use_stub.obe")
        code, out = run([obc, "-src", os.path.join(data, "use_stub.obs") + "," + stub, "-lib", LIBS, "-dest", use], env, work)
        check("the generated stub compiles with a program that calls it", code == 0 and os.path.isfile(use), out)
        if code == 0:
            for label, args in (("default", []), ("JIT off", ["--jit=off"])):
                code, out = run([obr] + args + [use, command], env, work)
                check("the typed calls work (%s)" % label, code == 0 and "PASS: stub use, 11 checks" in out, out)

        # a call the compiler must refuse: a wrong argument type is caught at compile time, not by the server
        wrong = os.path.join(work, "wrong.obs")
        with open(wrong, "w") as f:
            f.write("use API.MCP;\nclass Wrong {\n\tfunction : Main(args : String[]) ~ Nil {\n"
                    "\t\tweather := WeatherClient->New(McpClient->Command(\"x\"));\n"
                    "\t\tweather->Forecast(\"Denver\", \"three\", \"metric\");\n\t}\n}\n")
        code, out = run([obc, "-src", wrong + "," + stub, "-lib", LIBS, "-dest", os.path.join(work, "wrong.obe")], env, work)
        check("a wrong argument type is a compile error", code != 0 and "Forecast" in out, out)

        # the command line
        code, out = run([obr, tool], env, work)
        check("no source is a usage error", code == 2 and "give one of --cmd and --http" in out, out)
        code, out = run([obr, tool, "--cmd", "x", "--http", "http://127.0.0.1:1/mcp"], env, work)
        check("both sources is a usage error", code == 2, out)
        code, out = run([obr, tool, "--wat", "1"], env, work)
        check("an unknown option is a usage error", code == 2 and "unknown option" in out, out)
        code, out = run([obr, tool, "--cmd"], env, work)
        check("a missing value is a usage error", code == 2 and "needs a value" in out, out)
        code, out = run([obr, tool, "--http", "http://127.0.0.1:1/mcp"], env, work)
        check("a server that cannot be reached is an error, not a stub", code == 1 and "could not reach the server" in out, out)

    if failures:
        print("FAIL: %d check(s) failed" % len(failures))
        return 1
    print("PASS: MCP stubs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
