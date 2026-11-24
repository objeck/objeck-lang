# Writes opt_arith_identities.obs: python3 gen_opt_arith_identities.py opt_arith_identities.obs
import sys
MIN = -9223372036854775808
MAX = 9223372036854775807

def lit(v):
    if v == MIN:
        return "(-9223372036854775807 - 1)"
    return "(%d)" % v if v < 0 else str(v)

xs = [MIN, MIN + 1, -4611686018427387905, -4294967297, -65537, -1025, -9, -8, -7, -3, -2, -1, 0, 1, 2, 3, 7, 8, 9,
      1023, 65535, 4294967295, 4611686018427387904, MAX - 1, MAX]
ys = [5, -3, MAX, MIN, 0, 1, -1, 7, 1024, -65536, 3, 2, -2, 255, 9, 11, -11, 13, 17, 1 << 40, -(1 << 40), 6, 4, 100, -100]
assert len(xs) == len(ys)

consts = []
def K(v):
    if v not in consts:
        consts.append(v)
    return consts.index(v)

lines = []
def stmt(label, build):
    # build(lit_fn) -> expression text; the reference reads each literal from the
    # constants array, where the compiler cannot see it
    opt = build(lambda v: lit(v))
    ref = build(lambda v: "K[%d]" % K(v))
    lines.append('      failures += Chk("%s", x, y, %s, %s);' % (label.replace('"', "'"), opt, ref))

ops = [("+", "+"), ("-", "-"), ("*", "*"), ("and", "and"), ("or", "or"), ("xor", "xor")]
vals = {
    "+": [0, 1, -1, 7, MAX, MIN],
    "-": [0, 1, -1, 7, MAX, MIN],
    "*": [0, 1, 2, 3, 4, 5, 8, 16, 1024, 65536, 1048576, 4294967296, 1 << 62, MAX, -1, -2, -8, MIN],
    "and": [-1, 0, 255, MAX, MIN, 1],
    "or": [0, 1, -1, 255, MIN],
    "xor": [0, 1, -1, MAX, MIN],
}
for o, _ in ops:
    for v in vals[o]:
        t = "x %s %s" % (o, lit(v))
        stmt(t, lambda L, o=o, v=v: "(x %s %s)" % (o, L(v)))
        if o != "-":
            stmt("L %s x" % o, lambda L, o=o, v=v: "(%s %s x)" % (L(v), o))
        stmt("(x + y) %s L" % o, lambda L, o=o, v=v: "((x + y) %s %s)" % (o, L(v)))
        if o == "*" or o == "and":
            stmt("(x xor y) %s L" % o, lambda L, o=o, v=v: "((x xor y) %s %s)" % (o, L(v)))

for sh in ["<<", ">>"]:
    for v in [0, 1, 7, 31, 32, 63, 64, 65, 127, 128, 129]:
        stmt("x %s %d" % (sh, v), lambda L, sh=sh, v=v: "(x %s %s)" % (sh, L(v)))
        stmt("(x + y) %s %d" % (sh, v), lambda L, sh=sh, v=v: "((x + y) %s %s)" % (sh, L(v)))

chains = {
    "+": [(3, 4), (MAX, 1), (-1, 1), (MIN, MIN), (0, 5), (5, 0)],
    "*": [(2, 3), (2, 4), (3, 5), (4, 4), (MAX, 2), (1024, 1024), (65536, 65536), (65536, 4294967296), (-1, 2), (2, 0), (1, 8), (8, 1)],
    "and": [(255, 15), (-1, 7), (MIN, MAX), (6, 3)],
    "or": [(1, 4), (0, 0), (MIN, 1)],
    "xor": [(5, 5), (MAX, -1), (1, 2)],
}
for o, lst in chains.items():
    for a, b in lst:
        stmt("(x %s a) %s b" % (o, o), lambda L, o=o, a=a, b=b: "((x %s %s) %s %s)" % (o, L(a), o, L(b)))
        stmt("(a %s x) %s b" % (o, o), lambda L, o=o, a=a, b=b: "((%s %s x) %s %s)" % (L(a), o, o, L(b)))
        stmt("b %s (x %s a)" % (o, o), lambda L, o=o, a=a, b=b: "(%s %s (x %s %s))" % (L(b), o, o, L(a)))
        stmt("((x + y) %s a) %s b" % (o, o), lambda L, o=o, a=a, b=b: "(((x + y) %s %s) %s %s)" % (o, L(a), o, L(b)))

# division and remainder are not rewritten; kept here so a future rewrite of them is checked
for v in [1, 2, 4, 8, 1024, 65536, 3, 7, -1, -2, -4, 4611686018427387904, MAX, MIN]:
    stmt("x / %d" % v, lambda L, v=v: "(x / %s)" % L(v))
    stmt("x %% %d" % v, lambda L, v=v: "(x %% %s)" % L(v))
    stmt("(x + y) / %d" % v, lambda L, v=v: "((x + y) / %s)" % L(v))

# mixed shapes where a multiply by a power of two feeds other operators
stmt("(x * 8) + (y * 4)", lambda L: "((x * %s) + (y * %s))" % (L(8), L(4)))
stmt("(x * 2) * (y * 2)", lambda L: "((x * %s) * (y * %s))" % (L(2), L(2)))
stmt("x * 2 + 0", lambda L: "((x * %s) + %s)" % (L(2), L(0)))
stmt("((x * 4) + 1) * 8", lambda L: "(((x * %s) + %s) * %s)" % (L(4), L(1), L(8)))
stmt("(x - 0) * 1 + (0 + y)", lambda L: "(((x - %s) * %s) + (%s + y))" % (L(0), L(1), L(0)))
stmt("x * y * 2", lambda L: "((x * y) * %s)" % L(2))
stmt("2 * x * y", lambda L: "((%s * x) * y)" % L(2))
stmt("(x and 255) * 256", lambda L: "((x and %s) * %s)" % (L(255), L(256)))

hdr = '''#~
Integer identities and multiply-by-a-power-of-two (-opt s2 and s3), checked at the edges.

The compiler rewrites, on Int operands (arithmetic wraps, shift counts are masked):
  x + 0, x - 0, x * 1, x or 0, x xor 0, x and -1, x << 0 and x >> 0 (also << 64)  to x
  x * 0 and x and 0                                                               to 0
  x * 2^k                                                                         to x << k
  (x + a) + b, (x * a) * b, and the and / or / xor chains                         to one literal

Each line below evaluates one such expression on literals (which the compiler
rewrites) and the same expression with the literals read back from an array
(which it cannot see), for 25 values of x and y that include Int minimum and
maximum, -1, 0, 1, powers of two and negative odd numbers. Any mismatch is
printed with the operands. Division and remainder are not rewritten (an
arithmetic shift rounds toward negative infinity where / truncates toward zero);
they are in the battery so a future change to them is checked here too.

Generated by gen_opt_arith_identities.py (python3 gen_opt_arith_identities.py opt_arith_identities.obs).
~#

class OptArithIdentities {
  function : Chk(name : String, x : Int, y : Int, got : Int, want : Int) ~ Int {
    if(got <> want) {
      "FAIL: {$name} with x={$x} y={$y}: got {$got}, expected {$want}"->PrintLine();
      return 1;
    };
    return 0;
  }

  function : Main(args : String[]) ~ Nil {
    failures := 0;
'''
out = [hdr]
out.append("    xs := Int->New[%d];" % len(xs))
out.append("    ys := Int->New[%d];" % len(ys))
for i, v in enumerate(xs):
    out.append("    xs[%d] := %s;" % (i, lit(v)))
for i, v in enumerate(ys):
    out.append("    ys[%d] := %s;" % (i, lit(v)))
out.append("    K := Int->New[%d];" % len(consts))
for i, v in enumerate(consts):
    out.append("    K[%d] := %s;" % (i, lit(v)))
out.append("")
out.append("    for(i := 0; i < %d; i += 1;) {" % len(xs))
out.append("      x := xs[i];")
out.append("      y := ys[i];")
out.extend(lines)
out.append("    };")
out.append('''
    if(failures > 0) {
      "{$failures} failure(s)"->PrintLine();
      Runtime->Exit(1);
    };
    "PASS"->PrintLine();
  }
}''')
open(sys.argv[1], "w", newline="\n").write("\n".join(out) + "\n")
print(len(lines), "expressions,", len(consts), "constants")
