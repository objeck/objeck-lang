#!/usr/bin/env python3
"""
An 'enum' and a 'consts' group mean the same in a library as beside the program.

Usage: check_lib_enum_consts.py <bin_dir>

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are
read from the sibling lib/ directory, as check_lib_generic_inference.py does.

A 'match' treats the two differently: an enum is a closed set of items, so its
arms name items and, when they name them all, no 'other' arm is needed; a
consts group is a set of named integers, so its arms may be numbers and it
always takes 'other'. Beside the program the parser knows which one it read. A
library recorded a name, an offset and the items, a consts group with offset
-1, and the linker guessed: a consts group whose values happened to be -1, 0,
.. read back as 'enum := -1'. `match k { 0 => .., other => .. }` on such a
group compiled beside its source and was refused through its library
("Expected an item of 'Kinds.Edge', the enum this 'match' is on").

The library's enum record now says which it is. This builds one library with
consts groups (-1 and 0; -1, 0, 1; from a lower negative value; with gaps;
one value) and enums (numbered from 0, from -1, from -20) and compiles each
program twice: against the library, and with the library's source as a second
source file. The two must agree:

  - a program that matches on every group runs to the same output, at -opt s0
    and s3, in three execution modes
  - each refused program is refused both ways with the same messages: a number
    for an enum arm, an enum match that leaves an item out, a consts match
    with no 'other'

An enum record whose kind byte is neither 0 nor 1 is refused by the linker.
"""

import os
import struct
import subprocess
import sys
import tempfile
import zlib

EXE = ".exe" if os.name == "nt" else ""
TIMEOUT = 180

LIBRARY = """bundle Kinds {
  # the two the linker could not tell from 'enum := -1'
  consts Edge {
    Below := -1,
    At := 0
  }

  consts Steps {
    Back := -1,
    Stay := 0,
    Ahead := 1
  }

  # other consts groups
  consts Depth {
    Floor := -20,
    Above := -19
  }

  consts Mask {
    None := 0,
    Read := 4,
    Write := 8
  }

  consts Single {
    Only := -1
  }

  # enums, numbered from 0, from -1 (the offset a consts group is written with)
  # and from a lower negative value
  enum Color {
    Red,
    Green,
    Blue
  }

  enum Side := -1 {
    Left,
    Right
  }

  enum Format := -20 {
    Plain,
    Rich
  }

  class Maker {
    function : public : EdgeOf(n : Int) ~ Edge {
      return n < 0 ? Edge->Below : Edge->At;
    }

    function : public : StepOf(n : Int) ~ Steps {
      if(n < 0) {
        return Steps->Back;
      };
      return n = 0 ? Steps->Stay : Steps->Ahead;
    }

    function : public : DepthOf(n : Int) ~ Depth {
      return n < 0 ? Depth->Floor : Depth->Above;
    }

    function : public : MaskOf(n : Int) ~ Mask {
      if(n = 0) {
        return Mask->None;
      };
      return n = 1 ? Mask->Read : Mask->Write;
    }

    function : public : SingleOf() ~ Single {
      return Single->Only;
    }

    function : public : ColorOf(n : Int) ~ Color {
      if(n = 0) {
        return Color->Red;
      };
      return n = 1 ? Color->Green : Color->Blue;
    }

    function : public : SideOf(n : Int) ~ Side {
      return n < 0 ? Side->Left : Side->Right;
    }

    function : public : FormatOf(n : Int) ~ Format {
      return n < 0 ? Format->Plain : Format->Rich;
    }
  }
}
"""

PROGRAM = """use Kinds;

class User {
  function : Edge(k : Edge) ~ Int {
    return match k { 0 => 10, -1 => 20, other => 30 };
  }

  function : EdgeNamed(k : Edge) ~ Int {
    return match k { Edge->At => 11, Edge->Below => 21, other => 31 };
  }

  function : Step(k : Steps) ~ Int {
    return match k { -1 => 1, 0 => 2, 1 => 3, other => 4 };
  }

  function : Depth(k : Depth) ~ Int {
    return match k { -20 => 5, Depth->Above => 6, other => 7 };
  }

  function : Mask(k : Mask) ~ Int {
    return match k { 0 => 8, 4 => 9, other => 12 };
  }

  function : Single(k : Single) ~ Int {
    return match k { -1 => 13, other => 14 };
  }

  # every item named: no 'other'
  function : Color(c : Color) ~ Int {
    return match c { Color->Red => 100, Color->Green => 200, Color->Blue => 300 };
  }

  function : Side(s : Side) ~ Int {
    return match s { Side->Left => 400, Side->Right => 500 };
  }

  function : Format(f : Format) ~ Int {
    return match f { Format->Plain => 600, Format->Rich => 700 };
  }

  function : Main(args : String[]) ~ Nil {
    Edge(Maker->EdgeOf(0))->PrintLine();
    Edge(Maker->EdgeOf(-1))->PrintLine();
    EdgeNamed(Maker->EdgeOf(0))->PrintLine();
    EdgeNamed(Maker->EdgeOf(-1))->PrintLine();
    Step(Maker->StepOf(-1))->PrintLine();
    Step(Maker->StepOf(0))->PrintLine();
    Step(Maker->StepOf(1))->PrintLine();
    Depth(Maker->DepthOf(-1))->PrintLine();
    Depth(Maker->DepthOf(1))->PrintLine();
    Mask(Maker->MaskOf(0))->PrintLine();
    Mask(Maker->MaskOf(1))->PrintLine();
    Mask(Maker->MaskOf(2))->PrintLine();
    Single(Maker->SingleOf())->PrintLine();
    Color(Maker->ColorOf(0))->PrintLine();
    Color(Maker->ColorOf(2))->PrintLine();
    Side(Maker->SideOf(-1))->PrintLine();
    Side(Maker->SideOf(1))->PrintLine();
    Format(Maker->FormatOf(-1))->PrintLine();
    Format(Maker->FormatOf(1))->PrintLine();
  }
}
"""

EXPECTED = ["10", "20", "11", "21", "1", "2", "3", "5", "6", "8", "9", "12", "13",
            "100", "300", "400", "500", "600", "700"]

# (name, source, a message the compiler must report, both ways)
BAD = [
    ("a number for an arm of an enum numbered from -1", """use Kinds;

class Bad {
  function : Main(args : String[]) ~ Nil {
    v := match Maker->SideOf(0) { 0 => 1, other => 2 };
    v->PrintLine();
  }
}
""", "bad.obs:(5,35): Expected an item of 'Kinds.Side', the enum this 'match' is on"),
    ("a number for an arm of an enum numbered from 0", """use Kinds;

class Bad {
  function : Main(args : String[]) ~ Nil {
    v := match Maker->ColorOf(0) { 0 => 1, other => 2 };
    v->PrintLine();
  }
}
""", "bad.obs:(5,36): Expected an item of 'Kinds.Color', the enum this 'match' is on"),
    ("an enum numbered from -1 with an item left out", """use Kinds;

class Bad {
  function : Main(args : String[]) ~ Nil {
    v := match Maker->SideOf(0) { Side->Left => 1 };
    v->PrintLine();
  }
}
""", "This 'match' does not cover every item of 'Kinds.Side', missing: Right"),
    ("an enum numbered from -20 with an item left out", """use Kinds;

class Bad {
  function : Main(args : String[]) ~ Nil {
    v := match Maker->FormatOf(0) { Format->Rich => 1 };
    v->PrintLine();
  }
}
""", "This 'match' does not cover every item of 'Kinds.Format', missing: Plain"),
    ("a consts group of -1 and 0 with every name and no 'other'", """use Kinds;

class Bad {
  function : Main(args : String[]) ~ Nil {
    v := match Maker->EdgeOf(0) { Edge->Below => 1, Edge->At => 2 };
    v->PrintLine();
  }
}
""", "'other'"),
    ("a consts group of -1, 0, 1 with every name and no 'other'", """use Kinds;

class Bad {
  function : Main(args : String[]) ~ Nil {
    v := match Maker->StepOf(0) { Steps->Back => 1, Steps->Stay => 2, Steps->Ahead => 3 };
    v->PrintLine();
  }
}
""", "'other'"),
    ("a consts group with gaps and no 'other'", """use Kinds;

class Bad {
  function : Main(args : String[]) ~ Nil {
    v := match Maker->MaskOf(0) { Mask->None => 1, Mask->Read => 2, Mask->Write => 3 };
    v->PrintLine();
  }
}
""", "'other'"),
]

MODES = [("default", {}), ("JIT on every call", {"OBJECK_JIT_THRESHOLD": "1"}),
         ("interpreter", {"OBJECK_JIT_DISABLE": "1"})]


def fail(msg):
    print("FAIL: %s" % msg)
    sys.exit(1)


def describe(code):
    if code < 0:
        return "died on signal %d" % -code
    return "exit %d" % code


def run(argv, cwd, env, what, expect_ok=True):
    try:
        proc = subprocess.run(argv, cwd=cwd, env=env, timeout=TIMEOUT,
                              stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except subprocess.TimeoutExpired:
        fail("%s timed out after %ds" % (what, TIMEOUT))
    out = proc.stdout.decode("utf-8", "replace").strip()
    if expect_ok and proc.returncode != 0:
        fail("%s %s\n%s" % (what, describe(proc.returncode), out))
    if not expect_ok and proc.returncode <= 0:
        fail("%s should have failed to compile, got %s\n%s" % (what, describe(proc.returncode), out))
    return out


def non_empty(path, what):
    if not os.path.isfile(path) or os.path.getsize(path) == 0:
        fail("%s produced no %s" % (what, os.path.basename(path)))


def diagnostics(out):
    """The compiler's messages about bad.obs: what it says of the program,
    which must not depend on where the enums came from."""
    return [line.strip() for line in out.splitlines() if "bad.obs:(" in line]


def read_image(path):
    """The inflated bytes of a library: [size: 4 bytes][zlib stream], or a bare
    zlib stream."""
    with open(path, "rb") as handle:
        raw = handle.read()
    try:
        return zlib.decompress(raw[4:])
    except zlib.error:
        return zlib.decompress(raw)


def write_image(path, image):
    with open(path, "wb") as handle:
        handle.write(struct.pack("<I", len(image)) + zlib.compress(image))


def enum_kind_offset(image, name):
    """Where the kind byte of the enum record called 'name' is: a record is the
    name (a 4-byte length, then UTF-8), the offset (8 bytes), the kind (1 byte),
    then the items."""
    encoded = name.encode("utf-8")
    at = image.find(struct.pack("<i", len(encoded)) + encoded)
    if at < 0:
        fail("no enum record named %s in the library" % name)
    return at + 4 + len(encoded) + 8


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip())
        return 2

    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    lib_dir = os.path.join(os.path.dirname(bin_dir), "lib")
    for path in (obc, obr):
        if not os.path.isfile(path):
            fail("no %s" % path)
    if not os.path.isdir(lib_dir):
        fail("no library directory at %s" % lib_dir)

    with tempfile.TemporaryDirectory() as work:
        # obc resolves -lib against OBJECK_LIB_PATH, so the generated library
        # goes into a private copy of lib/ rather than the real deploy tree.
        private_lib = os.path.join(work, "lib")
        os.mkdir(private_lib)
        for name in os.listdir(lib_dir):
            if name.endswith(".obl") or name == "configobjk.ini":
                with open(os.path.join(lib_dir, name), "rb") as src:
                    with open(os.path.join(private_lib, name), "wb") as dst:
                        dst.write(src.read())

        env = dict(os.environ)
        env["OBJECK_LIB_PATH"] = private_lib
        env.pop("OBJECK_JIT_THRESHOLD", None)
        env.pop("OBJECK_JIT_DISABLE", None)

        for name, text in {"kinds.obs": LIBRARY, "user.obs": PROGRAM}.items():
            with open(os.path.join(work, name), "w") as handle:
                handle.write(text)

        lib_obl = os.path.join(private_lib, "kinds.obl")
        run([obc, "-src", "kinds.obs", "-tar", "lib", "-dest", lib_obl], work, env, "library build")
        non_empty(lib_obl, "library build")

        # the same program against the library and beside its source
        builds = [("through the library", ["-src", "user.obs", "-lib", "kinds"]),
                  ("from source", ["-src", "kinds.obs,user.obs"])]
        for opt in ("s0", "s3"):
            for how, args in builds:
                prog_obe = os.path.join(work, "user_%s_%s.obe" % (opt, how.split()[0]))
                run([obc] + args + ["-opt", opt, "-dest", prog_obe], work, env, "program build (%s, -opt %s)" % (how, opt))
                non_empty(prog_obe, "program build (%s, -opt %s)" % (how, opt))

                for mode, extra in MODES:
                    mode_env = dict(env)
                    mode_env.update(extra)
                    out = run([obr, prog_obe], work, mode_env, "program run (%s, -opt %s, %s)" % (how, opt, mode))
                    lines = [l.strip() for l in out.splitlines() if l.strip()]
                    if lines != EXPECTED:
                        fail("wrong output (%s, -opt %s, %s)\n  expected: %s\n  got:      %s" %
                             (how, opt, mode, EXPECTED, lines))

        bad_builds = [("through the library", ["-src", "bad.obs", "-lib", "kinds"]),
                      ("from source", ["-src", "kinds.obs,bad.obs"])]
        for what, text, message in BAD:
            with open(os.path.join(work, "bad.obs"), "w") as handle:
                handle.write(text)
            said = []
            for how, args in bad_builds:
                out = run([obc] + args + ["-dest", os.path.join(work, "bad.obe")], work, env,
                          "%s (%s)" % (what, how), expect_ok=False)
                if message not in out:
                    fail("%s (%s): expected '%s'\n%s" % (what, how, message, out))
                said.append(diagnostics(out))
            if said[0] != said[1]:
                fail("%s: the library and its source are told different things\n  through the library: %s\n  from source:         %s" %
                     (what, said[0], said[1]))

        # the kind byte: an enum is 0, a consts group 1, and the linker takes
        # nothing else
        image = bytearray(read_image(lib_obl))
        for name, kind in (("Kinds.Edge", 1), ("Kinds.Steps", 1), ("Kinds.Mask", 1), ("Kinds.Side", 0), ("Kinds.Color", 0)):
            at = enum_kind_offset(image, name)
            if image[at] != kind:
                fail("the record of %s has kind %d, expected %d" % (name, image[at], kind))

        with open(os.path.join(work, "bad.obs"), "w") as handle:
            handle.write(BAD[0][1])
        crafted = bytearray(image)
        crafted[enum_kind_offset(crafted, "Kinds.Side")] = 2
        write_image(lib_obl, bytes(crafted))
        out = run([obc, "-src", "bad.obs", "-lib", "kinds", "-dest", os.path.join(work, "bad.obe")], work, env,
                  "a kind byte of 2", expect_ok=False)
        if "is corrupt (an enum record that is neither an 'enum' nor a 'consts' group)" not in out:
            fail("a kind byte of 2: expected the library to be refused as corrupt\n%s" % out)

        # cut inside the record, at the kind byte: refused, not read past
        cut = bytes(image[:enum_kind_offset(image, "Kinds.Side")])
        write_image(lib_obl, cut)
        out = run([obc, "-src", "bad.obs", "-lib", "kinds", "-dest", os.path.join(work, "bad.obe")], work, env,
                  "a library cut at a kind byte", expect_ok=False)
        if "truncated or corrupt" not in out:
            fail("a library cut at a kind byte: expected it to be refused as truncated\n%s" % out)

        # the mark decides, not the values: the Edge group marked an enum is one
        flipped = bytearray(image)
        flipped[enum_kind_offset(flipped, "Kinds.Edge")] = 0
        write_image(lib_obl, bytes(flipped))
        with open(os.path.join(work, "bad.obs"), "w") as handle:
            handle.write(BAD[4][1].replace("Edge->Below => 1, Edge->At => 2", "0 => 1, other => 2"))
        out = run([obc, "-src", "bad.obs", "-lib", "kinds", "-dest", os.path.join(work, "bad.obe")], work, env,
                  "a consts group marked as an enum", expect_ok=False)
        if "Expected an item of 'Kinds.Edge', the enum this 'match' is on" not in out:
            fail("a consts group marked as an enum: expected its numeric arm to be refused\n%s" % out)

    print("PASS: an enum and a consts group read the same from a library and from source "
          "(two opt levels, three modes, %d refusals, 3 crafted libraries)" % len(BAD))
    return 0


if __name__ == "__main__":
    sys.exit(main())
