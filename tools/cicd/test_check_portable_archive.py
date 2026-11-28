#!/usr/bin/env python3
"""tools/packaging/check_portable_archive.sh must tie the portable gate to the archive that ships:
it unpacks the .tgz into a fresh tree, runs check_docs_deps.py --portable on THAT, prints the
archive's sha256, and fails otherwise. Negative controls only: a passing archive needs a real
release build (the ELF files, sonames and symbol floors), which is the gate's own job.
Also checks that test_portable_linux.sh runs it first and records the same hash."""
import hashlib
import io
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "packaging", "check_portable_archive.sh")
SMOKE = os.path.join(HERE, "..", "packaging", "test_portable_linux.sh")
CORE = ("obc", "obr", "obd", "obi", "obu", "obb")
BASH = shutil.which("bash")


def make_archive(path, files, top="objeck-lang"):
    with tarfile.open(path, "w:gz") as t:
        for name, data in files.items():
            info = tarfile.TarInfo("%s/%s" % (top, name) if top else name)
            info.size = len(data)
            info.mode = 0o755
            t.addfile(info, io.BytesIO(data))


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def run(*args):
    env = dict(os.environ, PYTHON=sys.executable)
    r = subprocess.run([BASH, SCRIPT] + list(args), capture_output=True, text=True, env=env)
    return r.returncode, r.stdout + r.stderr


@unittest.skipIf(BASH is None, "needs bash")
class CheckPortableArchive(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def archive(self, files, name="objeck-linux-x64_2099.1.0.tgz", top="objeck-lang"):
        p = os.path.join(self.tmp.name, name)
        make_archive(p, files, top)
        return p

    def test_text_placeholder_archive_fails_and_prints_its_hash(self):
        tgz = self.archive({"bin/" + n: b"#!/bin/sh\necho placeholder\n" for n in CORE})
        rc, out = run(tgz, "x64")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("archive_sha256=" + sha(tgz), out)
        self.assertIn("bin/obi is not an ELF file", out)  # found in the UNPACKED archive, not in a build tree
        self.assertNotIn("PASS portable check", out)

    def test_hash_goes_to_the_file_asked_for(self):
        tgz = self.archive({"bin/obc": b"x"})
        sha_file = os.path.join(self.tmp.name, "archive.sha256")
        run("--sha256-out", sha_file, tgz, "x64")
        with open(sha_file) as f:
            self.assertEqual(f.read().strip(), sha(tgz))

    def test_missing_archive_fails(self):
        rc, out = run(os.path.join(self.tmp.name, "nope.tgz"), "x64")
        self.assertNotEqual(rc, 0)
        self.assertIn("no such archive", out)

    def test_empty_archive_fails(self):
        p = os.path.join(self.tmp.name, "empty.tgz")
        open(p, "w").close()
        rc, out = run(p, "x64")
        self.assertNotEqual(rc, 0)
        self.assertIn("is empty", out)

    def test_not_a_tarball_fails(self):
        p = os.path.join(self.tmp.name, "junk.tgz")
        with open(p, "wb") as f:
            f.write(b"this is not gzip data")
        rc, out = run(p, "x64")
        self.assertNotEqual(rc, 0)
        self.assertIn("not a readable .tgz", out)

    def test_archive_without_the_top_directory_fails(self):
        tgz = self.archive({"bin/obc": b"x"}, top="somewhere-else")
        rc, out = run(tgz, "x64")
        self.assertNotEqual(rc, 0)
        self.assertIn("no top-level objeck-lang/ directory", out)

    def test_unknown_architecture_fails(self):
        tgz = self.archive({"bin/obc": b"x"})
        rc, out = run(tgz, "sparc")
        self.assertNotEqual(rc, 0)
        self.assertIn("unknown architecture", out)


class SmokeIsBound(unittest.TestCase):
    def test_the_distro_smoke_runs_the_gate_first_and_records_the_hash(self):
        with open(SMOKE, encoding="utf-8") as f:
            text = f.read()
        gate = text.index("check_portable_archive.sh\" --sha256-out")
        unpack = text.index('tar -xzf "$TGZ" -C "$WORK/pkg"')
        self.assertLess(gate, unpack, "the gate must run before the archive is unpacked for the smoke")
        self.assertIn("ARCHIVE_SHA256", text)
        final = [l for l in text.splitlines() if l.startswith('echo "PASS ($MODE)')]
        self.assertEqual(len(final), 1)
        self.assertIn("$ARCHIVE_SHA256", final[0], "the final PASS line must carry the archive hash")


if __name__ == "__main__":
    unittest.main()
