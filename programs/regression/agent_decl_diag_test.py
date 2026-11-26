#!/usr/bin/env python3
"""
Agent declarations (docs/design/AGENT_SYNTAX.md, section 3): what the compiler says about a declaration it
cannot make, where it says it, and what it must leave alone.

Usage: agent_decl_diag_test.py <bin_dir>

<bin_dir> holds obc and obr; the libraries are read from the sibling lib/ directory. No network is used.

Diagnostics (each source is compiled, as an LF and as a CRLF file; obc must fail, write no program, name the
problem and point at the line and column of the text that is wrong):
  the declaration itself: an unknown setting, a setting twice, no backend, an empty list, a list that ends in a
  comma, a missing ';', a missing ':', a setting that is not a name, a declaration that is not closed; an unknown
  limit, a limit twice, an empty budget, a budget without braces; a parameter the compiler's own names could
  clash with; one function given as a tool; a class with no '@tool' function given as a tool.
  the user's own text, reported where it was written: a backend, instructions, tool, MCP server, tool name,
  limit or trace of the wrong type; a class that does not exist; an undefined name on the second line of a
  value; a parenthesis that is not closed; two values where one is expected; a parameter of an unknown type,
  also when the declaration is the first line of the file; a mistake in the second setting of a line that
  holds two.

At run time (the program compiles; it must stop with the message and a non-zero exit):
  a tool that is Nil, an MCP server that is Nil.

Left alone or supported (each source must compile and, run, print what it should):
  an agent in a named bundle, reached by its full name and through 'use'; an agent on the first line of a file;
  a CRLF file; an agent whose tools class is in another file; an agent that needs no MCP library; 'agent' as the
  name of a class with a function called Build; 'agent' as a variable at the start of a statement, a function, a
  method, a parameter and an instance variable; a program with no declaration compiles without the agent library.

Damaged programs (syntax_mutants.py): 300 random mutants of agent_decl_test.obs, from a fixed seed, are compiled;
the compiler may accept or refuse each one, and must not crash or hang on any.
"""
import os, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import syntax_mutants  # noqa: E402

EXE = ".exe" if os.name == "nt" else ""
LIBS = "gen_collect,json,mcp,agent,mcp_agent,net,net_server,cipher,misc"

HEAD = """use API.MCP, API.Agent, Data.JSON, Collection;

class Tools {
\t#~
\tAdd two integers.
\t@tool
\t~#
\tfunction : Add(a : Int, b : Int) ~ Int {
\t\treturn a + b;
\t}
}

class NoTools {
\tfunction : Add(a : Int, b : Int) ~ Int {
\t\treturn a + b;
\t}
}

class T {
\tfunction : Main(args : String[]) ~ Nil {
\t}
}

"""


def run(cmd, env, cwd, timeout=300):
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, timeout=timeout)
    return p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace")


def where(text, marker, start=0):
    """The 1-based (line, column) of the marker's first character at or after 'start'; a tab is one column, as the compiler counts."""
    at = text.index(marker, start)
    return text.count("\n", 0, at) + 1, at - (text.rfind("\n", 0, at) + 1) + 1


OKB = "\tbackend : ScriptedBackend->New(script);\n"

# what, the declaration (after HEAD), text the error must hold, and the text at whose first character the
# error must point
BAD = [
    # ---- the declaration itself ----
    ("an unknown setting", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\tmodel : \"claude\";\n}\n",
     ["Unknown setting 'model' in the agent 'A'", "backend, instructions, tools, mcp, allow, deny, budget and trace"], "model :"),
    ("a setting twice", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\tinstructions : \"a\";\n\tinstructions : \"b\";\n}\n",
     ["The agent 'A' has 'instructions' twice"], "instructions : \"b\""),
    ("no backend", "agent Quiet {\n\tinstructions : \"a\";\n}\n",
     ["The agent 'Quiet' has no backend", "backend : AnthropicBackend->New("], "Quiet {"),
    ("an empty declaration", "agent Empty {\n}\n",
     ["The agent 'Empty' has no backend"], "Empty {"),
    ("an empty list", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\ttools : ;\n}\n",
     ["Expected a value for an entry of 'tools' of the agent 'A'"], ";\n}\n"),
    ("a list that ends in a comma", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\ttools : Tools, ;\n}\n",
     ["Expected a value for an entry of 'tools'"], ";\n}\n"),
    ("a missing ';'", "agent A(script : Vector<AgentReply>) {\n\tbackend : ScriptedBackend->New(script)\n}\n",
     ["Expected ';' after 'backend' of the agent 'A', found '}'"], "}\n"),
    ("a missing ':'", "agent A(script : Vector<AgentReply>) {\n\tbackend ScriptedBackend->New(script);\n}\n",
     ["Expected ':' after 'backend', found identifier 'ScriptedBackend'"], "ScriptedBackend->New(script);\n}"),
    ("a setting that is not a name", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\t\"tools\" : Tools;\n}\n",
     ["Expected a setting of the agent 'A'", "each written 'name : value;'"], "tools\" : Tools"),
    ("a declaration that is not closed", "agent Open(script : Vector<AgentReply>) {\n" + OKB,
     ["The agent 'Open' is not closed: expected '}'"], "agent Open"),
    ("an unknown limit", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\tbudget { turns : 3; }\n}\n",
     ["Unknown limit 'turns' in the budget of the agent 'A'", "steps, tokens, tool_calls, model_calls, wall_ms, model_call_ms and cost"], "turns : 3"),
    ("a limit twice", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\tbudget { steps : 3; tokens : 9; steps : 4; }\n}\n",
     ["The budget of the agent 'A' has 'steps' twice"], "steps : 4"),
    ("an empty budget", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\tbudget { }\n}\n",
     ["The budget of the agent 'A' is empty"], "budget { }"),
    ("a budget without braces", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\tbudget : 8;\n}\n",
     ["Expected '{' after 'budget'", "budget { steps : 8; tokens : 5000; }"], ": 8;"),
    ("a limit with no ';'", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\tbudget { steps : 3 }\n}\n",
     ["Expected ';' after the limit 'steps', found '}'"], "}\n}\n"),
    ("a parameter the compiler's names could clash with", "agent A(agent__b : Vector<AgentReply>) {\n\tbackend : ScriptedBackend->New(agent__b);\n}\n",
     ["A parameter of an agent may not start with 'agent__'"], "agent__b : Vector"),
    ("one function as a tool", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\ttools : Tools->Add;\n}\n",
     ["A tool of an agent is a class, not one function", "'allow' or 'deny'"], "Tools->Add;"),
    ("a class with no '@tool' function as a tool", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\ttools : Tools, NoTools;\n}\n",
     ["The class 'NoTools', named in 'tools' of the agent 'A', has no '@tool' function"], "NoTools;"),
    # ---- the user's own text ----
    ("a backend that is not one", "agent A {\n\tbackend : \"not a backend\";\n}\n",
     ["Invalid cast between classes: 'API.Agent.ChatBackend' and 'System.String'"], "not a backend\";"),
    ("instructions that are a number", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\tinstructions : 42;\n}\n",
     ["Invalid cast with classes: System.String and Int"], "42;"),
    ("a tool that is a string", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\ttools : Tools, script->Size()->ToString();\n}\n",
     ["Invalid cast between classes: 'API.Agent.AgentTool' and 'System.String'"], "ToString();"),
    ("a class that does not exist", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\ttools : Tools, Missing;\n}\n",
     ["Undefined class: 'Missing'"], "Missing;"),
    ("an MCP server that is not one", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\tmcp : script;\n}\n",
     ["Invalid cast between classes: 'API.MCP.McpClient' and 'Collection.Vector'"], "script;\n}"),
    ("a tool name that is a number", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\tallow : \"add\", 7;\n}\n",
     ["Invalid cast with classes: System.String and Int"], "7;"),
    ("a limit that is a string", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\tbudget { tokens : 100; steps : \"eight\"; }\n}\n",
     ["Invalid cast with classes: System.Int and System.String"], "eight\";"),
    ("a trace that is a number", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\ttrace : script->Size();\n}\n",
     ["Invalid cast with classes: API.Agent.AgentTraceSink and Int"], "script->Size();"),
    ("an undefined name on the second line of a value", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\tinstructions : \"a\" +\n\t\t\tundefined_name;\n}\n",
     ["Undefined variable: 'undefined_name'"], "undefined_name;"),
    ("a parenthesis that is not closed", "agent A(script : Vector<AgentReply>) {\n\tbackend : ScriptedBackend->New(script;\n\tinstructions : \"a\";\n}\n",
     ["found ';'"], ";\n\tinstructions"),
    ("two values where one is expected", "agent A(script : Vector<AgentReply>, extra : String) {\n" + OKB + "\tinstructions : extra extra;\n}\n",
     ["found identifier 'extra'"], "extra;"),
    ("a parameter of an unknown type", "agent A(script : Vector<AgentReply>, n : Intt) {\n" + OKB + "}\n",
     ["Undefined class or enum: 'Intt'"], "n : Intt)"),
    ("a mistake in the second setting of a line that holds two", "agent A(script : Vector<AgentReply>) { backend : ScriptedBackend->New(script); instructions : 42; }\n",
     ["Invalid cast with classes: System.String and Int"], "42; }"),
    ("a mistake in the third entry of a list on one line", "agent A(script : Vector<AgentReply>) {\n" + OKB + "\ttools : Tools, AgentTools->Calculator(), Nope;\n}\n",
     ["Undefined class: 'Nope'"], "Nope;"),
]

# a declaration on the first line of a file, where there is no line above it for the compiler's own code
FIRST_LINE = ("agent A(n : Intt) { backend : API.Agent.ScriptedBackend->New(Nil); }\nclass T {\n\tfunction : Main(args : String[]) ~ Nil {\n\t}\n}\n",
              "Undefined class or enum: 'Intt'", "n : Intt)")

RUNTIME = [
    ("a tool that is Nil",
     "agent A(script : Vector<AgentReply>, extra : AgentTool) {\n" + OKB + "\ttools : extra;\n}\n"
     "class Go {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tA->Build(Nil, Nil);\n\t\t\"not reached\"->PrintLine();\n\t}\n}\n",
     "agent 'A': a tool is Nil"),
    ("an MCP server that is Nil",
     "agent A(script : Vector<AgentReply>, remote : McpClient) {\n" + OKB + "\tmcp : remote;\n}\n"
     "class Go {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tA->Build(Nil, Nil);\n\t\t\"not reached\"->PrintLine();\n\t}\n}\n",
     "agent 'A': an 'mcp' server is Nil"),
]

SCRIPT = ("\t\tscript := Vector->New()<AgentReply>;\n\t\tscript->AddBack(AgentReply->Ask(\"1\", \"add\", \"{\\\"a\\\":2,\\\"b\\\":3}\"));\n"
          "\t\tscript->AddBack(AgentReply->Say(\"done\"));\n")
SHOW = ("\t\teach(step in result->GetSteps()) {\n\t\t\teach(r in step->GetResults()) {\n\t\t\t\t\"{$r->GetToolName()}={$r->GetContent()}\"->PrintLine();\n\t\t\t};\n\t\t};\n"
        "\t\tresult->GetText()->PrintLine();\n")
TOOLS = "class Tools {\n\t#~\n\tAdd two integers.\n\t@tool\n\t~#\n\tfunction : Add(a : Int, b : Int) ~ Int {\n\t\treturn a + b;\n\t}\n}\n"

# what, {file: text}, what the program prints, libraries, CRLF
OK = [
    ("an agent in a named bundle, by its full name",
     {"main.obs": "use API.MCP, API.Agent, Collection;\n\nbundle Shop {\n" + TOOLS + "\tagent Clerk(script : Vector<AgentReply>) {\n\t" + OKB + "\t\ttools : Tools;\n\t}\n}\n\n"
                  "class Go {\n\tfunction : Main(args : String[]) ~ Nil {\n" + SCRIPT + "\t\tresult := Shop.Clerk->Build(script)->Run(\"go\");\n" + SHOW + "\t}\n}\n"},
     "add=5\ndone\n", LIBS, False),
    ("an agent in a named bundle, through 'use'",
     {"main.obs": "use API.MCP, API.Agent, Collection, Shop;\n\nbundle Shop {\n" + TOOLS + "\tagent Clerk(script : Vector<AgentReply>) {\n\t" + OKB + "\t\ttools : Shop.Tools;\n\t}\n}\n\n"
                  "bundle Default {\nclass Go {\n\tfunction : Main(args : String[]) ~ Nil {\n" + SCRIPT + "\t\tresult := Clerk->Build(script)->Run(\"go\");\n" + SHOW + "\t}\n}\n}\n"},
     "add=5\ndone\n", LIBS, False),
    ("an agent on the first line of a file",
     {"main.obs": "agent First { backend : API.Agent.ScriptedBackend->New(Nil)->Add(API.Agent.AgentReply->Say(\"first\")); }\n"
                  "class Go {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tFirst->Build()->Run(\"go\")->GetText()->PrintLine();\n\t}\n}\n"},
     "first\n", "gen_collect,json,agent,net,cipher,misc", False),
    ("a CRLF file",
     {"main.obs": "use API.MCP, API.Agent, Collection;\n\n" + TOOLS + "\nagent Clerk(script : Vector<AgentReply>) {\n" + OKB + "\tinstructions : \"two\n lines\";\n\ttools : Tools,\n\t\tAgentTools->Calculator();\n"
                  "\tbudget {\n\t\tsteps : 4;\n\t}\n}\n\nclass Go {\n\tfunction : Main(args : String[]) ~ Nil {\n" + SCRIPT + "\t\tresult := Clerk->Build(script)->Run(\"go\");\n" + SHOW + "\t}\n}\n"},
     "add=5\ndone\n", LIBS, True),
    ("an agent whose tools class is in another file",
     {"main.obs": "use API.MCP, API.Agent, Collection;\n\nagent Clerk(script : Vector<AgentReply>) {\n" + OKB + "\ttools : Tools;\n}\n\n"
                  "class Go {\n\tfunction : Main(args : String[]) ~ Nil {\n" + SCRIPT + "\t\tresult := Clerk->Build(script)->Run(\"go\");\n" + SHOW + "\t}\n}\n",
      "tools.obs": "use API.MCP;\n\n" + TOOLS},
     "add=5\ndone\n", LIBS, False),
    ("an agent that needs no MCP library",
     {"main.obs": "use API.Agent, Collection;\n\nagent Clerk(script : Vector<AgentReply>) {\n" + OKB + "\ttools : AgentTools->Calculator();\n\tbudget { steps : 2; }\n}\n\n"
                  "class Go {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tscript := Vector->New()<AgentReply>;\n\t\tscript->AddBack(AgentReply->Say(\"no mcp\"));\n"
                  "\t\tClerk->Build(script)->Run(\"go\")->GetText()->PrintLine();\n\t}\n}\n"},
     "no mcp\n", "gen_collect,json,agent,net,cipher,misc", False),
    ("'agent' as the name of a class, with a function called Build",
     {"main.obs": "class agent {\n\t@name : String;\n\tNew(name : String) {\n\t\t@name := name;\n\t}\n\tmethod : public : Name() ~ String {\n\t\treturn @name;\n\t}\n"
                  "\tfunction : Build(count : Int) ~ Int {\n\t\treturn count + 1;\n\t}\n}\n\n"
                  "class Go {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tmade := agent->New(\"named\");\n\t\tmade->Name()->PrintLine();\n\t\tagent->Build(1)->PrintLine();\n\t}\n}\n"},
     "named\n2\n", "", False),
    ("'agent' as a variable at the start of a statement, a function, a method, a parameter and an instance variable",
     {"main.obs": "class Go {\n\t@agent : String;\n\tNew(agent : String) {\n\t\t@agent := agent;\n\t}\n\tmethod : public : agent() ~ String {\n\t\treturn @agent;\n\t}\n"
                  "\tfunction : agent(agent : Int) ~ Int {\n\t\treturn agent * 2;\n\t}\n"
                  "\tfunction : Main(args : String[]) ~ Nil {\n\t\tagent := 4;\n\t\tagent += 1;\n\t\tagent->PrintLine();\n\t\tagent(agent)->PrintLine();\n"
                  "\t\tGo->New(\"field\")->agent()->PrintLine();\n\t\tbudget := agent;\n\t\ttools := budget + 1;\n\t\ttools->PrintLine();\n\t}\n}\n"},
     "5\n10\nfield\n6\n", "", False),
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
        line = "  [%s] %s%s" % ("PASS" if cond else "FAIL", name, "" if cond else "\n         " + str(detail)[-900:])
        print(line.encode("ascii", "replace").decode("ascii"))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="agent-decl-") as work:
        def compile_files(name, files, crlf=False, libs=LIBS):
            sources = []
            for file_name, text in files.items():
                src = os.path.join(work, name + "_" + file_name)
                with open(src, "wb") as f:
                    f.write(text.replace("\n", "\r\n").encode("utf-8") if crlf else text.encode("utf-8"))
                sources.append(src)
            obe = os.path.join(work, name + ".obe")
            if os.path.exists(obe):
                os.remove(obe)
            cmd = [obc, "-src", ",".join(sources), "-dest", obe]
            if libs:
                cmd += ["-lib", libs]
            code, out = run(cmd, env, work)
            return code, out, obe

        def diagnostic(tag, label, text, needles, marker, crlf, start=0):
            line, col = where(text, marker, start)
            code, out, obe = compile_files(tag, {"main.obs": text}, crlf=crlf)
            ok = code != 0 and not os.path.exists(obe) and all(n in out for n in needles)
            check("diagnostic: %s" % label, ok, out)
            at = [l for l in out.splitlines() if needles[0] in l]
            check("  at line %d, column %d" % (line, col), bool(at) and ":(%d,%d):" % (line, col) in at[0], out)

        for i, (what, body, needles, marker) in enumerate(BAD):
            for crlf in (False, True):
                diagnostic("bad%d%s" % (i, "c" if crlf else ""), "%s%s" % (what, " (CRLF)" if crlf else ""), HEAD + body, needles, marker, crlf, len(HEAD))

        text, needle, marker = FIRST_LINE
        diagnostic("first", "a parameter of an unknown type, in a declaration on the first line of a file", text, [needle], marker, False)

        # an error inside a declaration leaves the rest of the file checked as before: one error, not a flood
        code, out, obe = compile_files("one", {"main.obs": HEAD + "agent A(script : Vector<AgentReply>) {\n" + OKB + "\tmodel : \"x\";\n}\n"})
        errors = [l for l in out.splitlines() if ".obs:(" in l and "Warning:" not in l]
        check("one mistake in a declaration is one error", len(errors) == 1, out)

        for i, (what, body, message) in enumerate(RUNTIME):
            code, out, obe = compile_files("run%d" % i, {"main.obs": HEAD.replace("class T {\n\tfunction : Main(args : String[]) ~ Nil {\n\t}\n}\n", "") + body})
            if code != 0:
                check("at run time: %s" % what, False, out)
                continue
            code, out = run([obr, obe], env, work)
            check("at run time: %s stops the program with a message" % what, code != 0 and message in out and "not reached" not in out, out)

        for i, (what, files, expect, libs, crlf) in enumerate(OK):
            code, out, obe = compile_files("ok%d" % i, files, crlf=crlf, libs=libs)
            if code != 0:
                check("works: %s" % what, False, out)
                continue
            code, out = run([obr, obe], env, work)
            check("works: %s" % what, code == 0 and expect in out.replace("\r\n", "\n"), out)

        # the agent library is needed by a declaration and only by a declaration
        code, out, obe = compile_files("plain", {"main.obs": "class T {\n\tfunction : Main(args : String[]) ~ Nil { \"x\"->PrintLine(); }\n}\n"}, libs="")
        check("a program with no declaration needs no agent library", code == 0, out)
        code, out, obe = compile_files("nolib", {"main.obs": "agent A { backend : Nil; }\nclass T {\n\tfunction : Main(args : String[]) ~ Nil {}\n}\n"}, libs="gen_collect")
        check("a declaration without the agent library is an error that names its classes", code != 0 and not os.path.exists(obe) and "API.Agent." in out, out)

        # a mistake that ends the reading of a setting early does not flood the rest of the file with errors
        code, out, obe = compile_files("few", {"main.obs": HEAD + "agent A(script : Vector<AgentReply>) {\n" + OKB + "\tbudget : 8;\n\tinstructions : \"a\";\n}\n"})
        errors = [l for l in out.splitlines() if ".obs:(" in l and "Warning:" not in l]
        check("a budget without braces is one error", len(errors) == 1, out)

        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "agent_decl_test.obs"), encoding="utf-8") as f:
            source = f.read()
        accepted, refused, findings = syntax_mutants.smoke(bin_dir, [source], LIBS + ",llm_util,regex", work, 300, 20261126)
        check("300 damaged programs: the compiler answers every one (%d accepted, %d refused)" % (accepted, refused),
              not findings and accepted + refused == 300, findings)

    if failures:
        print("FAIL: %d check(s) failed" % len(failures))
        return 1
    print("PASS: agent declaration diagnostics")
    return 0


if __name__ == "__main__":
    sys.exit(main())
