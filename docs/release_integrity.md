# Release integrity: what is verified today, and the signed manifest that would close the gap

> **Status, checked 2026-11-18.** The signed manifest this document designs is built: phases 1 to 4 below are marked done, and the repository shows each piece. `release-publish.yml` signs `SHA256SUMS` with `minisign -S -l` and uploads `SHA256SUMS.minisig`; `resign-manifest.yml` signs it again after the Windows installers are re-signed locally; `obu update` verifies the signature against the key compiled into it before trusting a line of the manifest and `obu verify` runs the same check on a downloaded archive (`core/utils/updater/obu.cpp`); `core/release/objeck-release.pub` (113 bytes, sha256 `864c6a99...7a6c04`) is the committed key; `tools/cicd/test_obu_verify.py` holds the verifier to behaviour against a fixture signed with the real key; and `verify-signing-credentials.yml` runs weekly jobs (`manifest-key`, `published-key`) that check the stored secret still matches the committed key and that objeck.org still serves the same bytes. The sections "What exists" and "What is not covered" below describe the state **before** the signature and are kept as the problem statement; read the Phases section for what is current. The "Limits" section at the end lists what even the signed manifest does not establish.

## What exists (before the signed manifest)

| Layer | Mechanism | Verified by |
|---|---|---|
| Windows installers | Authenticode signature (SafeNet eToken, signed locally after publish by `tools/cicd/sign_release.cmd`) | `Get-AuthenticodeSignature`; the release body tells the user to check it |
| macOS package | Developer ID signature + notarization, in CI | Gatekeeper |
| Every asset | `SHA256SUMS`, regenerated after signing (PR #693) | `sha256sum -c`; `obu update` computes SHA-256 itself and refuses an asset that does not match the manifest |
| Repository | `secret-scan.yml` (gitleaks), CodeQL (`codeql.yml`), GitGuardian (an external service: no configuration for it is in this repository, so its presence is not verified here) | CI |

So a corrupted or truncated download is caught, and a Windows or macOS installer that was tampered with fails its platform's signature check.

## What is not covered

`SHA256SUMS` is served from the same place as the assets it describes. Anyone who can replace a release asset on GitHub — a compromised maintainer account, a compromised Actions token with `contents: write` — can replace the manifest to match. `obu` then verifies the substituted archive against the substituted manifest and reports success. The Linux `.tgz` and the macOS `.tgz`/`.zip` have no platform signature at all, so for those the manifest is the only integrity claim, and it is unsigned.

There is also no load-time integrity: `obr` executes any `.obe`, links any `.obl` and `LoadLibrary`s any `libobjk_*` it finds under `lib/`. That is by design for a development toolchain (users compile their own libraries) and is out of scope here; integrity is a property of the *distribution*, checked once at install or update time.

## Design: a detached signature on the manifest

**Key.** One Ed25519 keypair (minisign format: small, no PKI, no expiry ceremony). The public key is committed to the repository and compiled into `obu` (`core/utils/updater/obu.cpp`) as a string constant; the secret key lives only in a GitHub Actions secret (`MINISIGN_SECRET_KEY`) and, as a fallback, in the maintainer's password manager. The public key is also published on objeck.org and in `README.md` so a user can check it out of band.

**Signing.** `release-publish.yml`, after "Generate SHA256SUMS" (which already runs after signing so the manifest describes the signed bytes), signs `SHA256SUMS` with `minisign -S` and uploads `SHA256SUMS.minisig` beside it. If the secret is absent the step *fails* unless `sig` is declared in `RELEASE_MANUAL_STEPS` — the same rule every other publish step follows since #692, so a missing key is a red job, not a silent skip. `tools/cicd/check_release_config.sh` learns the new secret and the new manual token.

**Verification in `obu`.** `obu update` downloads `SHA256SUMS` and `SHA256SUMS.minisig`, verifies the signature against the compiled-in key *before* trusting any line of the manifest, then proceeds as today. A missing `.minisig` is a hard failure once the key has shipped in an `obu` — the transition rule below covers the release that introduces it. `obu verify` is a new subcommand that runs the same check against an already-downloaded archive and manifest, so a user who fetched with `curl` can verify without installing.

Ed25519 verification is ~600 lines of dependency-free C (the reference or TweetNaCl implementation); `obu` already carries its own SHA-256 for the same reason (no OpenSSL at update time), so this keeps the updater self-contained.

**Correction, 2026-10-09, from the format minisign actually produced.** The
paragraph above under-counts, because it assumed the legacy signing mode. The
signature this project's key produces carries algorithm **`ED`**, not `Ed`:

```
untrusted comment: signature from minisign secret key
RUS5pn58scY3gpNFQt8L9neY1mGAAGb1G1aJTgsgr1tWvGkkussfxXeBguwgYz246qO1CrDcSJFPbWJ1+NIU2VXlwFkgVDANZQo=
trusted comment: timestamp:1791564238	file:SHA256SUMS	hashed
gk3z1ebiIK2ch68JeOlRZ/EW1mwCpVJjDwNv49CQPhy+AYClWg85cBSil0Xu9CDCHKuOpPGtCEPBFVEulFw6BA==
```

| field | bytes | layout |
|---|---|---|
| public key | 42 | `Ed` + 8-byte key id + 32-byte key |
| signature (line 2) | 74 | `ED` prehashed, or **`Ed` legacy with `-l`** — then key id + 64-byte signature |
| global signature (line 4) | 64 | Ed25519 over (signature ‖ trusted comment) |

`ED` is the **prehashed** mode, minisign's default since 0.6 and what 0.12
produces: the signature is over **BLAKE2b-512 of the message**, not the message.
The trusted comment's trailing `hashed` is minisign's own marker for it.

**Superseded within the hour by reading minisign's own options.** It has `-l`,
"sign using the legacy format", which produces algorithm `Ed`: the signature is
over the raw message, there is no BLAKE2b, and the trusted comment loses its
`hashed` marker. A modern minisign still verifies such a signature — checked,
not assumed: `Signature and comment signature verified`, exit 0.

**So the signing step uses `-l` and `obu` needs only Ed25519 after all.**

That is worth the trade. Prehashing exists so a signer can stream a large file
without holding it in memory; `SHA256SUMS` is a few kilobytes, so it buys
nothing here. Against that it would have put a second hand-written crypto
primitive inside the trust path, and Ed25519 already hashes internally with
SHA-512 — which TweetNaCl supplies — so legacy mode is not weaker for a small
file, merely older.

One primitive, from a known implementation, is a smaller attack surface than two
where one is ours.

And verifying line 4 as well as line 2 is not optional: without it the trusted
comment, which names the file the signature is for, is unauthenticated — a
signature for one file could be presented with a comment claiming another.

**Where the primitives come from is a supply-chain decision, not an
implementation detail.** A verification bug that always *returns true* is silent
and total: the feature would look present while protecting nothing, which is
precisely the failure this document exists to prevent. So the Ed25519 code should
be a known, unmodified public-domain implementation (TweetNaCl's
`crypto_sign_open` is the usual choice — public domain, single file, widely
reviewed) rather than written here.

One thing in our favour: this is *verification with a public key*. There is no
secret to leak, so constant-time behaviour is not a requirement, which is what
usually makes vendoring crypto delicate.

Whatever the source, it must be held to behaviour rather than inspection, with
minisign itself as the oracle: a valid signature accepted, and a tampered
message, a tampered signature, a wrong key, a truncated signature and a missing
line 4 each rejected. A test that only checks the accept case cannot tell a
working verifier from one that always returns true.

**Key rotation.** A new key is introduced by shipping an `obu` that trusts both the old and new keys for one release, then dropping the old one. Rotation, like the eToken certificate's renewal, is a documented manual step with a date in [`SIGNING.md`](../SIGNING.md).

## Phases

1. ~~**This document; `obu verify` reads `SHA256SUMS` without a signature**~~ **DONE** (already true for `update`; `verify` exposes it). No new secrets.
2. ~~**Generate the keypair (maintainer, locally), commit the public key, add the signing step and the `sig` manual token.**~~ **DONE, 2026-10-09.** The release that ships this has a signed manifest but an `obu` that does not yet require it.
3. ~~**`obu` verifies the signature; missing or invalid is fatal.**~~ **DONE, 2026-10-09.** From here on a substituted manifest is caught. Ed25519 comes from TweetNaCl, vendored unmodified ([`core/utils/updater/vendor/README.md`](../core/utils/updater/vendor/README.md)); `tools/cicd/test_obu_verify.py` holds it to behaviour against a committed fixture signed with the real key, in both directions.
4. **Publish the public key out of band** (objeck.org, README) and add a `verify-signing-credentials.yml`-style check that the secret key matches the committed public key, on the same weekly schedule. **README and the weekly check: DONE, 2026-10-09.** `verify-signing-credentials.yml` gained a `manifest-key` job that signs a canary with the stored secret and verifies it against the committed `core/release/objeck-release.pub`, so a secret that stops matching is a red scheduled run rather than an uninstallable release; and CI fails if README stops publishing the committed key, because `obu`'s own refusal message sends the user there. **DONE, 2026-10-10.** The key is served from <https://www.objeck.org/objeck-release.pub> -- a different host from this repository and from the release assets (objeck.org is Apache at 107.180.118.117; the assets are on GitHub) -- and verified byte-identical to the committed copy: 113 bytes, sha256 `864c6a9951a539ec356520dbeca4f97ec7a5fc9f4fd31d881f659f59bf7a6c04`, at both `www.objeck.org` and the bare hostname. That is what makes the check genuinely out of band: a key obtainable only from the repository being verified is not independent of it. Phase 4, and with it #723, is complete.

Two things to carry for a rotation. The site serves `.pub` as `application/x-mspublisher`, so browsers download rather than display it; `curl` is unaffected, and `AddType` was deliberately not added because a bad `.htaccess` on that account can take the site down for a cosmetic gain. And the published copy is NOT generated by anything -- rotating the key means uploading the new public half there by hand, or the weekly check keeps passing against a repository copy no user can corroborate.

**The manifest is signed twice.** Windows MSI signing happens after publication,
on the maintainer's machine, and it rewrites the MSI bytes -- so `SHA256SUMS` is
regenerated over the published assets afterwards, which invalidates the signature
made during publication. `.github/workflows/resign-manifest.yml` signs the
published manifest again, in CI, so the secret stays where it belongs;
`update_sha256sums.ps1` and `post_release.sh` both trigger it, and
`post_release.sh` gate `[4b]` verifies the published signature against the
published manifest with `obu` itself. Before phase 3 a stale signature was
merely useless. Now it makes the release uninstallable.

**What phase 3 changes at release time.** Up to phase 2, a release that was not
signed simply had no signature and `obu` carried on. From phase 3, `obu update`
refuses a manifest it cannot verify -- so an unsigned release is one that no
`obu` will install, and declaring `sig` manual without then uploading
`SHA256SUMS.minisig` ships exactly that. The signing step fails the publish when
the secret is absent and `sig` is not declared manual, which is the interlock;
the manual path has no such protection by construction, so it is the one to
check by hand.

The keypair generation and the secret's placement are maintainer actions; nothing in this design has an assistant or CI handle the secret key in the clear.

## Limits: what the signed manifest does not establish

- **One key, one secret.** Whoever holds `MINISIGN_SECRET_KEY` (a GitHub Actions secret, with the maintainer's password manager as fallback) can sign any manifest. A compromise of that secret, or of a workflow that has access to it, defeats the scheme; the weekly job only proves the secret still *matches* the committed key, not that it has not leaked.
- **The out-of-band copy is one more web host.** `https://www.objeck.org/objeck-release.pub` is independent of GitHub, but a user who trusts neither the repository nor that site has nothing to compare against, and the published copy is uploaded by hand (the document says so), so it can lag a rotation.
- **Platform signatures are separate.** Windows installers rely on Authenticode (checked by `signtool verify /pa`, which `release-publish.yml` only *reports*, it does not sign) and the macOS package on Developer ID plus notarization. The Linux `.tgz` archives and the macOS `.tgz`/`.zip` have no platform signature; the signed manifest is their only claim, and it protects only a user who goes through `obu` (or runs `obu verify`), not one who downloads with a browser and does not check.
- **Freshness is not shown.** A manifest is signed for a file name and carries a signed timestamp in its trusted comment, but this repository shows no check in `obu` that refuses an older, validly signed manifest (a grep of `core/utils/updater/obu.cpp` finds none). An attacker who can serve old assets with their old signed manifest is therefore not shown to be stopped. This has not been tested here.
- **Load-time integrity is out of scope** (see above): `obr` runs any `.obe` it is given; the loader's refusal of malformed files is a robustness property covered in [SECURITY_MODEL.md](SECURITY_MODEL.md) and fuzzed ([TESTING_AND_ASSURANCE.md](TESTING_AND_ASSURANCE.md)), not an integrity one.
- **Reproducibility is not claimed.** Nothing here shows that two builds of the same tag produce the same bytes, so a user cannot independently rebuild and compare.

**How to check it yourself.** Fetch `SHA256SUMS`, `SHA256SUMS.minisig` and an archive from a release, keep `SHA256SUMS.minisig` beside `SHA256SUMS`, then `obu verify <archive> SHA256SUMS` (it checks the manifest's signature first; exit 0 matches, 1 does not match, 2 could not check; the usage text in `obu` says so), and `minisign -V -p core/release/objeck-release.pub -m SHA256SUMS` as an independent verifier; compare `sha256sum core/release/objeck-release.pub` with the digest above and with what objeck.org serves.
