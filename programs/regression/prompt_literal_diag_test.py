#!/usr/bin/env python3
"""
Prompt literals (docs/design/AGENT_SYNTAX.md, section 2): what the compiler says about a hole it cannot
fill, where it says it, and what it must leave alone.

Usage: prompt_literal_diag_test.py <bin_dir>

<bin_dir> holds obc and obr; the libraries are read from the sibling lib/ directory. No network is used.

Diagnostics (each source is compiled; obc must fail, write no program, name the problem and give the line and
column of the hole, also for a hole on a later line of a literal that runs over several lines, in an LF and in a
CRLF file):
  a hole that names no variable in scope; a hole declared with a type its variable does not have (each pair that
  could be confused: Int/Float, Int/String, Char/String, Byte/Int, Bool/Int); a hole whose variable is an object,
  an array or a function; an empty hole; a hole that is an expression; a hole with an unknown type; a hole that is
  not closed; a '}' that closes nothing; a '{$' interpolation that is not closed; a '$"..."' string; several bad
  holes in one literal (every one is reported); a program without the llm_util library.

Left alone (each source must compile and, run, print what it should):
  'prompt' as a local, a parameter, an instance variable, a function, a method, a class, a label of a select and
  an enum item; a string on the line after a statement that ends in 'prompt'; the word in a comment and in a
  string; a program that has no prompt literal compiles without the llm_util library.
"""
import os, subprocess, sys, tempfile

EXE = ".exe" if os.name == "nt" else ""
LIBS = "llm_util,regex,json,gen_collect"


def run(cmd, env, cwd, timeout=300):
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, timeout=timeout)
    return p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace")


def program(body, extra=""):
    """A class whose Main holds the given statements, each line indented by two tabs."""
    return ("use Collection;\n\nclass Thing {\n\tNew() {}\n}\n\nclass T {\n\tfunction : Main(args : String[]) ~ Nil {\n" +
            "".join("\t\t" + line + "\n" for line in body.split("\n")) + "\t}\n" + extra + "}\n")


def where(text, marker):
    """The 1-based (line, column) of the marker's first character; a tab is one column, as the compiler counts."""
    at = text.index(marker)
    return text.count("\n", 0, at) + 1, at - (text.rfind("\n", 0, at) + 1) + 1


# what, statements, text the error must hold, and the text in the program at whose first character the error
# must point: a hole's '{' for a hole that is wrong, the name for a variable that does not exist
BAD = [
    ("a hole that names no variable", 'text := "a";\np := prompt "Summarise {txt} now";\np->ToString()->PrintLine();',
     ["Undefined variable: 'txt'", "Did you mean 'text'?"], 'txt} now'),
    ("a hole that names no variable, on the third line of a literal",
     'text := "a";\np := prompt "first {text}\nsecond\n  third {nope} end";\np->ToString()->PrintLine();',
     ["Undefined variable: 'nope'"], 'nope} end'),
    ("an Int declared for a Float", 'ratio := 0.5;\np := prompt "r {ratio : Int}";\np->ToString()->PrintLine();',
     ["'{ratio : Int}' declares Int, but 'ratio' is Float"], '{ratio : Int}'),
    ("a Float declared for an Int", 'count := 3;\np := prompt "{count : Float}";\np->ToString()->PrintLine();',
     ["'{count : Float}' declares Float, but 'count' is Int"], '{count : Float}'),
    ("a String declared for an Int", 'count := 3;\np := prompt "n {count : String}";\np->ToString()->PrintLine();',
     ["'{count : String}' declares String, but 'count' is Int"], '{count : String}'),
    ("an Int declared for a String", 'text := "a";\np := prompt "{text : Int}";\np->ToString()->PrintLine();',
     ["'{text : Int}' declares Int, but 'text' is String"], '{text : Int}'),
    ("a String declared for a Char", "mark := 'x';\np := prompt \"{mark : String}\";\np->ToString()->PrintLine();",
     ["'{mark : String}' declares String, but 'mark' is Char"], '{mark : String}'),
    ("an Int declared for a Byte", 'small : Byte := 7;\np := prompt "{small : Int}";\np->ToString()->PrintLine();',
     ["'{small : Int}' declares Int, but 'small' is Byte"], '{small : Int}'),
    ("an Int declared for a Bool", 'ok := true;\np := prompt "{ok : Int}";\np->ToString()->PrintLine();',
     ["'{ok : Int}' declares Int, but 'ok' is Bool"], '{ok : Int}'),
    ("a hole whose variable is an object", 'thing := Thing->New();\np := prompt "see {thing}";\np->ToString()->PrintLine();',
     ["The prompt hole '{thing}' cannot take 'thing', which is Thing", "a hole takes a String, Int, Float, Bool, Char or Byte"], '{thing}'),
    ("a hole whose variable is a collection", 'list := Vector->New()<String>;\np := prompt "{list}";\np->ToString()->PrintLine();',
     ["The prompt hole '{list}' cannot take 'list'", "Put the text to send in a String variable first"], '{list}'),
    ("a hole whose variable is an array", 'words := ["a", "b"];\np := prompt "{words}";\np->ToString()->PrintLine();',
     ["The prompt hole '{words}' cannot take 'words', which is String[]"], '{words}'),
    ("a hole declared String whose variable is an array", 'words := ["a", "b"];\np := prompt "{words : String}";\np->ToString()->PrintLine();',
     ["cannot take 'words', which is String[]"], '{words : String}'),
    ("a hole whose variable is a function", 'f := \\() ~ Int : () => 1;\np := prompt "{f}";\np->ToString()->PrintLine();',
     ["The prompt hole '{f}' cannot take 'f'"], '{f}'),
    ("an empty hole", 'p := prompt "a {} b";\np->ToString()->PrintLine();',
     ["An empty prompt hole '{}' names no variable", "write {{ and }} for literal braces"], '{} b'),
    ("a hole of spaces", 'p := prompt "a {  } b";\np->ToString()->PrintLine();',
     ["An empty prompt hole"], '{  } b'),
    ("a hole that is an expression", 'n := 1;\np := prompt "{n + 1}";\np->ToString()->PrintLine();',
     ["'{n + 1}' is not a prompt hole", "a hole names one variable"], '{n + 1}'),
    ("a hole that is a call", 'text := "a";\np := prompt "{text->Size()}";\np->ToString()->PrintLine();',
     ["'{text->Size()}' is not a prompt hole"], '{text->Size()}'),
    ("a hole of two words", 'p := prompt "to {user name}";\np->ToString()->PrintLine();',
     ["'{user name}' is not a prompt hole"], '{user name}'),
    ("a hole with an unknown type", 'n := 1;\np := prompt "{n : Number}";\np->ToString()->PrintLine();',
     ["Unknown type 'Number' in the prompt hole '{n : Number}'", "String, Int, Float, Bool, Char or Byte"], '{n : Number}'),
    ("a hole with a missing type", 'n := 1;\np := prompt "{n : }";\np->ToString()->PrintLine();',
     ["Unknown type '' in the prompt hole"], '{n : }'),
    ("a hole that is not closed", 'n := 1;\np := prompt "a {n b";\np->ToString()->PrintLine();',
     ["A prompt hole is not closed", "write {{ for a literal '{'"], '{n b'),
    ("a hole left open at the end of a line", 'n := 1;\np := prompt "a {n\n b}";\np->ToString()->PrintLine();',
     ["A prompt hole is not closed"], '{n\n'),
    ("a closing brace alone", 'p := prompt "a } b";\np->ToString()->PrintLine();',
     ["A '}' in a prompt literal closes no hole", "Write }} for a literal '}'"], '} b'),
    ("an unbalanced object", 'p := prompt "{{\\"a\\": 1}";\np->ToString()->PrintLine();',
     ["A '}' in a prompt literal closes no hole"], '}";'),
    ("an interpolation that is not closed", 'n := 1;\np := prompt "a {$n b";\np->ToString()->PrintLine();',
     ["A '{$' interpolation in a prompt literal is not closed"], 'a {$n b'),
    ("a raw string", 'p := prompt $"a {n}";\np->ToString()->PrintLine();',
     ["A prompt literal takes an ordinary string"], 'prompt $'),
    ("an unknown variable in an interpolation beside a hole", 'n := 1;\np := prompt "{n} {$nope}";\np->ToString()->PrintLine();',
     ["Undefined variable: 'nope'"], ' {$nope}'),
]

# several bad holes in one literal: every one is reported, each at its own place
MANY = ('text := "a";\ncount := 3;\nratio := 0.5;\np := prompt "one {missing}\n two {count : String}\n three {ratio : Int} {text}\n four {gone} {count}";\n'
        'p->ToString()->PrintLine();')
MANY_EXPECT = [("Undefined variable: 'missing'", "missing}"), ("declares String, but 'count' is Int", "{count : String}"),
               ("declares Int, but 'ratio' is Float", "{ratio : Int}"), ("Undefined variable: 'gone'", "gone}")]
# the same for mistakes in how holes are written, which are found before any name is looked up
SHAPES = 'n := 1;\np := prompt "one {}\n two {n : Number} {n}\n three } and {n + 1}\n four {n";\np->ToString()->PrintLine();'
SHAPES_EXPECT = [("An empty prompt hole", "{}\n\t\t two"), ("Unknown type 'Number'", "{n : Number}"), ("closes no hole", "} and"), ("'{n + 1}' is not a prompt hole", "{n + 1}"),
                 ("A prompt hole is not closed", "{n\";")]

OK = [
    ("a local, a parameter and an instance variable called prompt",
     'class T {\n\t@prompt : String;\n\tNew(prompt : String) {\n\t\t@prompt := prompt;\n\t}\n\tmethod : public : Get() ~ String {\n\t\treturn @prompt;\n\t}\n'
     '\tfunction : Twice(prompt : String) ~ String {\n\t\treturn prompt + prompt;\n\t}\n'
     '\tfunction : Main(args : String[]) ~ Nil {\n\t\tprompt := "ab";\n\t\tprompt->PrintLine();\n\t\tTwice(prompt)->PrintLine();\n\t\tT->New("field")->Get()->PrintLine();\n\t}\n}\n',
     "ab\nabab\nfield\n", ""),
    ("a function and a method called prompt",
     'class T {\n\tNew() {}\n\tfunction : prompt(text : String) ~ String {\n\t\treturn "f:" + text;\n\t}\n\tmethod : public : prompt() ~ String {\n\t\treturn "m";\n\t}\n'
     '\tfunction : Main(args : String[]) ~ Nil {\n\t\tprompt("x")->PrintLine();\n\t\tT->prompt("y")->PrintLine();\n\t\tT->New()->prompt()->PrintLine();\n\t}\n}\n',
     "f:x\nf:y\nm\n", ""),
    ("a class and an enum item called prompt",
     'enum Kind {\n\tprompt,\n\tanswer\n}\nclass prompt {\n\tNew() {}\n\tmethod : public : Name() ~ String {\n\t\treturn "class";\n\t}\n}\n'
     'class T {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tp := prompt->New();\n\t\tp->Name()->PrintLine();\n\t\tk := Kind->prompt;\n\t\tif(k = Kind->prompt) {\n\t\t\t"enum"->PrintLine();\n\t\t};\n\t}\n}\n',
     "class\nenum\n", ""),
    ("a string on the line after a statement that ends in prompt",
     'class T {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tprompt := "one";\n\t\tcopy := prompt;\n\t\t"two"->PrintLine();\n\t\tcopy->PrintLine();\n'
     '\t\tvalues := [prompt, "three"];\n\t\tvalues[1]->PrintLine();\n\t\tjoined := prompt + "four";\n\t\tjoined->PrintLine();\n\t}\n}\n',
     "two\none\nthree\nonefour\n", ""),
    ("the word in a comment and in a string",
     'class T {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\t# prompt "not {code}"\n\t\t"prompt \\"{text}\\" is text"->PrintLine();\n\t}\n}\n',
     'prompt "{text}" is text\n', ""),
    ("a prompt literal in a CRLF file",
     'class T {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\ttopic := "tides";\n\t\tcount := 2;\n\t\tp := prompt "About {topic}\nin {count : Int} lines";\n'
     '\t\tp->ToString()->Size()->PrintLine();\n\t\tp->Get("topic")->PrintLine();\n\t}\n}\n',
     "tides\n", LIBS),
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

    with tempfile.TemporaryDirectory(prefix="prompt-lit-") as work:
        def compile_text(name, text, crlf=False, libs=LIBS):
            src = os.path.join(work, name + ".obs")
            with open(src, "wb") as f:
                f.write(text.replace("\n", "\r\n").encode("utf-8") if crlf else text.encode("utf-8"))
            obe = os.path.join(work, name + ".obe")
            if os.path.exists(obe):
                os.remove(obe)
            cmd = [obc, "-src", src, "-dest", obe]
            if libs:
                cmd += ["-lib", libs]
            code, out = run(cmd, env, work)
            return code, out, obe

        for i, (what, body, needles, marker) in enumerate(BAD):
            line, col = where(program(body), marker)
            for crlf in (False, True):
                label = "%s%s" % (what, " (CRLF)" if crlf else "")
                code, out, obe = compile_text("bad%d%s" % (i, "c" if crlf else ""), program(body), crlf=crlf)
                ok = code != 0 and not os.path.exists(obe) and all(n in out for n in needles)
                check("diagnostic: %s" % label, ok, out)
                at = [l for l in out.splitlines() if needles[0] in l]
                check("  at line %d, column %d" % (line, col), bool(at) and ":(%d,%d):" % (line, col) in at[0], out)

        for name, body, expect in (("many", MANY, MANY_EXPECT), ("shapes", SHAPES, SHAPES_EXPECT)):
            code, out, obe = compile_text(name, program(body))
            check("several bad holes (%s): the compile fails and writes no program" % name, code != 0 and not os.path.exists(obe), out)
            for needle, marker in expect:
                line, col = where(program(body), marker)
                at = [l for l in out.splitlines() if needle in l]
                check("  every hole is reported: %s at (%d,%d)" % (needle, line, col), len(at) == 1 and ":(%d,%d):" % (line, col) in at[0], out)
            errors = [l for l in out.splitlines() if ".obs:(" in l and "Warning:" not in l]
            check("  and nothing else is: %d errors" % len(expect), len(errors) == len(expect), out)

        # the library a prompt literal is made with
        code, out, obe = compile_text("nolib", program('n := 1;\np := prompt "{n}";\np->ToString()->PrintLine();'), libs="gen_collect")
        check("a prompt literal without the llm_util library is an error that names the library",
              code != 0 and not os.path.exists(obe) and "Undefined class: 'API.LlmUtil.Prompt'" in out and "-lib llm_util,regex,json,gen_collect" in out, out)

        for i, (what, text, expect, libs) in enumerate(OK):
            crlf = "CRLF" in what
            code, out, obe = compile_text("ok%d" % i, text, crlf=crlf, libs=libs)
            if code != 0:
                check("left alone: %s" % what, False, out)
                continue
            code, out = run([obr, obe], env, work)
            check("left alone: %s" % what, code == 0 and expect in out.replace("\r\n", "\n"), out)

    if failures:
        print("FAIL: %d check(s) failed" % len(failures))
        return 1
    print("PASS: prompt literal diagnostics")
    return 0


if __name__ == "__main__":
    sys.exit(main())
