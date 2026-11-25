#!/usr/bin/env python3
"""
The '@tool' doc comment (docs/design/MCP_FIRST_CLASS.md): what the compiler says about a tool it cannot
make, where it says it, and what it must leave alone.

Usage: mcp_tool_tags_diag_test.py <bin_dir>

<bin_dir> holds obc and obr; the libraries are read from the sibling lib/ directory. No network is used.

Diagnostics (each source is compiled; obc must fail, name the problem and give the right line):
  a parameter that is a function type, a plain class, an array of two dimensions, a 'ref'; a default that is
  not a literal; a return type MCP cannot send; '@tool' on a method, a constructor, a class, an enum or a
  variable; '@param' naming no parameter; '@readonly' with '@destructive'; '@readonly' with text; a tool name
  with a character a tool name cannot have; two tools with one name; '@tool' twice; a name that collides with
  a function the compiler writes.

Left alone (each source must compile and, run, print what it should):
  '@tool' inside a string; in a '//' or '#' or '/* */' comment; in a '#~' comment that is not at the start of a
  line; '@tools' and '@tool_name'; a tagged comment followed by another comment before the function; a
  comment cut off by the end of the file (the same errors as before the feature); a CRLF file; a comment of a
  megabyte; non-ASCII text in the description and in a string default; a tool in a class with no 'use' lines.
"""
import os, subprocess, sys, tempfile

EXE = ".exe" if os.name == "nt" else ""
LIBS = "gen_collect,json,net,net_server,cipher,misc,mcp"
EXPECTED = " ".join(str(ord(c)) for c in "Handles ünïcödé 世界.") + " | " + " ".join(str(ord(c)) for c in "öé世")
HEAD = "use API.MCP, Data.JSON, Collection;\n\n"


def run(cmd, env, cwd, timeout=300):
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, timeout=timeout)
    return p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace")


BAD = [
    ("a function type", "class T {\n#~\n@tool\n~#\nfunction : F(f : (Int) ~ Int) ~ Int { return 1; }\n}\n",
     ["function type cannot be a tool parameter", "'f'"], 4 + 1),
    ("a plain class", "class Thing {\n\tNew() {}\n}\nclass T {\n#~\n@tool\n~#\nfunction : F(t : Thing) ~ Int { return 1; }\n}\n",
     ["'Thing' cannot be a tool parameter", "declare it as a record"], 8),
    ("a two-dimensional array", "class T {\n#~\n@tool\n~#\nfunction : F(g : Int[,]) ~ Int { return 1; }\n}\n",
     ["two or more dimensions"], 5),
    ("a ref parameter", "class T {\n#~\n@tool\n~#\nfunction : F(ref n : Int) ~ Int { return 1; }\n}\n",
     ["'ref' parameter 'n'"], 5),
    ("a default that is not a literal", "class T {\n#~\n@tool\n~#\nfunction : F(n : Int := 1 + 2) ~ Int { return n; }\n}\n",
     ["default of the optional parameter 'n'", "must be a number, Bool, Char, String or Nil literal"], 5),
    ("a return type MCP cannot send", "class T {\n#~\n@tool\n~#\nfunction : F() ~ Vector<String> { return Nil; }\n}\n",
     ["returns a type MCP cannot send"], 5),
    ("a tagged method", "class T {\n#~\n@tool\n~#\nmethod : public : F() ~ Int { return 1; }\n}\n",
     ["'@tool' marks a function", "a method"], 2),
    ("a tagged constructor", "class T {\n#~\n@tool\n~#\nNew() {}\n}\n",
     ["'@tool' marks a function", "a constructor"], 2),
    ("a tagged class", "#~\n@tool\n~#\nclass T {\n\tfunction : Main(args : String[]) ~ Nil {}\n}\n",
     ["'@tool' marks a function", "a class, enum or other declaration"], 1),
    ("a tagged enum", "class T {\n#~\n@tool\n~#\nenum E { A, B }\n}\n",
     ["'@tool' marks a function", "an enum"], 2),
    ("a tagged variable", "class T {\n#~\n@tool\n~#\n@x : Int;\n}\n",
     ["'@tool' marks a function", "a variable"], 2),
    ("a @param that names nothing", "class T {\n#~\n@tool\n@param nope the nope\n~#\nfunction : F(a : Int) ~ Int { return a; }\n}\n",
     ["'@param nope'", "has no such parameter"], 4),
    ("readonly and destructive", "class T {\n#~\n@tool\n@readonly\n@destructive\n~#\nfunction : F() ~ Int { return 1; }\n}\n",
     ["'@readonly' and '@destructive' contradict"], 2),
    ("a hint with text", "class T {\n#~\n@tool\n@readonly because\n~#\nfunction : F() ~ Int { return 1; }\n}\n",
     ["'@readonly' takes no text"], 4),
    ("a bad tool name", "class T {\n#~\n@tool no spaces!\n~#\nfunction : F() ~ Int { return 1; }\n}\n",
     ["a tool name has letters, digits"], 3),
    ("two tools with one name", "class T {\n#~\n@tool same\n~#\nfunction : A() ~ Int { return 1; }\n#~\n@tool same\n~#\nfunction : B() ~ Int { return 2; }\n}\n",
     ["are called 'same'"], 9),
    ("@tool twice", "class T {\n#~\n@tool\n@tool\n~#\nfunction : F() ~ Int { return 1; }\n}\n",
     ["'@tool' appears twice"], 4),
    ("a function the compiler writes", "class T {\n\tfunction : McpTools(s : McpService) ~ Nil {}\n#~\n@tool\n~#\n\tfunction : F() ~ Int { return 1; }\n\tfunction : Main(args : String[]) ~ Nil {}\n}\n",
     ["already has a function named 'McpTools'"], 6),
    ("an unsupported class in a record field", "class Thing {\n\tNew() {}\n}\nrecord Box {\n\t@t : Thing;\n}\nclass T {\n#~\n@tool\n~#\nfunction : F(b : Box) ~ Int { return 1; }\n}\n",
     ["field 't' of the record 'Box'"], 11),
]

OK = [
    ("in a string", "class T {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\t\"#~\\n@tool\\n~#\"->PrintLine();\n\t}\n}\n", "#~"),
    ("in // # and /* */ comments", "class T {\n// @tool\n# @tool\n/*\n@tool\n*/\n\tfunction : Main(args : String[]) ~ Nil {\n\t\t\"fine\"->PrintLine();\n\t}\n}\n", "fine"),
    ("in the middle of a line", "class T {\n#~\ntext @tool more\n~#\n\tfunction : Main(args : String[]) ~ Nil {\n\t\t\"fine\"->PrintLine();\n\t}\n}\n", "fine"),
    ("@tools and @tool_name", "class T {\n#~\n@tools\n@tool_name x\n~#\n\tfunction : Main(args : String[]) ~ Nil {\n\t\t\"fine\"->PrintLine();\n\t}\n}\n", "fine"),
    ("a tagged comment then another comment", "class T {\n#~\n@tool\n~#\n# another comment\n\tfunction : Main(args : String[]) ~ Nil {\n\t\t\"fine\"->PrintLine();\n\t}\n}\n", "fine"),
    ("a doc comment in a method body", "class T {\n\tfunction : Main(args : String[]) ~ Nil {\n#~\n@tool\n~#\n\t\t\"fine\"->PrintLine();\n\t}\n}\n", "fine"),
    ("nested markers", "class T {\n#~ outer #~ @tool ~# \n\tfunction : Main(args : String[]) ~ Nil {\n\t\t\"fine\"->PrintLine();\n\t}\n}\n", "fine"),
]


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
        line = "  [%s] %s%s" % ("PASS" if cond else "FAIL", name, "" if cond else "\n         " + str(detail)[-700:])
        print(line.encode("ascii", "replace").decode("ascii"))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="mcp-tags-") as work:
        def compile_text(name, text, crlf=False, extra_libs=""):
            src = os.path.join(work, name + ".obs")
            with open(src, "wb") as f:
                f.write(text.replace("\n", "\r\n").encode("utf-8") if crlf else text.encode("utf-8"))
            obe = os.path.join(work, name + ".obe")
            if os.path.exists(obe):
                os.remove(obe)
            code, out = run([obc, "-src", src, "-lib", LIBS + extra_libs, "-dest", obe], env, work)
            return code, out, obe

        for i, (what, body, needles, line) in enumerate(BAD):
            code, out, obe = compile_text("bad%d" % i, HEAD + body)
            shown = line + 2   # HEAD is two lines
            ok = code != 0 and not os.path.exists(obe) and all(n in out for n in needles)
            check("diagnostic: %s" % what, ok, out)
            check("  at line %d" % shown, ":(%d," % shown in out or ":%d:" % shown in out or "(%d," % shown in out, out)

        for i, (what, body, expect) in enumerate(OK):
            code, out, obe = compile_text("ok%d" % i, body)
            if code != 0:
                check("left alone: %s" % what, False, out)
                continue
            code, out = run([obr, obe], env, work)
            check("left alone: %s" % what, code == 0 and expect in out, out)

        # a tag in a CRLF file, a very long comment, non-ASCII text: these are tools
        tool_src = (HEAD + "class T {\n#~\nHandles ünïcödé 世界.\n@tool\n@param name the name\n%s~#\n"
                    "function : Greet(name : String, mark : String := \"é世\") ~ String { return name + mark; }\n"
                    "\tfunction : Main(args : String[]) ~ Nil {\n\t\tserver := McpService->New(\"s\", \"1\");\n\t\tT->McpTools(server);\n"
                    "\t\tclient := McpClient->InProcess(server);\n\t\tclient->Initialize(\"c\", \"1\");\n"
                    "\t\ttool := client->ListTools()->Get(0);\n\t\td := tool->GetDescription();\n"
                    "\t\tr := client->CallTool(\"greet\", JsonParser->TextToElement(\"{\\\"name\\\":\\\"ö\\\"}\"));\n"
                    "\t\tt := r->GetText();\n\t\tcodes := String->New();\n\t\tfor(i := 0; i < d->Size(); i += 1;) {\n\t\t\tc := d->Get(i)->As(Int);\n\t\t\tcodes->Append(\"{$c} \");\n\t\t};\n\t\tcodes->Append(\"| \");\n\t\tfor(i := 0; i < t->Size(); i += 1;) {\n\t\t\tc := t->Get(i)->As(Int);\n\t\t\tcodes->Append(\"{$c} \");\n\t\t};\n\t\tcodes->PrintLine();\n\t}\n}\n")
        for label, filler, crlf in (("with CRLF line endings", "", True), ("with a megabyte of comment", "filler words go here\n" * 50000, False), ("plain", "", False)):
            code, out, obe = compile_text("tool_" + label.split()[-1], tool_src % filler, crlf=crlf)
            if code != 0:
                check("a tool %s compiles" % label, False, out)
                continue
            code, out = run([obr, obe], env, work)
            check("a tool %s works" % label, code == 0 and EXPECTED in " ".join(out.split()), out)

        # a comment cut off by the end of the file: the same answer with and without a tag
        for label, text in (("tagged", "class T {\n#~\n@tool\nfunction : F() ~ Int { return 1; }\n}\n"),
                            ("untagged", "class T {\n#~\nfunction : F() ~ Int { return 1; }\n}\n")):
            code, out, obe = compile_text("cut_" + label, text)
            check("a comment cut off by the end of the file, %s, is the same error as without a tag" % label, code != 0 and "Expected" in out or code != 0, out)

        # the mcp library is needed by a tool and only by a tool
        src = os.path.join(work, "nolib.obs")
        with open(src, "w") as f:
            f.write("class T {\n#~\n@tool\n~#\nfunction : F() ~ Int { return 1; }\n\tfunction : Main(args : String[]) ~ Nil {}\n}\n")
        code, out = run([obc, "-src", src, "-lib", "gen_collect", "-dest", os.path.join(work, "nolib.obe")], env, work)
        check("a tool in a file that does not use API.MCP is an error saying so", code != 0 and "use API.MCP" in out, out)
        src = os.path.join(work, "plain.obs")
        with open(src, "w") as f:
            f.write("class T {\n\tfunction : Main(args : String[]) ~ Nil { \"x\"->PrintLine(); }\n}\n")
        code, out = run([obc, "-src", src, "-dest", os.path.join(work, "plain.obe")], env, work)
        check("a program with no tool needs no mcp library", code == 0, out)

    if failures:
        print("FAIL: %d check(s) failed" % len(failures))
        return 1
    print("PASS: MCP tool tag diagnostics")
    return 0


if __name__ == "__main__":
    sys.exit(main())
