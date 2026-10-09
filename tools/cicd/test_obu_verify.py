#!/usr/bin/env python3
"""`obu verify` must accept a real signature and reject every way one can be wrong.

Usage: test_obu_verify.py <path-to-obu>

WHY BOTH DIRECTIONS ARE HERE
----------------------------
A signature verifier that always returns true is a silent, total failure: the
feature looks present and protects nothing, which is the exact outcome
docs/release_integrity.md exists to prevent. So every reject case below is the
point.

But a suite of reject cases alone passes against `return false;`, which fails
closed and is merely useless. The accept case is what rules that out, and it is
the reason this uses a COMMITTED FIXTURE rather than a freshly generated
keypair: obu trusts the key compiled into it, so only a signature from the real
release key can ever be accepted.

The fixtures under fixtures/obu_verify/ were signed once with the release secret
and carry no secret themselves. A signature is public data. So this test needs
no access to the signing key, no minisign installed, and runs anywhere.

WHAT IS AND IS NOT ESTABLISHED
------------------------------
That obu interoperates with real minisign output and that its reject paths
reject. It does NOT audit the Ed25519 implementation -- that is vendored
unmodified from tweetnacl.cr.yp.to precisely so it need not be audited here; see
core/utils/updater/vendor/README.md for provenance and how the download was
corroborated against an independent source.
"""

import argparse
import base64
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "fixtures", "obu_verify")

# The bytes the committed SHA256SUMS lists a hash for. Written at test time
# rather than committed, so the fixture directory holds only text.
PAYLOAD = b"objeck obu verify fixture; content is irrelevant, its hash is not\n"
PAYLOAD_NAME = "obu-verify-fixture.bin"

failures = []


def flip_in_base64(text, index):
    """Flip one bit of the DECODED bytes and re-encode, preserving the length.

    Mutating base64 characters directly can change the decoded size, and a
    size check would then reject the input before any signature maths runs.
    """
    raw = bytearray(base64.b64decode(text))
    raw[index] ^= 0x01
    return base64.b64encode(bytes(raw)).decode("ascii")


def run(argv):
    done = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return done.returncode, done.stdout.decode("utf-8", "replace")


def indent(text):
    return "\n".join("    " + line for line in text.strip().splitlines())


def check(label, obu, archive, sums, expect_ok, must_mention=()):
    code, output = run([obu, "verify", archive, sums])
    if (code == 0) != expect_ok:
        failures.append("%s: expected %s, got exit %d\n%s"
                        % (label, "accept" if expect_ok else "REJECT", code, indent(output)))
        return
    for needle in must_mention:
        if needle.lower() not in output.lower():
            failures.append("%s: behaved correctly but never mentions %r.\n"
                            "A rejection a user cannot act on is barely a rejection.\n%s"
                            % (label, needle, indent(output)))
            return
    print("  ok    %s" % label)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("obu")
    args = parser.parse_args()

    obu = os.path.abspath(args.obu)
    if not os.path.isfile(obu):
        print("Error: no obu at %s" % obu, file=sys.stderr)
        return 1

    for name in ("SHA256SUMS", "SHA256SUMS.minisig", "SHA256SUMS.prehashed.minisig"):
        if not os.path.isfile(os.path.join(FIXTURES, name)):
            print("Error: missing fixture %s" % name, file=sys.stderr)
            return 1

    committed_sums = open(os.path.join(FIXTURES, "SHA256SUMS"), encoding="utf-8").read()
    expected_digest = hashlib.sha256(PAYLOAD).hexdigest()
    if expected_digest not in committed_sums:
        print("Error: the fixture manifest does not list this payload's hash.\n"
              "       PAYLOAD here and fixtures/obu_verify/SHA256SUMS disagree, so the\n"
              "       accept case would fail for a reason unrelated to signatures.",
              file=sys.stderr)
        return 1

    work = tempfile.mkdtemp(prefix="obuverify_")
    try:
        archive = os.path.join(work, PAYLOAD_NAME)
        with open(archive, "wb") as fh:
            fh.write(PAYLOAD)

        sums = os.path.join(work, "SHA256SUMS")
        sig = sums + ".minisig"

        def place(sig_source="SHA256SUMS.minisig", sig_lines=None, sums_text=None):
            with open(sums, "w", newline="") as fh:
                fh.write(sums_text if sums_text is not None else committed_sums)
            if sig_lines is None:
                shutil.copyfile(os.path.join(FIXTURES, sig_source), sig)
            else:
                with open(sig, "w", newline="\n") as fh:
                    fh.write("\n".join(sig_lines) + "\n")

        original = open(os.path.join(FIXTURES, "SHA256SUMS.minisig"),
                        encoding="utf-8").read().splitlines()

        # ---- the accept case, which rules out a verifier that fails closed ----
        place()
        check("a real signature from the release key is ACCEPTED",
              obu, archive, sums, True, must_mention=("signature verified",))

        # ---- and every way one can be wrong ----------------------------------
        place()
        os.remove(sig)
        check("a missing signature is rejected, not treated as unsigned-but-fine",
              obu, archive, sums, False, must_mention=("no signature",))

        place(sig_lines=original[:2])
        check("a signature with no global signature is rejected",
              obu, archive, sums, False, must_mention=("global signature",))

        # Flip a byte INSIDE the decoded signature and re-encode, so the length
        # is unchanged and the cryptographic check is what has to reject it.
        # Mutating the base64 text instead changed the decoded length, and obu
        # then rejected on size before reaching Ed25519 at all -- which would
        # have left the actual verification untested.
        place(sig_lines=[original[0], flip_in_base64(original[1], -1)] + original[2:])
        check("a tampered signature is rejected by the cryptographic check",
              obu, archive, sums, False, must_mention=("does not match the manifest",))

        place(sig_lines=[original[0], "not base64 at all !!!"] + original[2:])
        check("a signature that is not base64 is rejected",
              obu, archive, sums, False, must_mention=("could not be parsed",))

        place(sig_lines=[original[0], "QQ=="] + original[2:])
        check("a truncated signature is rejected",
              obu, archive, sums, False, must_mention=("bytes",))

        place(sig_lines=original[:3] + [flip_in_base64(original[3], -1)])
        check("a tampered global signature is rejected by the cryptographic check",
              obu, archive, sums, False, must_mention=("trusted comment's signature",))

        place(sig_lines=[original[0], original[1],
                         "trusted comment: timestamp:1\tfile:SOMETHING-ELSE", original[3]])
        check("a tampered trusted comment is rejected, so it cannot lie about the file",
              obu, archive, sums, False, must_mention=("trusted comment",))

        # Prehashed is minisign's DEFAULT, so obu meeting one is likely rather
        # than hypothetical. It must say it cannot check that form rather than
        # mishandle it -- the signature here is genuine, from the real key.
        place(sig_source="SHA256SUMS.prehashed.minisig")
        check("a genuine but PREHASHED signature is refused by name, not mishandled",
              obu, archive, sums, False, must_mention=("algorithm",))

        # The threat this whole feature closes: the manifest replaced to match a
        # substituted asset. The signature no longer covers it.
        place(sums_text="%s  %s\n" % ("0" * 64, PAYLOAD_NAME))
        check("a substituted manifest is rejected -- the threat #723 closes",
              obu, archive, sums, False, must_mention=("does not match",))

    finally:
        shutil.rmtree(work, ignore_errors=True)

    if failures:
        print()
        for item in failures:
            print("FAIL: %s" % item)
        print()
        print("%d case(s) did not behave as stated." % len(failures))
        return 1

    print()
    print("PASS: obu verify accepts a real signature and rejects a missing, tampered,")
    print("      truncated, unparseable and prehashed one, a tampered trusted comment,")
    print("      and a substituted manifest.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
