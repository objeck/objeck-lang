# Vendored: TweetNaCl

`obu` verifies a detached Ed25519 signature on `SHA256SUMS`
([`docs/release_integrity.md`](../../../../docs/release_integrity.md), #723). This
is the implementation it uses.

## Provenance

| | |
|---|---|
| upstream | <https://tweetnacl.cr.yp.to/20140427/> |
| authors | Bernstein, Janssen, Lange, Schwabe |
| licence | public domain |
| version | 20140427 |
| retrieved | 2026-10-09 |

```
02e65bc3013ff2168983365e55906bc783c4c7e0a60d8100f17bb303a17175c4  tweetnacl.c
43f29ad721d9927b747b0100ab4160c119e7bb180c7c98a66e4bf79d31244287  tweetnacl.h
```

**Unmodified.** Both files are byte-for-byte as retrieved. Anything `obu` needs
on top of them — the `randombytes` stub, base64, the minisign format — lives in
`obu.cpp`, so this directory stays diffable against upstream.

## Why it was not written here

A signature verifier that always returns true is a silent, total failure: the
feature looks present and protects nothing, which is exactly what #723 exists to
prevent. So the primitive comes from a known, reviewed implementation rather than
from this project.

One thing made that cheaper than it usually is: this is verification with a
**public** key. There is no secret to leak, so constant-time behaviour is not a
requirement — which is normally the hard part of vendoring crypto.

## How the download was checked

Not by trusting one fetch. `tweetnacl.h` was retrieved independently from
`dominictarr/tweetnacl` on GitHub and is **byte-identical** to the canonical
copy — same SHA-256.

`tweetnacl.c` differed between the two, and the entire difference is that fork's
own edits: it comments out the `extern void randombytes(...)` declaration and the
two functions that call it (`crypto_box_keypair`, `crypto_sign_keypair`).
Stripping comments and whitespace from both shows every byte of code the fork
retained is identical to canonical, which is a strict superset. So the code is
corroborated by two independent sources, and the canonical copy is the one here.

## `randombytes`

Upstream declares `extern void randombytes(u8*, u64)` and uses it in exactly two
places, both key generation: `crypto_box_keypair` and `crypto_sign_keypair`.
`obu` generates no keys, so it supplies a stub that **aborts**. If a future change
ever reaches a keypair function, it fails loudly rather than producing a key from
whatever the stub left in the buffer.

## What `obu` calls

Only `crypto_sign_open` — Ed25519 verification — and `crypto_hash` (SHA-512)
beneath it, which TweetNaCl provides. Nothing else is referenced.

Releases are signed with `minisign -S -l`, the legacy format, so the signature is
over the raw manifest. The default prehashed mode (`ED`) would have additionally
required BLAKE2b-512, and the only candidate for that was hand-written code in
the trust path. See the correction in `release_integrity.md`.

## Upgrading

TweetNaCl 20140427 is the final release; upstream has not changed since. If it
ever does, replace both files wholesale, update the hashes above, and re-run
`tools/cicd/test_obu_verify.py` — which holds the verifier to behaviour against
`minisign` itself, accept and reject cases alike.
