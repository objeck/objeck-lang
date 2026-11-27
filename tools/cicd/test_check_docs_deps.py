#!/usr/bin/env python3
"""check_docs_deps.py --portable must refuse a core binary that is not an executable ELF for the target."""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "check_docs_deps.py")
CORE = ("obc", "obr", "obd", "obi", "obu", "obb")


def elf_header(machine):
    """The 20 bytes of an ELF header up to e_machine: 64-bit, little-endian."""
    return b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8) + (3).to_bytes(2, "little") + machine.to_bytes(2, "little")


def portable(tree, arch="x64"):
    r = subprocess.run([sys.executable, "-I", SCRIPT, "--portable", tree, "--platform", "linux-" + arch],
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


class CoreBinaries(unittest.TestCase):
    def tree(self, tmp, files):
        os.makedirs(os.path.join(tmp, "bin"))
        for name, data in files.items():
            p = os.path.join(tmp, "bin", name)
            with open(p, "wb") as f:
                f.write(data)
            os.chmod(p, 0o755)

    def test_text_placeholder_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.tree(tmp, {n: b"#!/bin/sh\necho placeholder\n" for n in CORE})
            rc, out = portable(tmp)
            self.assertNotEqual(rc, 0, out)
            for n in CORE:
                self.assertIn("bin/%s is not an ELF file" % n, out)

    def test_one_placeholder_among_elf_files_is_named(self):
        with tempfile.TemporaryDirectory() as tmp:
            files = {n: elf_header(62) for n in CORE}
            files["obi"] = b"not a binary\n"
            self.tree(tmp, files)
            rc, out = portable(tmp)
            self.assertNotEqual(rc, 0, out)
            self.assertIn("bin/obi is not an ELF file", out)
            self.assertNotIn("bin/obc is not an ELF file", out)

    def test_wrong_architecture_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.tree(tmp, {n: elf_header(183) for n in CORE})  # AArch64 files in an x64 tree
            rc, out = portable(tmp, "x64")
            self.assertNotEqual(rc, 0, out)
            self.assertIn("bin/obc is an ELF file for machine 183, not x86-64 (62)", out)

    @unittest.skipIf(os.name == "nt", "the executable bit does not exist on Windows")
    def test_not_executable_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.tree(tmp, {n: elf_header(62) for n in CORE})
            os.chmod(os.path.join(tmp, "bin", "obb"), 0o644)
            rc, out = portable(tmp)
            self.assertIn("bin/obb is not executable", out)

    def test_missing_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.tree(tmp, {n: elf_header(62) for n in CORE if n != "obu"})
            rc, out = portable(tmp)
            self.assertIn("bin/obu is missing", out)


if __name__ == "__main__":
    unittest.main()
