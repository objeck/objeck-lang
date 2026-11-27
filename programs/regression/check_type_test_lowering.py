#!/usr/bin/env python3
"""
The binding type test compiles to the test and the cast it stands for.

Usage: check_type_test_lowering.py <bin_dir>

`if(value is Type name) { ... }` is not a new instruction. The parser turns it into
`if(value <> Nil & value->TypeOf(Type)) { name := value->As(Type); ... }` with `name` declared in
the block. This check holds it to that claim.

type_test_lowering/type_test_lowering.obs has pairs of functions: `ValueN` uses the test and
`HandN` is the same code written by hand. The file is compiled at -opt s0 and -opt s3 with -asm
and every pair must have the same listing: the same slots in the frame, the same instructions
with the same operands, the same jump targets. So nothing reaches the VM or the JIT that
`TypeOf` and `As` do not already send them, and the test costs what the long form costs.

It also runs the program, so the pairs are known to agree in value as well as in code.
"""
import os, re, subprocess, sys, tempfile

EXE = ".exe" if os.name == "nt" else ""
HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "type_test_lowering", "type_test_lowering.obs")
PAIRS = 3


def methods_of(listing):
    """name -> listing body (frame slots and instructions) for each method of the fixture's class."""
    listing = listing.replace("\r\n", "\n")
    out = {}
    for block in re.split(r"\n(?=\s*Method: id=)", listing):
        head = re.match(r"\s*Method: id=[^;]*; name='TypeTestLowering:(\w+):[^']*';[^\n]*\n", block)
        if not head:
            continue
        body = block[head.end():]
        # a method's listing ends at the next class
        cut = re.search(r"^Class: id=", body, re.M)
        if cut:
            body = body[:cut.start()]
        out[head.group(1)] = body.rstrip() + "\n"
    return out


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
    with tempfile.TemporaryDirectory(prefix="type-test-lowering-") as work:
        for opt in ("s0", "s3"):
            dest = os.path.join(work, "ttl_%s.obe" % opt)
            p = subprocess.run([obc, "-src", SOURCE, "-lib", "gen_collect", "-opt", opt, "-dest", dest, "-asm"],
                               cwd=work, env=env, capture_output=True, timeout=300)
            out = (p.stdout + p.stderr).decode("utf-8", "replace")
            listing_path = os.path.join(work, "ttl_%s.obm" % opt)
            if p.returncode != 0 or not os.path.isfile(listing_path) or not os.path.isfile(dest):
                print("  [FAIL] -opt %s: the fixture compiles with -asm\n         %s" % (opt, out))
                return 1
            with open(listing_path, encoding="utf-8", errors="replace") as h:
                methods = methods_of(h.read())

            # two different functions must differ, or equal listings below would mean nothing
            if methods.get("Value1") is None or methods.get("Value1") == methods.get("Value2"):
                print("  [FAIL] -opt %s: Value1 and Value2 have the same listing; the comparison cannot tell methods apart" % opt)
                failed += 1

            compared = 0
            for n in range(1, PAIRS + 1):
                value, hand = methods.get("Value%d" % n), methods.get("Hand%d" % n)
                if value is None or hand is None:
                    print("  [FAIL] -opt %s: the listing has no Value%d/Hand%d" % (opt, n, n))
                    failed += 1
                    continue
                if "OBJ_TYPE_OF" not in value or "OBJ_INST_CAST" not in value:
                    print("  [FAIL] -opt %s: Value%d has no type test and cast in it; the listing was not read" % (opt, n))
                    failed += 1
                    continue
                compared += 1
                if value != hand:
                    failed += 1
                    print("  [FAIL] -opt %s: Value%d and Hand%d compile differently" % (opt, n, n))
                    v, w = value.split("\n"), hand.split("\n")
                    shown = 0
                    for i in range(max(len(v), len(w))):
                        a = v[i] if i < len(v) else "<end>"
                        b = w[i] if i < len(w) else "<end>"
                        if a != b:
                            print("         line %d: value %r, by hand %r" % (i + 1, a.strip(), b.strip()))
                            shown += 1
                            if shown >= 8:
                                break
            if compared == PAIRS and not failed:
                print("  [PASS] -opt %s: %d binding type tests compile to the listing of the test and cast written by hand" % (opt, compared))

            r = subprocess.run([obr, dest], cwd=work, env=env, capture_output=True, timeout=120)
            ran = (r.stdout + r.stderr).decode("utf-8", "replace")
            if r.returncode != 0 or "PASS: type test lowering" not in ran:
                failed += 1
                print("  [FAIL] -opt %s: the pairs do not agree in value (exit %d)\n         %s" % (opt, r.returncode, ran.strip()))
            else:
                print("  [PASS] -opt %s: the pairs agree in value" % opt)

    if failed:
        print("  [FAIL] the binding type test no longer compiles to the test and cast it stands for")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
