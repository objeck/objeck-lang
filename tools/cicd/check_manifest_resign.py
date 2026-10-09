#!/usr/bin/env python3
"""Anything that replaces a published SHA256SUMS must also have it re-signed.

WHY THIS EXISTS
---------------
`obu` refuses a manifest whose Ed25519 signature does not verify (#723 phase 3,
docs/release_integrity.md). That makes the signature part of the manifest: a
tool that replaces `SHA256SUMS` on a published release and leaves
`SHA256SUMS.minisig` alone does not produce a release with weaker integrity --
it produces one that `obu update` refuses outright, for every user.

Two tools already did exactly that, because Windows MSI signing happens after
publication and rewrites the installer bytes:

  * tools/cicd/update_sha256sums.ps1, called by sign_release.cmd
  * tools/cicd/post_release.sh, when its own gate finds the manifest stale

Both now request `.github/workflows/resign-manifest.yml`. This guard exists so
the third one is a failed build rather than a release nobody can install -- the
same reason check_native_write_barrier.py exists for native stores.

WHAT IT CHECKS
--------------
Any tracked script or workflow that uploads `SHA256SUMS` as a release asset must
also mention `resign-manifest`, or sign the manifest itself in the same file.
That is deliberately coarse: it cannot tell a correct re-sign from an incorrect
one, and does not try. It catches the omission, which is the failure that
actually happened, and it names the file so a reviewer looks at the right place.

Usage: check_manifest_resign.py          (from anywhere in the tree)
       check_manifest_resign.py --test   (self-test)
"""

import os
import re
import subprocess
import sys

# An upload of the manifest to a release. Both `gh release upload <tag> SHA256SUMS`
# and PowerShell's `gh release upload $tag $manifest` have to match, and the
# latter names the file through a variable -- so the trigger is the command plus
# SHA256SUMS appearing anywhere in the file, narrowed by the checks below.
UPLOAD = re.compile(r"release\s+upload\b", re.IGNORECASE)

# What makes an upload acceptable: the file arranges for a signature over the
# manifest it just wrote, either by asking the workflow or by signing inline.
RESIGN = re.compile(r"resign-manifest|minisign\s+-S", re.IGNORECASE)

# The manifest has to be what is being uploaded. A file that uploads only MSIs
# is not this guard's business.
MANIFEST = re.compile(r"SHA256SUMS")

# release-publish.yml signs the manifest it generates in its own signing step,
# which RESIGN matches through `minisign -S`. Nothing is exempt by name: an
# exemption list is how this guard would quietly stop guarding.
LOOK_IN = ('.sh', '.ps1', '.cmd', '.bat', '.yml', '.yaml', '.py')


def repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    out = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                         cwd=here, capture_output=True, text=True)
    if out.returncode == 0 and out.stdout.strip():
        return out.stdout.strip()
    return os.path.abspath(os.path.join(here, "..", ".."))


def tracked_files(root):
    out = subprocess.run(["git", "ls-files"], cwd=root, capture_output=True, text=True)
    if out.returncode != 0:
        print("could not list tracked files: %s" % out.stderr.strip())
        sys.exit(2)
    return [f for f in out.stdout.splitlines() if f.endswith(LOOK_IN)]


def offenders(root, files):
    bad = []
    for rel in files:
        path = os.path.join(root, rel)
        try:
            with open(path, encoding="utf-8", errors="replace") as handle:
                text = handle.read()
        except OSError:
            continue
        if not (UPLOAD.search(text) and MANIFEST.search(text)):
            continue
        # The upload and the manifest both appear. Is the manifest actually one
        # of the things uploaded? Check the upload lines themselves, so a file
        # that uploads MSIs and merely reads SHA256SUMS is not flagged.
        uploads_manifest = False
        for line in text.splitlines():
            if not UPLOAD.search(line):
                continue
            if MANIFEST.search(line) or re.search(r"\$\w*manifest|\$\w*sums|\$sums\b",
                                                  line, re.IGNORECASE):
                uploads_manifest = True
                break
        if not uploads_manifest:
            continue
        if not RESIGN.search(text):
            bad.append(rel)
    return bad


def main():
    root = repo_root()
    files = tracked_files(root)
    bad = offenders(root, files)
    if bad:
        print("These replace a published SHA256SUMS without arranging for a signature over it:")
        for rel in bad:
            print("  %s" % rel)
        print("")
        print("obu REFUSES a manifest whose signature does not verify (#723 phase 3), so a")
        print("replaced manifest with its old signature is a release no obu will install.")
        print("Request the re-signing in the same place:")
        print("")
        print("  gh workflow run resign-manifest.yml -f version=<VERSION>")
        print("")
        print("See SIGNING.md and docs/release_integrity.md.")
        return 1

    scanned = [f for f in files if f.endswith(('.sh', '.ps1', '.yml'))]
    print("%d script(s)/workflow(s) scanned: every SHA256SUMS upload arranges for a "
          "signature over it." % len(scanned))
    return 0


# ----------------------------------------------------------------- self-test --
def _test():
    import tempfile

    cases = [
        ("uploads the manifest, no re-sign",
         'gh release upload "$TAG" SHA256SUMS --clobber\n', True),
        ("uploads the manifest and asks for the re-sign",
         'gh release upload "$TAG" SHA256SUMS --clobber\n'
         'gh workflow run resign-manifest.yml -f version="$V"\n', False),
        ("uploads the manifest and signs inline",
         'minisign -S -l -s key -m SHA256SUMS -x SHA256SUMS.minisig\n'
         'gh release upload "$TAG" SHA256SUMS SHA256SUMS.minisig\n', False),
        ("PowerShell, manifest named through a variable",
         '& $gh release upload $tag $manifest --clobber\n'
         '$manifest = Join-Path $work "SHA256SUMS"\n', True),
        ("PowerShell variable plus a re-sign",
         '& $gh release upload $tag $manifest --clobber\n'
         '$manifest = Join-Path $work "SHA256SUMS"\n'
         '& $gh workflow run resign-manifest.yml -f version=$Version\n', False),
        ("uploads MSIs only, reads the manifest",
         'cat SHA256SUMS\ngh release upload "$TAG" objeck.msi --clobber\n', False),
        ("mentions the manifest but uploads nothing",
         'sha256sum * > SHA256SUMS\n', False),
        ("downloads the manifest only",
         'gh release download "$TAG" --pattern SHA256SUMS\n', False),
    ]

    root = tempfile.mkdtemp()
    failures = 0
    for name, body, want_bad in cases:
        rel = "case.sh"
        with open(os.path.join(root, rel), "w", encoding="utf-8") as handle:
            handle.write(body)
        got_bad = offenders(root, [rel]) != []
        ok = got_bad == want_bad
        print("  %-4s %s" % ("ok" if ok else "FAIL", name))
        if not ok:
            print("       wanted flagged=%s, got flagged=%s" % (want_bad, got_bad))
            failures += 1

    # And against the real tree, which must be clean.
    real = repo_root()
    live = offenders(real, tracked_files(real))
    print("  %-4s the repository itself is clean%s"
          % ("ok" if not live else "FAIL", "" if not live else " (%s)" % ", ".join(live)))
    if live:
        failures += 1

    print("")
    print("PASS: %d cases" % len(cases) if not failures else "FAIL: %d" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    if "--test" in sys.argv:
        raise SystemExit(_test())
    raise SystemExit(main())
