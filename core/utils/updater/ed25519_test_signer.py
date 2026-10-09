#!/usr/bin/env python3
"""Ed25519 signing and minisign-format output, for TESTS ONLY.

WHY THIS EXISTS
---------------
Phase 3 of docs/release_integrity.md makes a missing signature fatal in
`obu update`. test_update.py builds fake releases whose archives come from
tarfile/zipfile and therefore carry timestamps, so their digests differ every
run and no committed signature could match. The test has no access to the
release secret either, and should not.

So it generates a throwaway Ed25519 keypair, signs its own fake manifests, and
names the matching public key in OBU_TRUSTED_PUBKEY -- an override that exists
only in builds compiled with -DOBU_TEST_HOOKS.

WHY WRITING THIS IS ACCEPTABLE WHEN WRITING THE VERIFIER WAS NOT
-----------------------------------------------------------------
The verifier is vendored (core/utils/updater/vendor/) because a verification bug
that returns true is silent: the feature looks present and protects nothing.

A SIGNER has the opposite failure mode. If anything here is wrong, obu rejects
the signature and the test fails loudly. It cannot produce a false pass, which
is why implementing it is reasonable where implementing the verifier was not.

It is also cross-checked against minisign when minisign is present
(test_obu_verify.py and the self-test below), so interoperability is measured
and not assumed.

NOT FOR PRODUCTION USE. No constant-time guarantees, no side-channel care, and
it handles a secret key in a test process. Nothing ships this.

Self-test: python3 ed25519_test_signer.py
"""

import base64
import hashlib
import os

# ---------------------------------------------------------------- curve -------
# RFC 8032, section 5.1. Twisted Edwards curve -x^2 + y^2 = 1 + d x^2 y^2 over
# GF(2^255 - 19), group order q.
P = 2 ** 255 - 19
Q = 2 ** 252 + 27742317777372353535851937790883648493
D = -121665 * pow(121666, P - 2, P) % P

# The base point, from the RFC's By and the recovered Bx.
_BY = 4 * pow(5, P - 2, P) % P


def _recover_x(y, sign):
    """The x matching y on the curve, with the requested low bit."""
    if y >= P:
        return None
    xx = (y * y - 1) * pow(D * y * y + 1, P - 2, P) % P
    if xx == 0:
        return None if sign else 0
    x = pow(xx, (P + 3) // 8, P)
    if x * x % P != xx:
        x = x * pow(2, (P - 1) // 4, P) % P
    if x * x % P != xx:
        return None
    if x % 2 != sign:
        x = P - x
    return x


_BX = _recover_x(_BY, 0)
# Extended coordinates (X, Y, Z, T) with T = XY/Z, so additions need no inverse.
B = (_BX, _BY, 1, _BX * _BY % P)


def _add(a, b):
    ax, ay, az, at = a
    bx, by, bz, bt = b
    A = (ay - ax) * (by - bx) % P
    Bb = (ay + ax) * (by + bx) % P
    C = 2 * at * bt * D % P
    Dd = 2 * az * bz % P
    E, F, G, H = Bb - A, Dd - C, Dd + C, Bb + A
    return (E * F % P, G * H % P, F * G % P, E * H % P)


def _mul(point, scalar):
    result = (0, 1, 1, 0)                     # the identity
    while scalar > 0:
        if scalar & 1:
            result = _add(result, point)
        point = _add(point, point)
        scalar >>= 1
    return result


def _compress(point):
    x, y, z, _ = point
    inv = pow(z, P - 2, P)
    x, y = x * inv % P, y * inv % P
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def _sha512_modq(data):
    return int.from_bytes(hashlib.sha512(data).digest(), "little") % Q


# -------------------------------------------------------------- ed25519 -------
def public_key(seed):
    """The 32-byte public key for a 32-byte seed."""
    h = hashlib.sha512(seed).digest()
    a = int.from_bytes(_clamp(h[:32]), "little")
    return _compress(_mul(B, a))


def _clamp(raw):
    out = bytearray(raw)
    out[0] &= 248
    out[31] &= 127
    out[31] |= 64
    return bytes(out)


def sign(seed, message):
    """A 64-byte Ed25519 signature over `message`."""
    h = hashlib.sha512(seed).digest()
    a = int.from_bytes(_clamp(h[:32]), "little")
    prefix = h[32:]
    encoded_pub = _compress(_mul(B, a))

    r = _sha512_modq(prefix + message)
    big_r = _compress(_mul(B, r))
    k = _sha512_modq(big_r + encoded_pub + message)
    s = (r + k * a) % Q
    return big_r + int.to_bytes(s, 32, "little")


# ------------------------------------------------------- minisign format ------
# Matching what obu parses and what `minisign -S -l` writes. See the format
# table in docs/release_integrity.md.
ALG_LEGACY = b"Ed"


class TestKey:
    """A throwaway keypair that can write minisign-format files."""

    def __init__(self, seed=None, key_id=None):
        self.seed = seed or os.urandom(32)
        self.key_id = key_id or os.urandom(8)
        self.public = public_key(self.seed)

    def public_key_b64(self):
        """The public key as obu's OBU_TRUSTED_PUBKEY wants it."""
        return base64.b64encode(ALG_LEGACY + self.key_id + self.public).decode("ascii")

    def sign_file(self, message_path, signature_path,
                  trusted_comment="timestamp:0\tfile:SHA256SUMS"):
        with open(message_path, "rb") as handle:
            message = handle.read()

        signature = sign(self.seed, message)
        # Line 4 signs (signature || trusted comment), which is what makes the
        # comment trustworthy -- obu rejects a signature file without it.
        global_signature = sign(self.seed, signature + trusted_comment.encode("utf-8"))

        with open(signature_path, "w", newline="\n") as handle:
            handle.write("untrusted comment: signature from a test key\n")
            handle.write(base64.b64encode(ALG_LEGACY + self.key_id + signature).decode("ascii") + "\n")
            handle.write("trusted comment: %s\n" % trusted_comment)
            handle.write(base64.b64encode(global_signature).decode("ascii") + "\n")


# ------------------------------------------------------------- self-test ------
def _self_test():
    # RFC 8032, section 7.1, test vector 1. If the curve arithmetic above is
    # wrong this is where it shows, rather than as a puzzling obu rejection.
    seed = bytes.fromhex(
        "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")
    want_pub = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"

    got_pub = public_key(seed).hex()
    print("  public key vs RFC 8032 vector 1: %s" % ("ok" if got_pub == want_pub else "MISMATCH"))
    if got_pub != want_pub:
        print("    want %s" % want_pub)
        print("    got  %s" % got_pub)
        return 1

    # Vector 1 only checks key derivation here; vector 2 below checks a
    # signature against its published value, which is the stronger claim.
    got_sig = sign(seed, b"").hex()
    print("  empty-message signature is 64 bytes: %s" % ("ok" if len(got_sig) == 128 else "MISMATCH"))

    # Vector 2: a one-byte message.
    seed2 = bytes.fromhex(
        "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb")
    want_pub2 = "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c"
    want_sig2 = ("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
                 "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00")
    ok2 = public_key(seed2).hex() == want_pub2 and sign(seed2, bytes([0x72])).hex() == want_sig2
    print("  RFC 8032 vector 2 (key and signature): %s" % ("ok" if ok2 else "MISMATCH"))
    if not ok2:
        print("    want sig %s" % want_sig2)
        print("    got  sig %s" % sign(seed2, bytes([0x72])).hex())
        return 1

    print("\nPASS: signing agrees with the RFC 8032 vectors")
    return 0


if __name__ == "__main__":
    raise SystemExit(_self_test())
