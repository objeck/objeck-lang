#!/usr/bin/env python3
"""
obr refuses an .obe compiled by another toolchain version with a message that
names both versions and says to recompile.

Usage: check_obe_version_mismatch.py <bin_dir>

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are
read from the sibling lib/ directory, as check_obi_obr_parity.py does.

An .obe is tied to the toolchain that compiled it: the VM's traps, call stack
and bytecode change between versions and the file carries no format version of
its own, only VER_NUM written first. The loader compared it and exited with
"This executable appears to be invalid or compiled with an incompatible version
of the tool chain." -- which named neither version, did not say which side was
stale, and did not say what to do. Every release that changes the VM makes this
the first thing an upgrader sees when they run yesterday's .obe, so the message
has to carry the diagnosis: the two versions, the direction, and "recompile".

The case is built by patching a freshly compiled program's VER_NUM rather than
by keeping an .obe from an older release in the tree: a committed binary rots
(it would stop being "one version older" at the next bump) and could not cover
the newer-than-the-runtime direction at all. The file format is
[uncompressed_size:4][zlib stream] (Loader::LoadFileBuffer), with the version
as the first 4-byte little-endian int of the stream (Emitter: WriteInt(VER_NUM)).

Each direction must:
  exit non-zero, before running a line of the program (its output is absent)
  name the file
  print the program's version and the runtime's version
  say "recompile"
The control -- the unpatched .obe -- must still run.

The magic number follows the version and names the file's layout (core/shared/
version.in). It moves when a record gains a field, and two builds of one
toolchain version can then disagree about it: a developer's .obe or .obl from
before the change, read by a build from after it. Such a file is refused with
a message of the same kind, by obr and by obc's linker:
  exit non-zero, before a field of the file is taken for another
  name the file
  print the file's magic and the one this build reads
  say to recompile (a program) or rebuild (a library)
A magic that was never a program's or a library's is still an unknown file.
"""

import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

EXE = ".exe" if os.name == "nt" else ""

SOURCE = """class VersionMismatchProbe {
\tfunction : Main(args : String[]) ~ Nil {
\t\t"PROBE RAN"->PrintLine();
\t}
}
"""


def run(cmd, env, cwd):
    proc = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, timeout=60)
    return proc.returncode, (proc.stdout + proc.stderr).decode("utf-8", "replace")


def read_obe(path):
    with open(path, "rb") as handle:
        data = handle.read()
    # raw zlib (old format) starts with the CMF byte 0x78; the new format puts a
    # 4-byte uncompressed size first -- the same test the loader makes
    zlib_at_4 = len(data) >= 6 and data[4] == 0x78 and (0x78 * 256 + data[5]) % 31 == 0
    if data[0] == 0x78 and not zlib_at_4:
        return zlib.decompress(data)
    return zlib.decompress(data[4:])


def write_obe(path, stream):
    packed = zlib.compress(stream, 9)
    with open(path, "wb") as handle:
        handle.write(struct.pack("<I", len(stream)) + packed)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    lib_dir = os.path.join(os.path.dirname(bin_dir), "lib")
    for tool in (obc, obr):
        if not os.path.isfile(tool):
            print("missing: %s" % tool)
            return 2

    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = lib_dir
    env["PATH"] = bin_dir + os.pathsep + os.path.join(lib_dir, "native") + os.pathsep + env.get("PATH", "")

    failures = []

    def check(name, cond, detail=""):
        print("  [%s] %s%s" % ("PASS" if cond else "FAIL", name, "" if cond else "\n         " + detail))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="obe-version-") as work:
        src = os.path.join(work, "probe.obs")
        with open(src, "w") as handle:
            handle.write(SOURCE)
        good = os.path.join(work, "probe.obe")
        code, out = run([obc, "-src", src, "-dest", good], env, work)
        if code != 0 or not os.path.isfile(good):
            print("could not compile the probe (exit %d):\n%s" % (code, out))
            return 2

        code, out = run([obr, good], env, work)
        check("the unpatched probe runs", code == 0 and "PROBE RAN" in out,
              "exit=%d out=%r" % (code, out))

        stream = read_obe(good)
        ver_num = struct.unpack("<i", stream[:4])[0]
        check("the stream starts with a plausible VER_NUM (got %d)" % ver_num,
              2000000 < ver_num < 3000000)

        for label, delta in (("older program", -1), ("newer program", +1)):
            other = ver_num + delta
            patched = os.path.join(work, "probe_%s.obe" % label.replace(" ", "_"))
            write_obe(patched, struct.pack("<i", other) + stream[4:])
            code, out = run([obr, patched], env, work)
            tag = "%s (%d on a %d runtime)" % (label, other, ver_num)
            check("%s: refused, non-zero exit" % tag, code != 0, "exit=%d out=%r" % (code, out))
            check("%s: the program never ran" % tag, "PROBE RAN" not in out, "out=%r" % out)
            check("%s: the message names the file" % tag, os.path.basename(patched) in out, "out=%r" % out)
            check("%s: the message names the program's version" % tag, str(other) in out, "out=%r" % out)
            check("%s: the message names the runtime's version" % tag, str(ver_num) in out, "out=%r" % out)
            check("%s: the message says to recompile" % tag,
                  re.search(r"recompile", out, re.IGNORECASE) is not None, "out=%r" % out)
            expected_direction = "older than this runtime" if delta < 0 else "older than the program"
            check("%s: the message says which side is stale" % tag, expected_direction in out, "out=%r" % out)

        # The magic follows VER_NUM. Only the current values are accepted. The layout
        # before this one carried the magic one lower (version.in lists them), which is
        # what a file left from an earlier build of this version has.
        magic = struct.unpack("<i", stream[4:8])[0]
        superseded = magic - 1
        patched = os.path.join(work, "probe_magic_%x.obe" % superseded)
        write_obe(patched, stream[:4] + struct.pack("<i", superseded) + stream[8:])
        code, out = run([obr, patched], env, work)
        tag = "the layout before this one (0x%x, this toolchain writes 0x%x)" % (superseded, magic)
        check("%s: refused, non-zero exit" % tag, code != 0, "exit=%d out=%r" % (code, out))
        check("%s: the program never ran" % tag, "PROBE RAN" not in out, "out=%r" % out)
        check("%s: the message names the file" % tag, os.path.basename(patched) in out, "out=%r" % out)
        check("%s: the message says the layout differs, not that the file is unknown" % tag,
              "File layout mismatch" in out and "Unknown file type" not in out, "out=%r" % out)
        check("%s: the message names the file's magic and the runtime's" % tag,
              "0x%x" % superseded in out and "0x%x" % magic in out, "out=%r" % out)
        check("%s: the message says to recompile" % tag,
              re.search(r"recompile", out, re.IGNORECASE) is not None, "out=%r" % out)

        # a magic that was never a program's or a library's: an unknown file, as before
        patched = os.path.join(work, "probe_magic_unknown.obe")
        write_obe(patched, stream[:4] + struct.pack("<i", 0x1234) + stream[8:])
        code, out = run([obr, patched], env, work)
        check("a magic that is no layout of ours (0x1234): refused as an unknown file",
              code != 0 and "Unknown file type" in out and "PROBE RAN" not in out, "exit=%d out=%r" % (code, out))

        lib_src = os.path.join(work, "probe_lib.obs")
        with open(lib_src, "w") as handle:
            handle.write("bundle Probe {\n\tclass Lib {\n\t\tfunction : Answer() ~ Int { return 42; }\n\t}\n}\n")
        lib = os.path.join(work, "probe_lib.obl")
        code, out = run([obc, "-src", lib_src, "-tar", "lib", "-dest", lib], env, work)
        if code != 0 or not os.path.isfile(lib):
            print("could not compile the probe library (exit %d):\n%s" % (code, out))
            return 2
        user_src = os.path.join(work, "probe_user.obs")
        with open(user_src, "w") as handle:
            handle.write("use Probe;\nclass LibUser {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tLib->Answer()->PrintLine();\n\t}\n}\n")
        # -lib names a library in the library directory, so link against a private copy of it
        # holding the toolchain's libraries and the probe library
        probe_libs = os.path.join(work, "libs")
        os.makedirs(probe_libs)
        for name in os.listdir(lib_dir):
            if name.endswith(".obl"):
                shutil.copy(os.path.join(lib_dir, name), probe_libs)
        shutil.copy(lib, os.path.join(probe_libs, "probe_lib.obl"))
        link_env = dict(env)
        link_env["OBJECK_LIB_PATH"] = probe_libs
        code, out = run([obc, "-src", user_src, "-lib", "probe_lib", "-dest", os.path.join(work, "probe_user.obe")], link_env, work)
        check("the unpatched probe library links", code == 0, "exit=%d out=%r" % (code, out))

        lib_stream = read_obe(lib)
        lib_magic = struct.unpack("<i", lib_stream[4:8])[0]
        check("a library's magic is not a program's (0x%x, 0x%x)" % (lib_magic, magic), lib_magic != magic)

        # a library given to obr is named as one (it carries the library magic this toolchain
        # writes); under a program's name, since obr adds '.obe' to any other
        lib_as_program = os.path.join(work, "probe_lib_as_program.obe")
        shutil.copy(lib, lib_as_program)
        code, out = run([obr, lib_as_program], env, work)
        check("obr given a library says it is a library", code != 0 and "it is a library (.obl)" in out,
              "exit=%d out=%r" % (code, out))

        def link_with(label, other):
            patched = os.path.join(work, "probe_lib_%x.obl" % other)
            write_obe(patched, lib_stream[:4] + struct.pack("<i", other) + lib_stream[8:])
            shutil.copy(patched, os.path.join(probe_libs, "probe_lib.obl"))
            code, out = run([obc, "-src", user_src, "-lib", "probe_lib", "-dest", os.path.join(work, "probe_user2.obe")], link_env, work)
            tag = "%s (0x%x, this toolchain writes 0x%x)" % (label, other, lib_magic)
            check("%s: refused, non-zero exit" % tag, code != 0, "exit=%d out=%r" % (code, out))
            check("%s: the message names the file" % tag, "probe_lib.obl" in out, "out=%r" % out)
            check("%s: the compiler says why, not just exits" % tag, "Unable to" in out, "out=%r" % out)
            return tag, out

        tag, out = link_with("the library layout before this one", lib_magic - 1)
        check("%s: the message names the file's magic and the toolchain's" % tag,
              "0x%x" % (lib_magic - 1) in out and "0x%x" % lib_magic in out, "out=%r" % out)
        check("%s: the message says to rebuild the library" % tag,
              re.search(r"rebuild", out, re.IGNORECASE) is not None, "out=%r" % out)
        tag, out = link_with("an executable's magic", magic)
        check("%s: the message says it is an executable" % tag, "executable" in out, "out=%r" % out)
        link_with("a magic that is no layout of ours", 0x1234)

    print("\n%d failure(s)" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
