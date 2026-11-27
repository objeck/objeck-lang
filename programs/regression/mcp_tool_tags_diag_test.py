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
  a function the compiler writes; a McpMain(args : String[]) that is a method, that is virtual, or that
  returns a value (the entry 'obr --mcp' starts must be 'function : McpMain(args : String[]) ~ Nil'), in a
  program that is an MCP server: some class of the program, in this file or another, has a tagged function,
  or the McpMain is a function (a virtual one, or one that returns a value) in a file with 'use API.MCP'.

Left alone (each source must compile and, run, print what it should):
  '@tool' inside a string; in a '//' or '#' or '/* */' comment; in a '#~' comment that is not at the start of a
  line; '@tools' and '@tool_name'; a tagged comment followed by another comment before the function; a
  comment cut off by the end of the file (the same errors as before the feature); a CRLF file; a comment of a
  megabyte; non-ASCII text in the description and in a string default; a tool in a class with no 'use' lines;
  methods named McpMain with another parameter list; the entry itself; in a program that has nothing to do
  with MCP (no 'use API.MCP', no tagged function, one file or two) a method, a virtual method and a function
  that returns a value, each named McpMain(args : String[]); in a client, which imports API.MCP and has no
  tagged function, an instance method McpMain(args : String[]) (returning a value or Nil, or virtual with an
  override), called on an object.

Several files (the tools are derived when every file has been read):
  a record declared in a later file than the function that takes it; a syntax error in one file does not hide
  a bad tag in another, in either order; a record in a file that does not parse gives that file's syntax error
  and no complaint about the type.
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
    ("a default that interpolates", "class T {\n#~\n@tool\n~#\nfunction : F(a : String, n : String := \"hi {$a}\") ~ String { return n; }\n}\n",
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
    # found by the scanner fuzzer: the function does not parse, so there are no types to derive from; the syntax error is the answer (this crashed obc)
    ("a tagged function that does not parse", "use API.MCP;\nclass T {\n#~\n@tool\n~#\nfunction : F(p\n#~ @tool ~#\nath : String) ~ String {\n  return \"x\";\n}\n}\n",
     ["Expected ':'"], 8),
    # ---- Stage 2b: resources and prompts ----
    ("a resource with no URI", "class T {\n#~\n@resource\n~#\nfunction : F() ~ String { return \"x\"; }\n}\n",
     ["'@resource' needs a URI"], 3),
    ("a URI variable with no parameter", "class T {\n#~\n@resource a://{x}\n~#\nfunction : F() ~ String { return \"x\"; }\n}\n",
     ["has the variable '{x}'", "no parameter of that name"], 5),
    ("a resource parameter that is not in the URI", "class T {\n#~\n@resource a://b\n~#\nfunction : F(n : Int) ~ String { return \"x\"; }\n}\n",
     ["'n' of the resource", "not a variable of its URI"], 5),
    ("a resource parameter with a default", "class T {\n#~\n@resource a://{n}\n~#\nfunction : F(n : Int := 1) ~ String { return \"x\"; }\n}\n",
     ["has a default", "every variable of a URI template is required"], 5),
    ("a resource parameter of a type text cannot be", "class T {\n#~\n@resource a://{n}\n~#\nfunction : F(n : Int[]) ~ String { return \"x\"; }\n}\n",
     ["gets its arguments as text"], 5),
    ("a prompt parameter of a type text cannot be", "class T {\n#~\n@prompt\n~#\nfunction : F(n : Vector<String>) ~ String { return \"x\"; }\n}\n",
     ["gets its arguments as text", "(or the McpCall)"], 5),
    ("a resource that returns an Int", "class T {\n#~\n@resource a://b\n~#\nfunction : F() ~ Int { return 1; }\n}\n",
     ["returns a type MCP cannot send", "McpContents"], 5),
    ("a prompt that returns a JsonElement", "class T {\n#~\n@prompt\n~#\nfunction : F() ~ JsonElement { return Nil; }\n}\n",
     ["returns a type MCP cannot send", "McpPromptResult"], 5),
    ("a tool and a resource in one comment", "class T {\n#~\n@tool\n@resource a://b\n~#\nfunction : F() ~ String { return \"x\"; }\n}\n",
     ["a function is one of '@tool', '@resource' and '@prompt'"], 4),
    ("a resource and a prompt in one comment", "class T {\n#~\n@resource a://b\n@prompt\n~#\nfunction : F() ~ String { return \"x\"; }\n}\n",
     ["a function is one of '@tool', '@resource' and '@prompt'"], 4),
    ("a hint on a resource", "class T {\n#~\n@resource a://b\n@readonly\n~#\nfunction : F() ~ String { return \"x\"; }\n}\n",
     ["describe a tool", "is a resource"], 2),
    ("a mime type on a tool", "class T {\n#~\n@tool\n@mime text/plain\n~#\nfunction : F() ~ String { return \"x\"; }\n}\n",
     ["'@mime' belongs to a '@resource'", "is a tool"], 2),
    ("two resources with one URI", "class T {\n#~\n@resource a://b\n~#\nfunction : A() ~ String { return \"x\"; }\n#~\n@resource a://b\n~#\nfunction : B() ~ String { return \"y\"; }\n}\n",
     ["two '@resource' functions", "'a://b'"], 9),
    ("two prompts with one name", "class T {\n#~\n@prompt same\n~#\nfunction : A() ~ String { return \"x\"; }\n#~\n@prompt same\n~#\nfunction : B() ~ String { return \"y\"; }\n}\n",
     ["two '@prompt' functions", "'same'"], 9),
    ("a URI with an unclosed variable", "class T {\n#~\n@resource a://{b\n~#\nfunction : F() ~ String { return \"x\"; }\n}\n",
     ["a '{' needs a name and a closing '}'"], 2),
    ("a bad prompt name", "class T {\n#~\n@prompt no spaces!\n~#\nfunction : F() ~ String { return \"x\"; }\n}\n",
     ["a prompt name has letters, digits"], 3),
    ("a tagged resource on a method", "class T {\n#~\n@resource a://b\n~#\nmethod : public : F() ~ String { return \"x\"; }\n}\n",
     ["'@tool' marks a function", "a method"], 2),
    # McpMain(args : String[]) is the entry 'obr --mcp' starts, which the VM picks by name and parameter alone
    ("a McpMain(args : String[]) that is a method, in a program with a tool in another class",
     "class T {\n\tNew() {}\n\tmethod : public : McpMain(args : String[]) ~ Nil {\n\t\t\"x\"->PrintLine();\n\t}\n}\nclass Tools {\n#~\n@tool\n~#\nfunction : F() ~ Int { return 1; }\n}\n",
     ["'McpMain(args : String[])' is the function 'obr --mcp' starts", "'function : McpMain(args : String[]) ~ Nil'", "declared as a method"], 3),
    ("a virtual McpMain(args : String[]), in a program with a resource in another class",
     "class T {\n\tmethod : virtual : McpMain(args : String[]) ~ Nil;\n}\nclass Tools {\n#~\n@resource a://b\n~#\nfunction : F() ~ String { return \"x\"; }\n}\n",
     ["'McpMain(args : String[])' is the function 'obr --mcp' starts", "declared as a method"], 2),
    ("a McpMain(args : String[]) that is a method and returns a value, in a program with a prompt in another class",
     "class T {\n\tNew() {}\n\tmethod : public : McpMain(args : String[]) ~ Int {\n\t\treturn 1;\n\t}\n}\nclass Tools {\n#~\n@prompt\n~#\nfunction : F() ~ String { return \"x\"; }\n}\n",
     ["'McpMain(args : String[])' is the function 'obr --mcp' starts", "declared as a method"], 3),
    # a function with the entry's name and parameter in a file that imports API.MCP is a server's entry, written wrongly
    ("a virtual function McpMain(args : String[])", "class T {\n\tfunction : virtual : McpMain(args : String[]) ~ Nil;\n}\n",
     ["'McpMain(args : String[])' is the function 'obr --mcp' starts", "it is virtual, so it has no body"], 2),
    ("a McpMain(args : String[]) that returns a value", "class T {\n\tfunction : McpMain(args : System.String[]) ~ Int {\n\t\treturn 1;\n\t}\n}\n",
     ["'McpMain(args : String[])' is the function 'obr --mcp' starts", "it returns a value"], 2),
    ("a McpMain(args : String[]) that is a method, in a class with a tool",
     "class T {\n\tNew() {}\n#~\n@tool\n~#\n\tfunction : F() ~ Int { return 1; }\n\tmethod : public : McpMain(args : String[]) ~ Nil {\n\t\t\"x\"->PrintLine();\n\t}\n}\n",
     ["'McpMain(args : String[])' is the function 'obr --mcp' starts", "declared as a method"], 7),
]

OK = [
    ("in a string", "class T {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\t\"#~\\n@tool\\n~#\"->PrintLine();\n\t}\n}\n", "#~"),
    ("in // # and /* */ comments", "class T {\n// @tool\n# @tool\n/*\n@tool\n*/\n\tfunction : Main(args : String[]) ~ Nil {\n\t\t\"fine\"->PrintLine();\n\t}\n}\n", "fine"),
    ("in the middle of a line", "class T {\n#~\ntext @tool more\n~#\n\tfunction : Main(args : String[]) ~ Nil {\n\t\t\"fine\"->PrintLine();\n\t}\n}\n", "fine"),
    ("@tools and @tool_name", "class T {\n#~\n@tools\n@tool_name x\n~#\n\tfunction : Main(args : String[]) ~ Nil {\n\t\t\"fine\"->PrintLine();\n\t}\n}\n", "fine"),
    ("a tagged comment then another comment", "class T {\n#~\n@tool\n~#\n# another comment\n\tfunction : Main(args : String[]) ~ Nil {\n\t\t\"fine\"->PrintLine();\n\t}\n}\n", "fine"),
    ("a doc comment in a method body", "class T {\n\tfunction : Main(args : String[]) ~ Nil {\n#~\n@tool\n~#\n\t\t\"fine\"->PrintLine();\n\t}\n}\n", "fine"),
    ("a method named McpMain with another parameter list", "class T {\n\tNew() {}\n\tmethod : public : McpMain(n : Int) ~ Int {\n\t\treturn n + 1;\n\t}\n\tmethod : public : McpMain(a : String[], b : Int) ~ Int {\n\t\treturn b;\n\t}\n\tmethod : public : McpMain(a : String) ~ Int {\n\t\treturn 7;\n\t}\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tt := T->New();\n\t\tx := t->McpMain(1) + t->McpMain(args, 3) + t->McpMain(\"s\");\n\t\t\"fine {$x}\"->PrintLine();\n\t}\n}\n", "fine 12"),
    ("the entry itself, function McpMain(args : String[]) ~ Nil", "class T {\n\tfunction : McpMain(args : String[]) ~ Nil {\n\t\t\"entry\"->PrintLine();\n\t}\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tMcpMain(args);\n\t\t\"fine\"->PrintLine();\n\t}\n}\n", "fine"),
    # McpMain(args : String[]) is an ordinary name in a program that is not an MCP server
    ("an instance method McpMain(args : String[]) ~ Int in a program with no MCP", "class T {\n\t@n : Int;\n\tNew() { @n := 3; }\n\tmethod : public : McpMain(args : String[]) ~ Int {\n\t\treturn @n + args->Size();\n\t}\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tx := T->New()->McpMain(args);\n\t\t\"fine {$x}\"->PrintLine();\n\t}\n}\n", "fine 3"),
    ("an instance method McpMain(args : String[]) ~ Nil in a program with no MCP", "class T {\n\t@n : Int;\n\tNew() { @n := 4; }\n\tmethod : public : McpMain(args : String[]) ~ Nil {\n\t\t\"fine {$@n}\"->PrintLine();\n\t}\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tT->New()->McpMain(args);\n\t}\n}\n", "fine 4"),
    ("a virtual McpMain(args : String[]) in a program with no MCP", "class Served {\n\tNew() {}\n\tmethod : virtual : public : McpMain(args : String[]) ~ Nil;\n}\nclass T from Served {\n\tNew() { Parent(); }\n\tmethod : public : McpMain(args : String[]) ~ Nil {\n\t\t\"fine virtual\"->PrintLine();\n\t}\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tb : Served := T->New();\n\t\tb->McpMain(args);\n\t}\n}\n", "fine virtual"),
    ("a function McpMain(args : String[]) ~ Int in a program with no MCP", "class T {\n\tfunction : McpMain(args : String[]) ~ Int {\n\t\treturn 8;\n\t}\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tx := McpMain(args);\n\t\t\"fine {$x}\"->PrintLine();\n\t}\n}\n", "fine 8"),
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

        # several files: the tools are derived when every file has been read
        def compile_files(name, files):
            paths = []
            for i, text in enumerate(files):
                src = os.path.join(work, "%s_%d.obs" % (name, i))
                with open(src, "wb") as f:
                    f.write(text.encode("utf-8"))
                paths.append(src)
            obe = os.path.join(work, name + ".obe")
            if os.path.exists(obe):
                os.remove(obe)
            code, out = run([obc, "-src", ",".join(paths), "-lib", LIBS, "-dest", obe], env, work)
            return code, out, obe

        record_src = "record Point {\n\t@x : Int;\n\t@y : Int;\n}\n"
        sum_src = (HEAD + "class T {\n#~\n@tool\n~#\nfunction : Sum(p : Point) ~ Int { return p->GetX() + p->GetY(); }\n"
                   "\tfunction : Main(args : String[]) ~ Nil {\n\t\tserver := McpService->New(\"s\", \"1\");\n\t\tT->McpTools(server);\n"
                   "\t\tclient := McpClient->InProcess(server);\n\t\tclient->Initialize(\"c\", \"1\");\n"
                   "\t\tclient->CallTool(\"sum\", JsonParser->TextToElement(\"{\\\"p\\\":{\\\"x\\\":3,\\\"y\\\":4}}\"))->GetText()->PrintLine();\n\t}\n}\n")
        for label, files in (("a later file", [sum_src, record_src]), ("an earlier file", [record_src, sum_src])):
            code, out, obe = compile_files("rec_" + label.split()[1], files)
            if code != 0:
                check("a record declared in %s than the tool compiles" % label, False, out)
                continue
            code, out = run([obr, obe], env, work)
            check("a record declared in %s than the tool works" % label, code == 0 and out.strip() == "7", out)

        # McpMain(args : String[]) is the entry only in a program that is an MCP server. A file without
        # 'use API.MCP' is one when some class of the program has a tagged function, wherever it is: the
        # compiler writes an entry for that class and a McpMain the program wrote is started in its place.
        method_src = "class App {\n\tNew() {}\n\tmethod : public : McpMain(args : String[]) ~ Nil {\n\t\t\"app\"->PrintLine();\n\t}\n}\n"
        tagged_src = "class Tagged {\n#~\n@tool\n~#\nfunction : F() ~ Int { return 1; }\n}\n"
        main_src = "class Go {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tApp->New()->McpMain(args);\n\t}\n}\n"
        code, out, obe = compile_text("entry_same", method_src + tagged_src + main_src)
        check("a McpMain(args : String[]) that is a method, in a file with a tagged function and no 'use', is refused at line 3",
              code != 0 and not os.path.exists(obe) and "'McpMain(args : String[])' is the function 'obr --mcp' starts" in out and "declared as a method" in out and ":(3," in out, out)
        for label, files in (("an earlier", [tagged_src, method_src + main_src]), ("a later", [method_src + main_src, tagged_src])):
            code, out, obe = compile_files("entry_" + label.split()[1], files)
            at = [l for l in out.splitlines() if "is the function 'obr --mcp' starts" in l]
            check("a McpMain(args : String[]) that is a method is refused when %s file has a tagged function, at its own file and line" % label,
                  code != 0 and not os.path.exists(obe) and len(at) == 1 and "declared as a method" in at[0] and ":(3," in at[0] and
                  ("entry_%s_%d.obs" % (label.split()[1], 1 if label == "an earlier" else 0)) in at[0], out)
        code, out, obe = compile_files("entry_none", [method_src, main_src])
        check("two files with no tagged function and no 'use API.MCP': the method named McpMain compiles", code == 0 and os.path.exists(obe), out)
        if code == 0:
            code, out = run([obr, obe], env, work)
            check("  and runs", code == 0 and out.strip() == "app", out)
        # 'use API.MCP' alone does not make a program a server: an MCP client imports it too. Its instance
        # method McpMain(args : String[]), called on an object, was refused for the import ("declared as a
        # method, which needs an instance"). With no tagged function in the program it is an ordinary method,
        # whatever it returns, and so is a virtual one with its override; each compiles and runs.
        client_head = "use API.MCP, Collection;\n\n"
        client_main = ("class Go {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tclient : McpClient;\n\t\trunner := Runner->New();\n"
                       "\t\t%s\n\t\tif(client = Nil) { \"no server\"->PrintLine(); };\n\t}\n}\n")
        for label, runner, call, expect in (
                ("an instance method McpMain(args : String[]) ~ Int",
                 "class Runner {\n\t@base : Int;\n\tNew() { @base := 40; }\n\tmethod : public : McpMain(args : String[]) ~ Int {\n\t\treturn @base + args->Size();\n\t}\n}\n",
                 "runner->McpMain(args)->PrintLine();", "40\nno server"),
                ("an instance method McpMain(args : String[]) ~ Nil",
                 "class Runner {\n\t@base : Int;\n\tNew() { @base := 41; }\n\tmethod : public : McpMain(args : String[]) ~ Nil {\n\t\t@base->PrintLine();\n\t}\n}\n",
                 "runner->McpMain(args);", "41\nno server"),
                ("a virtual McpMain(args : String[]) and its override",
                 "class Served {\n\tNew() {}\n\tmethod : virtual : public : McpMain(args : String[]) ~ Int;\n}\n"
                 "class Runner from Served {\n\tNew() { Parent(); }\n\tmethod : public : McpMain(args : String[]) ~ Int {\n\t\treturn 42;\n\t}\n}\n",
                 "served : Served := runner;\n\t\tserved->McpMain(args)->PrintLine();", "42\nno server")):
            code, out, obe = compile_text("client", client_head + runner + client_main % call)
            check("a client that imports API.MCP: %s is an ordinary method" % label, code == 0 and os.path.exists(obe), out)
            if code == 0:
                code, out = run([obr, obe], env, work)
                check("  and runs", code == 0 and out.replace("\r", "").strip() == expect, out)
        # the same file once a class of the program has a tagged function: a server, and the method is refused
        code, out, obe = compile_files("client_tagged", [client_head + "class Runner {\n\tNew() {}\n\tmethod : public : McpMain(args : String[]) ~ Int {\n\t\treturn 1;\n\t}\n}\n" +
                                                         client_main % "runner->McpMain(args)->PrintLine();", tagged_src])
        at = [l for l in out.splitlines() if "is the function 'obr --mcp' starts" in l]
        check("the same method is refused once another file has a tagged function, at its own file and line",
              code != 0 and not os.path.exists(obe) and len(at) == 1 and "declared as a method" in at[0] and "client_tagged_0.obs:(5," in at[0], out)
        # a function is the shape of an entry: with the import it is a server's entry written wrongly, as before
        code, out, obe = compile_text("client_function", client_head + "class Runner {\n\tfunction : McpMain(args : String[]) ~ Int {\n\t\treturn 1;\n\t}\n}\n")
        check("a function McpMain(args : String[]) ~ Int in a file that imports API.MCP is still refused, at line 4",
              code != 0 and not os.path.exists(obe) and "it returns a value" in out and ":(4," in out, out)

        # the reason is not the name of a class: a class called Tagged, a string and a comment that say @tool
        code, out, obe = compile_text("entry_words", "# @tool\n" + method_src + "class Tagged {\n\tfunction : F() ~ String { return \"@tool\"; }\n}\n" + main_src)
        check("a program whose only '@tool' is in a string and a line comment is not a server", code == 0 and os.path.exists(obe), out)

        broken_src = "class Broken {\n\tfunction : Oops() ~ Int {\n\t\treturn 1 +;\n\t}\n}\n"
        bad_tag_src = HEAD + "class T {\n#~\n@tool\n@param nope the nope\n~#\nfunction : F(a : Int) ~ Int { return a; }\n\tfunction : Main(args : String[]) ~ Nil {}\n}\n"
        for label, files in (("before", [broken_src, bad_tag_src]), ("after", [bad_tag_src, broken_src])):
            code, out, obe = compile_files("two_" + label, files)
            check("a syntax error in a file %s it does not hide a bad tag" % label,
                  code != 0 and not os.path.exists(obe) and "Expected expression" in out and "'@param nope'" in out and "has no such parameter" in out, out)
            # the files print in the order they were given, though the tag is checked after the last file is read
            syntax_at, tag_at = out.find("Expected expression"), out.find("'@param nope'")
            check("  and the two files' errors print in the order the files were given",
                  syntax_at >= 0 and tag_at >= 0 and (syntax_at < tag_at) == (label == "before"), out)

        broken_record_src = "record Point {\n\t@x : Int;\n\t@y : Int\n}\n"
        for label, files in (("before", [broken_record_src, sum_src]), ("after", [sum_src, broken_record_src])):
            code, out, obe = compile_files("brec_" + label, files)
            check("a record in a file that does not parse, %s the tool, is that file's syntax error only" % label,
                  code != 0 and not os.path.exists(obe) and "Expected ';'" in out and "cannot be a tool parameter" not in out, out)

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
