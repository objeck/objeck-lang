#!/usr/bin/env python3
"""
A JSON literal compiles to the JsonElement calls it stands for.

Usage: check_json_literal_lowering.py <bin_dir>

`json{"name": value, "list": [a, b]}` is not a new instruction and no new library code. The parser
turns an object into `JsonElement->New(OBJECT)` in a hidden local and an `Insert(name, value)` on it
per member, an array into `JsonElement->New(ARRAY)` and an `Add(value)` per element, written where
the literal is evaluated. This check holds it to that claim.

json_literal_lowering/json_literal_lowering.obs has pairs of functions: `ValueN` returns a literal
and `HandN` builds the same element by hand. The file is compiled at -opt s0 and -opt s3 with -asm:

  * pair 1, a flat object, must have the same listing instruction for instruction (the same frame
    slots, operands and order);
  * pair 2, nested objects and arrays, must have the same instructions and the same frame, in a
    different order: the literal evaluates a member's name and then builds the nested element,
    where the code written by hand builds the element first. Literal operands (the ids of pooled
    strings) are numbered in that order too, so they are masked.

So a literal costs what the calls cost, and nothing reaches the VM or the JIT that those calls do
not already send them. It also runs the program, so the pairs are known to agree in value.
"""
import os, re, subprocess, sys, tempfile

EXE = ".exe" if os.name == "nt" else ""
HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "json_literal_lowering", "json_literal_lowering.obs")
# pair -> whether the order of the instructions is compared too
PAIRS = {1: True, 2: False}


def methods_of(listing):
    """name -> listing body (frame slots and instructions) for each method of the fixture's class."""
    listing = listing.replace("\r\n", "\n")
    out = {}
    for block in re.split(r"\n(?=\s*Method: id=)", listing):
        head = re.match(r"\s*Method: id=[^;]*; name='JsonLiteralLowering:(\w+):[^']*';[^\n]*\n", block)
        if not head:
            continue
        body = block[head.end():]
        # a method's listing ends at the next class
        cut = re.search(r"^Class: id=", body, re.M)
        if cut:
            body = body[:cut.start()]
        out[head.group(1)] = body.rstrip() + "\n"
    return out


def unordered(body):
    """The frame as listed, then the instructions without their indices, literal operands masked, sorted."""
    frame, _, code = body.partition("instructions:\n")
    lines = []
    for line in code.split("\n"):
        m = re.match(r"\s*\d+\s+(\S.*)$", line)
        if m:
            lines.append(re.sub(r"^LOAD_INT_LIT: value=-?\d+", "LOAD_INT_LIT: value=N", m.group(1)))
    return frame, sorted(lines)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = os.path.join(os.path.dirname(bin_dir), "lib")
    failed = 0
    with tempfile.TemporaryDirectory(prefix="json-literal-lowering-") as work:
        for opt in ("s0", "s3"):
            dest = os.path.join(work, "jll_%s.obe" % opt)
            p = subprocess.run([obc, "-src", SOURCE, "-lib", "gen_collect,json", "-opt", opt, "-dest", dest, "-asm"],
                               cwd=work, env=env, capture_output=True, timeout=300)
            out = (p.stdout + p.stderr).decode("utf-8", "replace")
            listing_path = os.path.join(work, "jll_%s.obm" % opt)
            if p.returncode != 0 or not os.path.isfile(listing_path) or not os.path.isfile(dest):
                print("  [FAIL] -opt %s: the fixture compiles with -asm\n         %s" % (opt, out))
                return 1
            with open(listing_path, encoding="utf-8", errors="replace") as h:
                methods = methods_of(h.read())

            # two different functions must differ, or equal listings below would mean nothing
            if methods.get("Value1") is None or methods.get("Value2") is None or \
               methods["Value1"] == methods["Value2"] or unordered(methods["Value1"]) == unordered(methods["Value2"]):
                print("  [FAIL] -opt %s: Value1 and Value2 compare equal; the comparison cannot tell methods apart" % opt)
                failed += 1

            before = failed
            for n, ordered in sorted(PAIRS.items()):
                value, hand = methods.get("Value%d" % n), methods.get("Hand%d" % n)
                if value is None or hand is None:
                    print("  [FAIL] -opt %s: the listing has no Value%d/Hand%d" % (opt, n, n))
                    failed += 1
                    continue
                if "JsonElement:Insert" not in value:
                    print("  [FAIL] -opt %s: Value%d has no Insert call in it; the listing was not read" % (opt, n))
                    failed += 1
                    continue
                same = value == hand if ordered else unordered(value) == unordered(hand)
                if not same:
                    failed += 1
                    print("  [FAIL] -opt %s: Value%d and Hand%d compile differently%s" % (opt, n, n, "" if ordered else " (order aside)"))
                    v = value.split("\n") if ordered else unordered(value)[1]
                    w = hand.split("\n") if ordered else unordered(hand)[1]
                    shown = 0
                    for i in range(max(len(v), len(w))):
                        a = v[i] if i < len(v) else "<end>"
                        b = w[i] if i < len(w) else "<end>"
                        if a != b:
                            print("         line %d: literal %r, by hand %r" % (i + 1, a.strip(), b.strip()))
                            shown += 1
                            if shown >= 8:
                                break
            if failed == before:
                print("  [PASS] -opt %s: a flat literal compiles to the listing of the calls written by hand, a nested one to the same instructions" % opt)

            r = subprocess.run([obr, dest], cwd=work, env=env, capture_output=True, timeout=120)
            ran = (r.stdout + r.stderr).decode("utf-8", "replace")
            if r.returncode != 0 or "PASS: json literal lowering" not in ran:
                failed += 1
                print("  [FAIL] -opt %s: the pairs do not agree in value (exit %d)\n         %s" % (opt, r.returncode, ran.strip()))
            else:
                print("  [PASS] -opt %s: the pairs agree in value" % opt)

    if failed:
        print("  [FAIL] a JSON literal no longer compiles to the JsonElement calls it stands for")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
