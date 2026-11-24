#!/usr/bin/env python3
"""
obc refuses a truncated or corrupt .obl with a clear message instead of reading
past the end of the file or sizing an allocation from a garbage length.

Usage: check_obl_truncated.py <bin_dir>

The library reader used to do raw reads with no end pointer: a count read from
the file went straight into `new FLOAT_VALUE[count]` and a length into
`std::string(buffer, length)`. A damaged .obl was an out-of-bounds read or a
huge allocation. Every read is now checked against the end of the inflated file.

The cases are built by taking a real library, inflating it, damaging the
inflated stream, and compressing it again (so zlib itself is happy and it is the
library reader that must notice):
  - truncated at many points: obc must exit non-zero with a message saying the
    library is "truncated or corrupt" (or another named error), never crash
  - a 4-byte field overwritten with 0x7FFFFFFF / 0x80000000 at many aligned
    offsets: any outcome but a crash or a hang is acceptable (the damage may
    land in a value that is still legal)
  - a file too short to hold a zlib header
The control, the undamaged library, must link and run.
"""
import os, random, struct, subprocess, sys, tempfile, zlib

EXE = ".exe" if os.name == "nt" else ""

LIB_SRC = """bundle LibProbe {
\tclass Greeter {
\t\tfunction : Greet() ~ Nil {
\t\t\t"hello from a library"->PrintLine();
\t\t\tf := [1.5, 2.5, 3.5];
\t\t\tb := [true, false];
\t\t\tn := [10, 20, 30];
\t\t\tf[1]->PrintLine();
\t\t\tn[2]->PrintLine();
\t\t}
\t}
}
"""
APP_SRC = """use LibProbe;
class App {
\tfunction : Main(args : String[]) ~ Nil {
\t\tGreeter->Greet();
\t}
}
"""


def run(cmd, env, cwd):
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, timeout=120)
    return p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace")


def inflate(data):
    zlib_at_4 = len(data) >= 6 and data[4] == 0x78 and (0x78 * 256 + data[5]) % 31 == 0
    if data[0] == 0x78 and not zlib_at_4:
        return zlib.decompress(data)
    return zlib.decompress(data[4:])


def pack(stream):
    return struct.pack("<I", len(stream)) + zlib.compress(stream, 9)


def main():
    if len(sys.argv) != 2:
        print(__doc__); return 2
    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    real_lib = os.path.join(os.path.dirname(bin_dir), "lib")
    failures = []

    def check(name, cond, detail=""):
        print("  [%s] %s%s" % ("PASS" if cond else "FAIL", name, "" if cond else "\n         " + detail[-500:]))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="obl-trunc-") as work:
        libdir = os.path.join(work, "lib")
        os.makedirs(libdir)
        for name in os.listdir(real_lib):
            if name in ("lang.obl", "gen_collect.obl"):
                with open(os.path.join(real_lib, name), "rb") as s, open(os.path.join(libdir, name), "wb") as d:
                    d.write(s.read())
        env = dict(os.environ)
        env["OBJECK_LIB_PATH"] = libdir
        lib_src = os.path.join(work, "probe_lib.obs")
        app_src = os.path.join(work, "app.obs")
        open(lib_src, "w").write(LIB_SRC)
        open(app_src, "w").write(APP_SRC)
        good = os.path.join(libdir, "libprobe.obl")
        code, out = run([obc, "-src", lib_src, "-tar", "lib", "-dest", good], env, work)
        if code != 0 or not os.path.isfile(good):
            print("could not build the probe library (exit %d):\n%s" % (code, out)); return 2
        exe = os.path.join(work, "app.obe")
        code, out = run([obc, "-src", app_src, "-lib", "libprobe", "-dest", exe], env, work)
        check("control: the intact library links", code == 0 and os.path.isfile(exe), out)
        if code == 0:
            code, out = run([obr, exe, "-lib", "libprobe"], env, work)
            check("control: the program runs", code == 0 and "hello from a library" in out, out)

        stream = inflate(open(good, "rb").read())
        print("  library stream: %d bytes" % len(stream))

        def link(data):
            open(good, "wb").write(data)
            dest = os.path.join(work, "damaged.obe")
            if os.path.exists(dest):
                os.remove(dest)
            return run([obc, "-src", app_src, "-lib", "libprobe", "-dest", dest], env, work)

        # a file too short to hold a size prefix and a zlib header
        for n in (0, 1, 3, 5):
            code, out = link(b"\x01" * n)
            check("%d-byte file: refused cleanly" % n, 0 < code < 128 and ("ibrary" in out or "ncompress" in out), "exit=%d %s" % (code, out))

        cuts = sorted(set([8, 9, 12, 16, 20, 24, 40, 64, len(stream) // 4, len(stream) // 2,
                           len(stream) - 100, len(stream) - 5, len(stream) - 1]))
        for cut in cuts:
            if not 0 < cut < len(stream):
                continue
            code, out = link(pack(stream[:cut]))
            check("truncated at %d: refused, no crash" % cut, 0 < code < 128 and ("ibrary" in out or "ncompress" in out), "exit=%d %s" % (code, out))
        code, out = link(pack(stream[:len(stream) // 2]))
        check("truncation names the library and says truncated or corrupt",
              "truncated or corrupt" in out and "libprobe" in out, out)

        rng = random.Random(1234)
        slots = list(range(8, len(stream) - 4, 4))
        for off in rng.sample(slots, min(150, len(slots))):
            for val in (0x7FFFFFFF, 0x80000000):
                damaged = bytearray(stream)
                struct.pack_into("<I", damaged, off, val)
                code, out = link(pack(bytes(damaged)))
                check("field at %d set to %#x: no crash" % (off, val), 0 <= code < 128, "exit=%d %s" % (code, out))

    print("\n%d failure(s)" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
