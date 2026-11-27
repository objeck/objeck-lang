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

and EACH of the three allocations must be the one that failed, for at least one limit,
with obr saying std::bad_alloc. obr does not say which allocation failed, and the three
fail at the same message, so a gate that asks only for "a bad_alloc somewhere" is passed by
a ladder in which every limit fails the same allocation. Which one it was is read from
the process's peak resident set instead: each array is zero-filled (the "()" on the new),
so every array that was allocated before the failing one is resident, 8 * COUNT bytes
apiece, and the one that failed is not. The smallest peak of all the runs is the first
allocation failing (nothing of the three beforehand; the ladder starts well above what
inflating the image needs), a peak one array above it is the second, two above the third. The check fails, and says which site was never reached,
when a band has no run; so the ladder cannot go quiet by all its limits landing in one.
The control -- the largest limit, on the unpatched program -- must still run.

RLIMIT_AS is what makes an allocation fail on demand. Windows has none, and
macOS does not enforce it, so the test says SKIP there: the code is the same on
every platform, and the Linux legs hold it.
"""

import os
import struct
import subprocess
import sys
import tempfile
import threading
import zlib

COUNT = 40 * 1000 * 1000
MB = 1024 * 1024
# 8 bytes a class in each of three arrays: 320 MB each. The ladder runs from below
# the inflated image alone to above everything the load needs.
# A step of 25 MB, a twelfth of an array, so every band between two arrays has runs in it.
LIMITS_MB = list(range(150, 2000, 25))
ARRAY_MB = 8.0 * COUNT / MB
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
        """(exit status, output, peak resident set in MB). The status is negative for a signal."""
        def limit():
            resource.setrlimit(resource.RLIMIT_AS, (limit_mb * MB, limit_mb * MB))
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        p = subprocess.Popen([obr, image], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, preexec_fn=limit)
        timer = threading.Timer(300, p.kill)
        timer.start()
        try:
            data = p.stdout.read()
            _, status, usage = os.wait4(p.pid, 0)
        finally:
            timer.cancel()
            p.stdout.close()
            p.returncode = 0  # reaped by wait4 above; keeps Popen's own __del__ from waiting again
        rc = os.waitstatus_to_exitcode(status)
        return rc, data.decode("utf-8", "replace"), usage.ru_maxrss / 1024.0  # Linux: kilobytes

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

        rc, out, _ = run(good, LIMITS_MB[-1])
        check("control: the unpatched program runs under the largest limit", rc == 0 and MARKER in out, "exit %d: %s" % (rc, out))

        runs = []
        for limit_mb in LIMITS_MB:
            rc, out, rss = run(big, limit_mb)
            said = "bad_alloc" in out
            runs.append((limit_mb, rc, said, rss))
            check("%4d MB: obr ends by itself (exit %d, peak %4.0f MB%s)" % (limit_mb, rc, rss, ", says std::bad_alloc" if said else ""),
                  0 < rc < 128 and MARKER not in out, out)

        # which allocation failed, from what was resident (see the top of this file)
        failed = [r for r in runs if r[2]]
        check("some limit made an allocation of the load fail and obr said so (%d of %d did)" % (len(failed), len(runs)), len(failed) > 0)
        if runs:
            base = min(r[3] for r in runs)
            check("the lowest peak (%.0f MB) is before the first array, not after it (an array is %.0f MB)" % (base, ARRAY_MB),
                  base < 0.75 * ARRAY_MB)
            for site in range(3):
                got = [r[0] for r in failed if int((r[3] - base) / ARRAY_MB + 0.5) == site]
                check("allocation %d of 3 failed, with bad_alloc, under at least one limit (%s)"
                      % (site + 1, ("%d run(s), %d..%d MB" % (len(got), min(got), max(got))) if got else "none"), bool(got))

    if failures:
        print("\nFAIL: %d check(s): %s" % (len(failures), "; ".join(failures)))
        return 1
    print("\nPASS: an image the loader has no memory for is reported, never a crash")
    return 0


if __name__ == "__main__":
    sys.exit(main())
