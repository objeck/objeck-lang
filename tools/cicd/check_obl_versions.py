#!/usr/bin/env python3
"""Every committed core/lib/*.obl must carry the toolchain version in version.h.

A library is tied to the toolchain that built it. The linker
(core/compiler/linker.cpp, Library::LoadFile) and the runtime loader refuse a
file whose first integer is not VER_NUM, so a version bump that forgets to
rebuild and commit a library ships a tree that cannot link: the first symptom
is "Toolchain version mismatch reading lang.obl" on the first program anyone
compiles. This catches it in the
`tools` job, without building anything.

File layout, as Library::LoadFileBuffer reads it:

  [uncompressed_size:int32 LE][zlib stream]     current format
  [zlib stream]                                 older format, first byte 0x78

When the first byte is 0x78 the file is the older format unless bytes 4..5 are
a valid zlib header (0x78 then a FLG with (0x78*256+FLG) % 31 == 0), in which
case the size just happens to end in 0x78. The inflated payload starts with
the version int32 (little endian), then the magic number.

Fails (exit 1) on any file that is unreadable, will not inflate, is too short,
or carries another version, and when there is no version.h VER_NUM or no
library to check: nothing is skipped silently.

  python3 tools/cicd/check_obl_versions.py          check the tree
  python3 tools/cicd/check_obl_versions.py --test   negative controls
"""
import glob
import os
import re
import struct
import sys
import zlib


def repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(here, "..", ".."))


def read_ver_num(root):
    path = os.path.join(root, "core", "shared", "version.h")
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    m = re.search(r"^\s*#\s*define\s+VER_NUM\s+(\d+)\s*$", text, re.M)
    if not m:
        raise ValueError("no '#define VER_NUM <number>' in " + path)
    return int(m.group(1))


def library_version(data):
    """The VER_NUM a library's bytes carry. Raises ValueError when they are not
    a library at all."""
    if len(data) < 6:
        raise ValueError("only %d bytes" % len(data))
    zlib_at_4 = data[4] == 0x78 and (0x78 * 256 + data[5]) % 31 == 0
    src = data if (data[0] == 0x78 and not zlib_at_4) else data[4:]
    try:
        payload = zlib.decompress(src)
    except zlib.error as e:
        raise ValueError("will not inflate (%s)" % e)
    if len(payload) < 8:
        raise ValueError("inflates to only %d bytes" % len(payload))
    return struct.unpack_from("<i", payload, 0)[0]


def problems(root):
    """One line per problem, for every core/lib/*.obl under root."""
    bad = []
    try:
        want = read_ver_num(root)
    except (OSError, ValueError) as e:
        return ["cannot read the toolchain version: %s" % e]
    files = sorted(glob.glob(os.path.join(root, "core", "lib", "*.obl")))
    if not files:
        return ["no core/lib/*.obl found under %s" % root]
    for path in files:
        name = os.path.basename(path)
        try:
            with open(path, "rb") as f:
                data = f.read()
            got = library_version(data)
        except (OSError, ValueError) as e:
            bad.append("%s: unreadable: %s" % (name, e))
            continue
        if got != want:
            bad.append("%s: built by toolchain %d, version.h says %d" % (name, got, want))
    return bad


def main():
    root = repo_root()
    bad = problems(root)
    if bad:
        for line in bad:
            print("FAIL " + line)
        print("\nRebuild and commit the libraries named above with this toolchain "
              "(see the version-bump checklist: lang.obl and the rest of core/lib).")
        return 1
    count = len(glob.glob(os.path.join(root, "core", "lib", "*.obl")))
    print("ok: %d libraries carry toolchain %d" % (count, read_ver_num(root)))
    return 0


# ---------------------------------------------------------------------------
# Negative controls: a checker that cannot fail proves nothing.
# ---------------------------------------------------------------------------

def _blob(ver, size_header=True, magic=0xffb9, body=b"x" * 64):
    payload = struct.pack("<ii", ver, magic) + body
    comp = zlib.compress(payload, 9)
    return (struct.pack("<I", len(payload)) + comp) if size_header else comp


def _tree(files, version_h="#define VER_NUM 2026103\n"):
    import tempfile
    root = tempfile.mkdtemp(prefix="oblver-")
    os.makedirs(os.path.join(root, "core", "shared"))
    os.makedirs(os.path.join(root, "core", "lib"))
    if version_h is not None:
        with open(os.path.join(root, "core", "shared", "version.h"), "w") as f:
            f.write(version_h)
    for name, data in files.items():
        with open(os.path.join(root, "core", "lib", name), "wb") as f:
            f.write(data)
    return root


def _test():
    import shutil
    good = 2026103
    failures = 0

    def case(label, files, expect_fail, version_h="#define VER_NUM 2026103\n", needle=None):
        nonlocal failures
        root = _tree(files, version_h)
        try:
            bad = problems(root)
        finally:
            shutil.rmtree(root, ignore_errors=True)
        ok = bool(bad) == expect_fail and (needle is None or any(needle in b for b in bad))
        print("  %-4s %s%s" % ("ok" if ok else "FAIL", label, "" if ok else "  -> %r" % bad))
        if not ok:
            failures += 1

    case("matching library passes", {"a.obl": _blob(good)}, False)
    case("older-format (no size header) matching library passes",
         {"a.obl": _blob(good, size_header=False)}, False)
    # size ends in 0x78 and byte 4 is a zlib header: the disambiguation rule must
    # still pick the size-header format
    padded = None
    for n in range(0, 600):
        b = _blob(good, body=b"y" * n)
        if b[0] == 0x78:
            padded = b
            break
    if padded is not None:
        case("size whose low byte is 0x78 still parses", {"a.obl": padded}, False)
    else:
        print("  FAIL could not build a size-0x78 control")
        failures += 1
    case("one stale library among good ones fails, and is named",
         {"a.obl": _blob(good), "stale.obl": _blob(good - 1), "b.obl": _blob(good)},
         True, needle="stale.obl: built by toolchain %d" % (good - 1))
    case("newer library fails", {"a.obl": _blob(good + 1)}, True, needle="built by toolchain")
    case("older-format stale library fails", {"a.obl": _blob(good - 1, size_header=False)}, True)
    case("garbage fails", {"a.obl": b"this is not a library at all"}, True, needle="unreadable")
    case("truncated zlib stream fails", {"a.obl": _blob(good)[:-12]}, True, needle="unreadable")
    case("empty file fails", {"a.obl": b""}, True, needle="unreadable")
    case("header-only file fails", {"a.obl": struct.pack("<I", 1) + zlib.compress(b"")}, True,
         needle="unreadable")
    case("no libraries fails", {}, True, needle="no core/lib/*.obl")
    case("version.h without VER_NUM fails", {"a.obl": _blob(good)}, True,
         version_h="#define VERSION_STRING L\"1\"\n", needle="cannot read the toolchain version")
    case("missing version.h fails", {"a.obl": _blob(good)}, True, version_h=None,
         needle="cannot read the toolchain version")

    live = problems(repo_root())
    print("  %-4s the repository itself is clean%s"
          % ("ok" if not live else "FAIL", "" if not live else " (%s)" % "; ".join(live)))
    if live:
        failures += 1

    print("")
    print("PASS" if not failures else "FAIL: %d" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    if "--test" in sys.argv:
        raise SystemExit(_test())
    raise SystemExit(main())
