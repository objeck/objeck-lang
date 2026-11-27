#!/usr/bin/env python3
"""
obr reports an image it has no memory to load; it does not crash.

Usage: check_loader_alloc_failure.py <bin_dir>

<bin_dir> must contain obc and obr; the libraries are read from the sibling lib/
directory, as check_obe_version_mismatch.py does.

Loader::LoadClasses makes three arrays sized by the image's class count, one
after the other, and a PartialClasses on its stack frees them if the load stops
part way. Its members were not initialized: when one of the three allocations
threw std::bad_alloc, the destructor ran over whatever the stack held for the
pointers not yet assigned, and obr ended with a segmentation fault in
~PartialClasses instead of ">>> load error: std::bad_alloc <<<" (found by the
Codex review of v2026.11.4, R2-S3-1; reproduced with exactly this image).

The image is a freshly compiled program whose class count is raised to COUNT,
with COUNT bytes appended so the loader's own bound (a count no larger than the
bytes left) lets it through. It is run under a ladder of address-space limits
(RLIMIT_AS), so the allocation that fails is, across the ladder, each of the
three and those before and after them. At every limit obr must

  * end by itself: an exit status, never a signal (the crash was SIGSEGV), and
  * not run a line of the program.

and at least one limit must produce the std::bad_alloc load error, so the ladder
is known to have reached the allocations at all. The control -- the same limits'
largest, on the unpatched program -- must still run.

RLIMIT_AS is what makes an allocation fail on demand. Windows has none, and
macOS does not enforce it, so the test says SKIP there: the code is the same on
every platform, and the Linux legs hold it.
"""

import os
import struct
import subprocess
import sys
import tempfile
import zlib

COUNT = 40 * 1000 * 1000
MB = 1024 * 1024
# 8 bytes a class in each of three arrays: 320 MB each. The ladder runs from below
# the inflated image alone to above everything the load needs.
LIMITS_MB = list(range(200, 1700, 100))
MARKER = "alloc-failure-control-ran"

PROGRAM = """class AllocFailure {
  function : Main(args : String[]) ~ Nil {
    "%s"->PrintLine();
  }
}
""" % MARKER


def classes_offset(tree, image):
    sys.path.insert(0, os.path.join(tree, "tools", "fuzz", "loader"))
    import obe_image
    return obe_image.parse(image)[0]


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    if sys.platform != "linux":
        print("SKIP: no enforced address-space limit on this platform (RLIMIT_AS); the Linux legs run this")
        return 0
    import resource

    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc")
    obr = os.path.join(bin_dir, "obr")
    tree = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = os.path.join(os.path.dirname(bin_dir), "lib")
    failures = []

    def check(name, cond, detail=""):
        print("  [%s] %s" % ("PASS" if cond else "FAIL", name))
        if not cond:
            failures.append(name)
            if detail:
                print("         " + str(detail)[-400:])

    def run(image, limit_mb):
        def limit():
            resource.setrlimit(resource.RLIMIT_AS, (limit_mb * MB, limit_mb * MB))
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        p = subprocess.run([obr, image], env=env, capture_output=True, timeout=300, preexec_fn=limit)
        return p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace")

    with tempfile.TemporaryDirectory(prefix="alloc-failure-") as work:
        src = os.path.join(work, "alloc_failure.obs")
        good = os.path.join(work, "alloc_failure.obe")
        big = os.path.join(work, "alloc_failure_big.obe")
        with open(src, "w", encoding="utf-8") as f:
            f.write(PROGRAM)
        p = subprocess.run([obc, "-src", src, "-dest", good], env=env, capture_output=True, timeout=300)
        if p.returncode != 0 or not os.path.exists(good):
            print("FAIL: the program did not compile\n" + (p.stdout + p.stderr).decode("utf-8", "replace")[-400:])
            return 1

        # [uncompressed size:4][zlib stream] (Loader::LoadFileBuffer); the class count
        # is the int the class table starts with
        with open(good, "rb") as f:
            raw = f.read()
        image = bytearray(zlib.decompress(raw[4:]))
        at = classes_offset(tree, bytes(image))
        real = struct.unpack_from("<i", image, at)[0]
        check("the compiled image has a class table (%d classes at byte %d)" % (real, at), 0 < real < 1000)
        struct.pack_into("<i", image, at, COUNT)
        image += bytes(COUNT)
        with open(big, "wb") as f:
            f.write(struct.pack("<I", len(image)) + zlib.compress(bytes(image), 1))

        rc, out = run(good, LIMITS_MB[-1])
        check("control: the unpatched program runs under the largest limit", rc == 0 and MARKER in out, "exit %d: %s" % (rc, out))

        reported = 0
        for limit_mb in LIMITS_MB:
            rc, out = run(big, limit_mb)
            said = "bad_alloc" in out
            reported += 1 if said else 0
            check("%4d MB: obr ends by itself (exit %d%s)" % (limit_mb, rc, ", says std::bad_alloc" if said else ""),
                  0 < rc < 128 and MARKER not in out, out)
        check("some limit made an allocation of the load fail and obr said so (%d of %d did)" % (reported, len(LIMITS_MB)), reported > 0)

    if failures:
        print("\nFAIL: %d check(s): %s" % (len(failures), "; ".join(failures)))
        return 1
    print("\nPASS: an image the loader has no memory for is reported, never a crash")
    return 0


if __name__ == "__main__":
    sys.exit(main())
