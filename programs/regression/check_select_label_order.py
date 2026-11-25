#!/usr/bin/env python3
"""
The compiler must emit the same jump labels for a select statement on every platform.

Usage: check_select_label_order.py <bin_dir> [--update]

SelectArrayTree::Divide used to hand out the label ids of a 2- or 3-value subtree with three
`++conditional_label` in one argument list. C++ leaves that order unspecified: g++ evaluated right to
left, Apple clang left to right, so the same source compiled to different jump operands on arm64
than on x64, and the library set built there differed from the committed one. It is fixed by
naming the order, and this check is what notices if it ever depends on the compiler again.

It compiles select_label_order/select_label_order_lib.obs (selects of 1, 2, 3, 4, 5 and 7 sparse cases, plus strings) at
-opt s0 with -asm and compares the listing of the fixture's own class with
select_label_order.golden.obm, which was produced on x64 with g++. A listing is the compiler's
text dump of what it emits (instruction indices and jump targets), so a different label order
shows as different `JMP: index=` lines. Only the fixture's class is compared (the library classes
before it change with the libraries) and the class/method ids are masked, since they count the
library classes linked.

--update rewrites the golden from the compiler under test: do that only for a deliberate change to
how select statements are emitted, and only from an x64 build.
"""
import os, re, subprocess, sys, tempfile

EXE = ".exe" if os.name == "nt" else ""
HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "select_label_order", "select_label_order_lib.obs")
GOLDEN = os.path.join(HERE, "select_label_order.golden.obm")


def listing_of_fixture(text):
    """The fixture's class onward, with the ids that count linked library classes masked."""
    text = text.replace("\r\n", "\n")
    marker = re.search(r"^Class: id=\d+; name='SelectLabelOrder.Probe'", text, re.M)
    if not marker:
        return None
    body = text[marker.start():]
    body = re.sub(r"^Class: id=\d+;", "Class: id=N;", body, flags=re.M)
    body = re.sub(r"^Method: id=\d+,", "Method: id=N,", body, flags=re.M)
    return body.rstrip("\n") + "\n"


def main():
    args = [a for a in sys.argv[1:] if a != "--update"]
    update = "--update" in sys.argv[1:]
    if len(args) != 1:
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(args[0])
    obc = os.path.join(bin_dir, "obc" + EXE)
    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = os.path.join(os.path.dirname(bin_dir), "lib")
    with tempfile.TemporaryDirectory(prefix="select-label-order-") as work:
        dest = os.path.join(work, "slo.obl")
        p = subprocess.run([obc, "-src", SOURCE, "-tar", "lib", "-opt", "s0", "-dest", dest, "-asm"],
                           cwd=work, env=env, capture_output=True, timeout=300)
        out = (p.stdout + p.stderr).decode("utf-8", "replace")
        listing_path = os.path.join(work, "slo.obm")
        if p.returncode != 0 or not os.path.isfile(listing_path):
            print("  [FAIL] fixture compiles with -asm\n         " + out)
            return 1
        with open(listing_path, encoding="utf-8", errors="replace") as h:
            got = listing_of_fixture(h.read())
    if got is None:
        print("  [FAIL] listing has no SelectLabelOrder class")
        return 1
    if update:
        with open(GOLDEN, "w", encoding="utf-8", newline="\n") as h:
            h.write(got)
        print("  golden rewritten: %d lines" % got.count("\n"))
        return 0
    with open(GOLDEN, encoding="utf-8", newline="") as h:
        want = h.read().replace("\r\n", "\n")
    if got == want:
        print("  [PASS] select statements emit the golden jump labels (%d lines)" % got.count("\n"))
        return 0
    g, w = got.split("\n"), want.split("\n")
    shown = 0
    for i in range(max(len(g), len(w))):
        a = g[i] if i < len(g) else "<end>"
        b = w[i] if i < len(w) else "<end>"
        if a != b:
            print("  line %d: got %r, golden %r" % (i + 1, a, b))
            shown += 1
            if shown >= 12:
                break
    print("  [FAIL] the emitted jump labels differ from the golden: select label ids depend on the compiler's "
          "evaluation order again (see this script's header)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
