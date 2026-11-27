#!/usr/bin/env python3
"""
'class Name : json' (docs/design/AGENT_SYNTAX.md): what the compiler says about a class it cannot derive a
schema from, where it says it, and what it must leave alone.

Usage: json_class_diag_test.py <bin_dir>

<bin_dir> holds obc and obr; the libraries are read from the sibling lib/ directory. No network is used.

Diagnostics (each source is compiled; obc must fail, name the problem and give the right line):
  no 'use API.LlmUtil;'; a function-typed field; a field that is a plain class; a field that is a json class
  declared further down; a class that holds itself; a two-dimensional array; a Map with Int keys; a parent
  class; a generic class; a constructor that takes parameters and no New(); 'json(x)' naming no field; a class
  with no fields (and one with only a static); 'json' misspelled; a function the class already has
  (JsonSchema, FromJson); Ask without 'use API.AgentJson;'; FromJson on a class that is not json.

Left alone (each source must compile and, run, print what it should):
  a class without ': json' (its code is unchanged: no JsonSchema is added); a json class in a bundle; a CRLF
  file; the optional list spelled over several lines; a json class as the parameter of an '@tool' function (the
  schema the tool gets is the class's).
"""
import os, subprocess, sys, tempfile

EXE = ".exe" if os.name == "nt" else ""
LIBS = "gen_collect,json,regex,llm_util,net,net_server,cipher,misc,agent,agent_json,mcp"
HEAD = "use API.LlmUtil, API.AgentJson, API.Agent, Data.JSON, Collection;\n\n"   # two lines


def run(cmd, env, cwd, timeout=300):
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, timeout=timeout)
    return p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace")


# (what, whole source, [text the message must contain], the line the error must be reported at)
BAD = [
    ("no use of API.LlmUtil", "use Data.JSON;\nclass W : json {\n  @a : Int;\n  New() {}\n}\n",
     ["needs 'use API.LlmUtil;'"], 2),
    ("a function-typed field", HEAD + "class W : json {\n  @f : (Int) ~ Int;\n  New() {}\n}\n",
     ["the field 'f' of the class 'W'", "function type cannot be a JSON field"], 3),
    ("a field that is a plain class", HEAD + "class Thing {\n  New() {}\n}\nclass W : json {\n  @t : Thing;\n  New() {}\n}\n",
     ["the field 't' of the class 'W'", "'Thing' cannot be a JSON field", "declare it as a record or as 'class Thing : json'"], 6),
    ("a json class declared further down", HEAD + "class W : json {\n  @p : Later;\n  New() {}\n}\nclass Later : json {\n  @x : Int;\n  New() {}\n}\n",
     ["the field 'p' of the class 'W'", "'Later'", "declared above the class that uses it"], 3),
    ("a class that holds itself", HEAD + "class W : json {\n  @me : W;\n  New() {}\n}\n",
     ["the field 'me' of the class 'W'", "declared above"], 3),
    ("a two-dimensional array", HEAD + "class W : json {\n  @g : Int[,];\n  New() {}\n}\n",
     ["the field 'g'", "two or more dimensions"], 3),
    ("a Map with Int keys", HEAD + "class W : json {\n  @m : Map<IntRef, IntRef>;\n  New() {}\n}\n",
     ["the field 'm'", "String keys"], 3),
    ("a parent class", HEAD + "class Base {\n  New() {}\n}\nclass W : json from Base {\n  @a : Int;\n  New() {}\n}\n",
     ["cannot extend another class"], 6),
    ("a generic class", HEAD + "class W<T> : json {\n  @a : Int;\n  New() {}\n}\n",
     ["cannot be generic"], 3),
    ("a constructor that needs parameters", HEAD + "class W : json {\n  @a : Int;\n  New(a : Int) { @a := a; }\n}\n",
     ["needs a constructor with no parameters"], 3),
    ("json() naming no field", HEAD + "class W : json(nope) {\n  @a : Int;\n  New() {}\n}\n",
     ["'json(nope)'", "no field of that name"], 3),
    ("a class with no fields", HEAD + "class W : json {\n  New() {}\n}\n",
     ["the class 'W' has no fields"], 3),
    ("a class with only a static", HEAD + "class W : json {\n  @n : static : Int;\n  New() {}\n}\n",
     ["the class 'W' has no fields"], 3),
    ("json misspelled", HEAD + "class W : jsn {\n  @a : Int;\n  New() {}\n}\n",
     ["Expected 'json' after the class name"], 3),
    ("a missing ) after the optional list", HEAD + "class W : json(a {\n  @a : Int;\n  New() {}\n}\n",
     ["Expected ')' after the optional fields"], 3),
    ("a JsonSchema the class already has", HEAD + "class W : json {\n  @a : Int;\n  New() {}\n  function : JsonSchema() ~ Int { return 1; }\n}\n",
     ["already has a function named 'JsonSchema'", "a 'json' class needs for itself"], 3),
    ("a FromJson the class already has", HEAD + "class W : json {\n  @a : Int;\n  New() {}\n  function : FromJson(j : JsonElement) ~ W { return W->New(); }\n}\n",
     ["already has a function named 'FromJson'"], 3),
    ("Ask without use API.AgentJson", "use API.LlmUtil, API.Agent, Data.JSON, Collection;\nclass W : json {\n  @a : Int;\n  New() {}\n}\nclass T {\n  function : Main(args : String[]) ~ Nil {\n    agent := Agent->New(Nil, Nil);\n    W->Ask(agent, \"x\");\n  }\n}\n",
     ["Ask"], 9),
    ("FromJson on a class that is not json", HEAD + "class W {\n  @a : Int;\n  New() {}\n}\nclass T {\n  function : Main(args : String[]) ~ Nil {\n    W->FromJson(Nil);\n  }\n}\n",
     ["FromJson"], 9),
]

PLAIN = HEAD + """class Plain {
  @a : Int;
  New() {}
  function : Main(args : String[]) ~ Nil {
    p := Plain->New();
    "plain ok"->PrintLine();
  }
}
"""

BUNDLED = HEAD + """bundle Shapes {
  class Dot : json(caption) {
    @x : Int;
    @caption : String;
    New() {}
    method : public : GetX() ~ Int { return @x; }
  }
}

class T {
  function : Main(args : String[]) ~ Nil {
    d := Shapes.Dot->FromJson(JsonParser->TextToElement("{\\"x\\":9}"));
    "{$d->GetX()} {$Shapes.Dot->JsonSchema()->Get(\\"required\\")->Size()}"->PrintLine();
  }
}
"""

MULTILINE = HEAD + """class W : json(
    a,
    b
  ) {
  @a : Int;
  @b : Int;
  @c : Int;
  New() {}
  function : Main(args : String[]) ~ Nil {
    "{$W->JsonSchema()->Get(\\"required\\")->Size()}"->PrintLine();
  }
}
"""

TOOL = HEAD + """use API.MCP;

class Spot : json {
  @x : Int;
  @name : String;
  New() {}
  method : public : GetX() ~ Int { return @x; }
}

class T {
  #~
  Reads a spot
  @tool
  ~#
  function : Look(p : Spot) ~ String {
    return "got {$p->GetX()}";
  }

  function : Main(args : String[]) ~ Nil {
    server := McpService->New("s", "1");
    T->McpTools(server);
    client := McpClient->InProcess(server);
    client->Initialize("c", "1");
    r := client->CallTool("look", JsonParser->TextToElement("{\\"p\\":{\\"x\\":4,\\"name\\":\\"n\\"}}"));
    r->GetText()->PrintLine();
    bad := client->CallTool("look", JsonParser->TextToElement("{\\"p\\":{\\"name\\":\\"n\\"}}"));
    "{$bad->IsError()}"->PrintLine();
  }
}
"""

OK = [
    ("a class without ': json' is unchanged", PLAIN, "plain ok", False),
    ("a json class in a bundle", BUNDLED, "9 1", False),
    ("the same in a CRLF file", BUNDLED, "9 1", True),
    ("the optional list over several lines", MULTILINE, "1", False),
    ("a json class as an '@tool' parameter", TOOL, "got 4\ntrue", False),
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

    with tempfile.TemporaryDirectory(prefix="json-class-") as work:
        def compile_text(name, text, crlf=False):
            src = os.path.join(work, name + ".obs")
            with open(src, "wb") as f:
                f.write(text.replace("\n", "\r\n").encode("utf-8") if crlf else text.encode("utf-8"))
            obe = os.path.join(work, name + ".obe")
            if os.path.exists(obe):
                os.remove(obe)
            code, out = run([obc, "-src", src, "-lib", LIBS, "-dest", obe], env, work)
            return code, out, obe

        for i, (what, text, needles, line) in enumerate(BAD):
            code, out, obe = compile_text("bad%d" % i, text)
            ok = code != 0 and not os.path.exists(obe) and all(n in out for n in needles)
            check("diagnostic: %s" % what, ok, out)
            check("  at line %d" % line, ":(%d," % line in out or ":%d:" % line in out or "(%d," % line in out, out)

        for i, (what, text, expect, crlf) in enumerate(OK):
            code, out, obe = compile_text("ok%d" % i, text, crlf)
            if code != 0:
                check("left alone: %s" % what, False, out)
                continue
            code, out = run([obr, obe], env, work)
            check("left alone: %s" % what, code == 0 and out.replace("\r", "").strip().endswith(expect), out)

        # a class without ': json' really has no JsonSchema (the feature adds nothing to it)
        code, out, obe = compile_text("nojson", HEAD + "class W {\n  @a : Int;\n  New() {}\n}\nclass T {\n  function : Main(args : String[]) ~ Nil {\n    W->JsonSchema();\n  }\n}\n")
        check("a class without ': json' gets no JsonSchema", code != 0 and "JsonSchema" in out, out)

    if failures:
        print("FAIL: %d check(s)" % len(failures))
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
