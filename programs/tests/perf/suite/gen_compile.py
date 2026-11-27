#!/usr/bin/env python3
"""
gen_compile.py <out_dir>: pathological-shape sources for obc compile-time measurement.

Every shape comes at two or three sizes, so a compile time that grows faster than the source does is seen
as a rising time per unit; perf_run.py's compile table prints them side by side. Shapes:

  nesting       parentheses, blocks, calls, array subscripts, unary operators, generic types at 100 and 250
                levels (the shapes of programs/regression/check_nesting_limit.py); obc compiles or refuses
  chains        a + a + ..., "s" + "s" + ..., a->M()->M()..., else-if ladders, select labels
  breadth       many classes, many methods in one class, many statements in one method, many locals
  literals      one very long string literal, a long Int array literal, a long String array literal
  interpolation many interpolated strings, and one string with many interpolations

generate(out_dir) returns [(name, path, libs, expect_ok)].
"""
import os, sys


def program(body, extra_classes="", uses="use Collection;\n\n"):
    return (uses + extra_classes + "class Gen {\n\tfunction : Id(v : Int) ~ Int {\n\t\treturn v;\n\t}\n\n"
            "\tfunction : Main(args : String[]) ~ Nil {\n\t\t" + body + "\n\t}\n}\n")


def nesting():
    def parentheses(n):
        return "x := " + "(" * n + "1" + ")" * n + ";\n\t\tx->PrintLine();"

    def blocks(n):
        return "x := 0;\n\t\t" + "if(x = 0) { " * n + "x += 1; " + "}; " * n + "\n\t\tx->PrintLine();"

    def calls(n):
        return "x := " + "Id(" * n + "1" + ")" * n + ";\n\t\tx->PrintLine();"

    def subscripts(n):
        return "a := Int->New[2];\n\t\tx := " + "a[" * n + "0" + "]" * n + ";\n\t\tx->PrintLine();"

    def unary(n):
        return "x := " + "-(" * n + "1" + ")" * n + ";\n\t\tx->PrintLine();"

    def generics(n):
        return "v : " + "Vector<" * n + "IntRef" + ">" * n + ";\n\t\t(v = Nil)->PrintLine();"

    out = []
    # subscripts are measured at 12, 16 and 20 levels too: nested subscripts were exponential (fixed on
    # fix/nested-subscript-exponential), so 100 levels never answers on a tree without that fix
    for name, build, levels in (("nest_parens", parentheses, (100, 250)), ("nest_blocks", blocks, (100, 250)),
                                ("nest_calls", calls, (100, 250)), ("nest_subscripts", subscripts, (12, 16, 20, 100, 250)),
                                ("nest_unary", unary, (100, 250)), ("nest_generics", generics, (100, 250))):
        for n in levels:
            out.append(("%s@%d" % (name, n), program(build(n)), "gen_collect", None))
    return out


def chains():
    out = []
    # obc refuses more than 2000 chained operators in one statement
    for n in (450, 1800):
        out.append(("chain_add@%d" % n, program("a := 1;\n\t\tx := " + " + ".join(["a"] * n) + ";\n\t\tx->PrintLine();"), "", True))
    for n in (250, 1000):
        out.append(("chain_strcat@%d" % n, program("x := " + " + ".join(['"s"'] * n) + ";\n\t\tx->Size()->PrintLine();"), "", True))
        out.append(("chain_strcat_var@%d" % n, program("s := \"s\";\n\t\tx := s" + " + s" * n + ";\n\t\tx->Size()->PrintLine();"), "", True))
    for n in (60, 120, 240):
        out.append(("chain_method@%d" % n, program("s := \" x \";\n\t\tx := s" + "->Trim()" * n + ";\n\t\tx->PrintLine();"), "", True))
    for n in (250, 1000):
        ladder = "x := args->Size();\n\t\ty := 0;\n\t\tif(x = -1) { y := 0; }"
        ladder += "".join("\n\t\telse if(x = %d) { y := %d; }" % (i, i) for i in range(n)) + ";\n\t\ty->PrintLine();"
        out.append(("chain_elseif@%d" % n, program(ladder), "", None))
    for n in (500, 2000):
        sel = "x := args->Size();\n\t\ty := 0;\n\t\tselect(x) {" + "".join("\n\t\t\tlabel %d: { y := %d; }" % (i, i + 1) for i in range(n))
        sel += "\n\t\t};\n\t\ty->PrintLine();"
        out.append(("chain_select@%d" % n, program(sel), "", True))
    for n in (400, 1600):
        out.append(("chain_logic@%d" % n, program("a := true;\n\t\tx := " + " & ".join(["a"] * n) + ";\n\t\tx->PrintLine();"), "", None))
    return out


def breadth():
    out = []
    for n in (500, 2000):
        classes = "".join("class C%d {\n\t@v : Int;\n\tNew(v : Int) { @v := v; }\n\tmethod : public : Get() ~ Int { return @v + %d; }\n"
                          "\tmethod : public : Next() ~ C%d { return C%d->New(@v); }\n}\n\n" % (i, i, (i + 1) % n, (i + 1) % n) for i in range(n))
        out.append(("many_classes@%d" % n, program("C0->New(1)->Next()->Get()->PrintLine();", classes), "", True))
    for n in (2000, 8000):
        methods = "class M {\n" + "".join("\tfunction : F%d(a : Int) ~ Int { return a + %d; }\n" % (i, i) for i in range(n)) + "}\n\n"
        out.append(("many_methods@%d" % n, program("M->F1(M->F0(1))->PrintLine();", methods), "", True))
    for n in (2000, 8000):
        # twelve overloads of one name: one to six Int parameters, one to six Float parameters
        methods = "class O {\n"
        for count in range(1, 7):
            for kind in ("Int", "Float"):
                methods += "\tfunction : F(%s) ~ Int { return %d; }\n" % (", ".join("a%d : %s" % (j, kind) for j in range(count)), count)
        methods += "}\n\n"
        body = "x := 0;\n\t\t" + "\n\t\t".join("x += O->F(%s);" % ", ".join(["1.5" if i % 2 else "1"] * (1 + i % 6)) for i in range(n)) + "\n\t\tx->PrintLine();"
        out.append(("many_overload_calls@%d" % n, program(body, methods), "", True))
    for n in (5000, 20000):
        out.append(("many_statements@%d" % n, program("x := 0;\n\t\t" + "\n\t\t".join("x += %d;" % i for i in range(n)) + "\n\t\tx->PrintLine();"), "", True))
    # a method's locals are bounded (LOCAL_SIZE), so breadth in locals is many methods of 40 locals each
    for n in (250, 1000):
        methods = "class L {\n" + "".join("\tfunction : F%d() ~ Int {\n\t\t%s\n\t\treturn %s;\n\t}\n" % (
            i, " ".join("v%d := %d;" % (j, j + i) for j in range(40)), " + ".join("v%d" % j for j in range(40))) for i in range(n)) + "}\n\n"
        out.append(("many_locals@%d" % (n * 40), program("L->F0()->PrintLine();", methods), "", True))
    for n in (1000, 4000):
        body = "v := Vector->New()<String>;\n\t\t" + "\n\t\t".join('v->AddBack("s%d");' % i for i in range(n)) + "\n\t\tv->Size()->PrintLine();"
        out.append(("many_generic_calls@%d" % n, program(body), "gen_collect", True))
    for n in (100, 400):
        methods = "class Lm {\n" + "".join("\tfunction : F%d() ~ Int {\n\t\tx := 0;\n\t\t%s\n\t\treturn x;\n\t}\n" % (
            i, " ".join("f%d := \(Int) ~ Int : (a) => a + %d; x += f%d(1);" % (j, j, j) for j in range(5))) for i in range(n)) + "}\n\n"
        out.append(("many_lambdas@%d" % (n * 5), program("Lm->F0()->PrintLine();", methods), "", None))
    return out


def literals():
    out = []
    for n in (100000, 1000000):
        out.append(("lit_string@%d" % n, program('x := "' + "abcdefghij" * (n // 10) + '";\n\t\tx->Size()->PrintLine();'), "", True))
    for n in (5000, 10000, 20000, 40000):
        out.append(("lit_int_array@%d" % n, program("x := [" + ", ".join(str(i) for i in range(n)) + "];\n\t\tx->Size()->PrintLine();"), "", True))
    for n in (2500, 10000):
        out.append(("lit_str_array@%d" % n, program("x := [" + ", ".join('"s%d"' % i for i in range(n)) + "];\n\t\tx->Size()->PrintLine();"), "", True))
    for n in (2500, 10000):
        out.append(("lit_strings@%d" % n, program("x := 0;\n\t\t" + "\n\t\t".join('x += "string number %d"->Size();' % i for i in range(n)) + "\n\t\tx->PrintLine();"), "", True))
    return out


def interpolation():
    out = []
    # an interpolated string takes hidden locals and a method's locals are bounded, so many of them is many methods
    for n in (100, 400):
        methods = "class Ip {\n" + "".join("\tfunction : F%d(a : Int, b : String) ~ Int {\n\t\tx := 0;\n\t\t%s\n\t\treturn x;\n\t}\n" % (
            i, " ".join('x += "n%d a={$a} b={$b}"->Size();' % j for j in range(20))) for i in range(n)) + "}\n\n"
        out.append(("interp_many@%d" % (n * 20), program('Ip->F0(1, "two")->PrintLine();', methods), "", True))
    # one method with more interpolated strings than its locals allow: the refusal itself should be prompt
    for n in (1000, 2000, 4000):
        body = "a := 1; b := \"two\";\n\t\tx := 0;\n\t\t" + "\n\t\t".join('x += "n%d a={$a} b={$b}"->Size();' % i for i in range(n)) + "\n\t\tx->PrintLine();"
        out.append(("interp_over_limit@%d" % n, program(body), "", False))
    for n in (100, 400, 1600):
        out.append(("interp_one_string@%d" % n, program('a := 1;\n\t\tx := "' + "v={$a};" * n + '";\n\t\tx->Size()->PrintLine();'), "", True))
    return out


def generate(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    cases = []
    for name, text, libs, expect_ok in nesting() + chains() + breadth() + literals() + interpolation():
        path = os.path.join(out_dir, name.replace("@", "_") + ".obs")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        cases.append((name, path, libs, expect_ok))
    return cases


if __name__ == "__main__":
    for case in generate(sys.argv[1]):
        print("%-28s %8d bytes" % (case[0], os.path.getsize(case[1])))
