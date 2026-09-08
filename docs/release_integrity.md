# Release integrity: what is verified today, and the signed manifest that would close the gap

## What exists

| Layer | Mechanism | Verified by |
|---|---|---|
| Windows installers | Authenticode signature (SafeNet eToken, signed locally after publish by `tools/cicd/sign_release.cmd`) | `Get-AuthenticodeSignature`; the release body tells the user to check it |
| macOS package | Developer ID signature + notarization, in CI | Gatekeeper |
| Every asset | `SHA256SUMS`, regenerated after signing (PR #693) | `sha256sum -c`; `obu update` computes SHA-256 itself and refuses an asset that does not match the manifest |
| Repository | `secret-scan.yml` (gitleaks), CodeQL, GitGuardian | CI |

So a corrupted or truncated download is caught, and a Windows or macOS installer that was tampered with fails its platform's signature check.

## What is not covered

`SHA256SUMS` is served from the same place as the assets it describes. Anyone who can replace a release asset on GitHub — a compromised maintainer account, a compromised Actions token with `contents: write` — can replace the manifest to match. `obu` then verifies the substituted archive against the substituted manifest and reports success. The Linux `.tgz` and the macOS `.tgz`/`.zip` have no platform signature at all, so for those the manifest is the only integrity claim, and it is unsigned.

There is also no load-time integrity: `obr` executes any `.obe`, links any `.obl` and `LoadLibrary`s any `libobjk_*` it finds under `lib/`. That is by design for a development toolchain (users compile their own libraries) and is out of scope here; integrity is a property of the *distribution*, checked once at install or update time.

## Design: a detached signature on the manifest

**Key.** One Ed25519 keypair (minisign format: small, no PKI, no expiry ceremony). The public key is committed to the repository and compiled into `obu` (`core/utils/updater/obu.cpp`) as a string constant; the secret key lives only in a GitHub Actions secret (`MINISIGN_SECRET_KEY`) and, as a fallback, in the maintainer's password manager. The public key is also published on objeck.org and in `README.md` so a user can check it out of band.

**Signing.** `release-publish.yml`, after "Generate SHA256SUMS" (which already runs after signing so the manifest describes the signed bytes), signs `SHA256SUMS` with `minisign -S` and uploads `SHA256SUMS.minisig` beside it. If the secret is absent the step *fails* unless `sig` is declared in `RELEASE_MANUAL_STEPS` — the same rule every other publish step follows since #692, so a missing key is a red job, not a silent skip. `tools/cicd/check_release_config.sh` learns the new secret and the new manual token.

**Verification in `obu`.** `obu update` downloads `SHA256SUMS` and `SHA256SUMS.minisig`, verifies the signature against the compiled-in key *before* trusting any line of the manifest, then proceeds as today. A missing `.minisig` is a hard failure once the key has shipped in an `obu` — the transition rule below covers the release that introduces it. `obu verify` is a new subcommand that runs the same check against an already-downloaded archive and manifest, so a user who fetched with `curl` can verify without installing.

Ed25519 verification is ~600 lines of dependency-free C (the reference or TweetNaCl implementation); `obu` already carries its own SHA-256 for the same reason (no OpenSSL at update time), so this keeps the updater self-contained.

**Key rotation.** A new key is introduced by shipping an `obu` that trusts both the old and new keys for one release, then dropping the old one. Rotation, like the eToken certificate, is a documented manual step with a date in `docs/release_signing_process.md`.

## Phases

1. **This document; `obu verify` reads `SHA256SUMS` without a signature** (already true for `update`; `verify` exposes it). No new secrets.
2. **Generate the keypair (maintainer, locally), commit the public key, add the signing step and the `sig` manual token.** The release that ships this has a signed manifest but an `obu` that does not yet require it.
3. **`obu` verifies the signature; missing or invalid is fatal.** From here on a substituted manifest is caught.
4. **Publish the public key out of band** (objeck.org, README) and add a `verify-signing-credentials.yml`-style check that the secret key matches the committed public key, on the same weekly schedule.

The keypair generation and the secret's placement are maintainer actions; nothing in this design has an assistant or CI handle the secret key in the clear.
