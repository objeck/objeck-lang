# Code Signing for CI

macOS binaries, the `.pkg` and the `.zip` are signed and notarized in CI. Windows
installers are not: they are signed locally with a hardware token after the
release is published (see [Windows Code Signing](#windows-code-signing)).

## GitHub Secrets

### macOS — Application Signing (binaries)

| Secret | Description |
|---|---|
| `APPLE_CERTIFICATE_BASE64` | Developer ID Application certificate (p12, base64-encoded) |
| `APPLE_CERTIFICATE_PASSWORD` | Password for the p12 file |
| `KEYCHAIN_PASSWORD` | Password for the temporary CI keychain |

### macOS — .pkg Installer Signing & Notarization

| Secret | Description |
|---|---|
| `APPLE_INSTALLER_CERT_BASE64` | Developer ID Installer certificate (p12, base64-encoded) |
| `APPLE_INSTALLER_CERT_PASSWORD` | Password for the installer p12 file |
| `APPLE_ID` | Apple ID email for notarization |
| `APPLE_TEAM_ID` | 10-character team ID (developer.apple.com > Membership) |
| `APPLE_APP_PASSWORD` | App-specific password (appleid.apple.com > Security) |

### Windows — none

There is no Windows signing secret. The certificate is on a SafeNet eToken, so
its private key cannot be exported. `CODESIGN_CERT_BASE64` and `CODESIGN_PASSWORD`
are retired: no workflow references them, and the certificate stored in them
(examined 2026-09-08) was the Sectigo intermediate CA with no private key, so it
could never have signed anything. They can be deleted.

## Signing Identities

```
macOS:   Developer ID Application: Randy Hollines (37JDXYTCG2)
macOS:   Developer ID Installer: Randy Hollines (37JDXYTCG2)
Windows: Sectigo code signing certificate on a SafeNet eToken (expires 2028-05-25)
```

## How It Works

### macOS Application Signing

#### CI Workflow (`.github/workflows/ci-build.yml`)

The "Install Apple signing certificate" step runs on macOS and:
1. Decodes the p12 from `APPLE_CERTIFICATE_BASE64`
2. Creates a temporary keychain at `$RUNNER_TEMP/app-signing.keychain-db`
3. Imports the certificate into that keychain
4. Adds the keychain to the search list so xcodebuild can find it

A cleanup step deletes the temporary keychain after the build.

#### Deploy Script (`core/release/deploy_macos_arm64.sh`)

In CI, the script looks in the temporary keychain for a "Mac Development" identity, which is what the Xcode projects are configured for:
- **If present:** passes `OTHER_CODE_SIGN_FLAGS=--keychain=$RUNNER_TEMP/app-signing.keychain-db` to xcodebuild
- **If absent** (the CI keychain holds Developer ID identities, so in practice always): falls back to ad-hoc signing (`CODE_SIGN_IDENTITY=- CODE_SIGNING_REQUIRED=NO CODE_SIGNING_ALLOWED=NO`)

Locally, no flags are passed — xcodebuild uses the default keychain.

The Developer ID signature is applied afterwards. With `deploy`, the script runs `tools/cicd/sign_macos_tree.sh`, which signs every Mach-O in the tree with the Developer ID Application identity, hardened runtime and a secure timestamp, and then archives the tree as the `.zip` (with `ditto`, which keeps the signatures) and the `.tgz`. With no Developer ID identity available (a local build) that step does nothing.

### macOS .pkg Installer

#### Release Workflow (`.github/workflows/release-build.yml`)

The .pkg creation step (`core/release/create_macos_pkg.sh`):
1. Signs the deploy tree with `tools/cicd/sign_macos_tree.sh` (Developer ID Application); when an installer identity was found, a tree that cannot be signed stops the build
2. Stages the deploy directory into a pkg root at `/usr/local/objeck-lang`
3. Creates a postinstall script that sets up PATH via `/etc/paths.d/objeck`
4. Builds with `pkgbuild` + `productbuild` (distribution with welcome page and license)
5. Signs with `productsign` using the Developer ID Installer identity (if available)
6. Notarizes through `tools/cicd/notarize.sh` (`xcrun notarytool`), which trusts the returned verdict rather than the exit code, and staples the ticket (if Apple ID secrets are set)

Falls back to an unsigned .pkg if installer secrets are not configured.

### macOS .zip Notarization

#### Release Workflow (`.github/workflows/release-build.yml`)

The "Notarize macOS archive" step submits the macOS `.zip` to Apple's notary service with `tools/cicd/notarize.sh`; a rejected submission fails the build. The binaries inside were signed by `sign_macos_tree.sh` when the archive was made. A ticket cannot be stapled to a `.zip`, so Gatekeeper checks the binaries online the first time they run. The step is skipped when `APPLE_ID`, `APPLE_TEAM_ID` or `APPLE_APP_PASSWORD` is unset.

The `.tgz` still published beside it, so that older copies of `obu` can self-update, cannot be submitted for notarization: its binaries are signed but not notarized.

### Windows Code Signing

Not done in CI, and it cannot be: the private key is on a SafeNet eToken and a GitHub-hosted runner has no USB token. `release-build.yml` has no signing step, and `release-publish.yml` only runs `signtool verify /pa` on each MSI and reports what it finds.

#### After Publishing (`tools/cicd/post_release.sh <version> --sign`)

Run on the Windows release machine with the token plugged in. It calls `tools/cicd/sign_release.cmd <version>`, which:
1. Downloads the published MSIs from the GitHub release
2. Signs each with `signtool` (SHA-256, Sectigo timestamp), selecting the certificate by thumbprint rather than with `/a`; the token asks for its password
3. Verifies each with `signtool verify /pa`
4. Uploads the signed MSIs back to the release (`--clobber`)
5. Regenerates `SHA256SUMS` with `tools/cicd/update_sha256sums.ps1`, because signing changed the MSI bytes

`post_release.sh` then checks the published MSIs with `tools/cicd/check_release_signatures.ps1` (`Get-AuthenticodeSignature`) and re-verifies `SHA256SUMS` against the published files.

The portable Windows `.zip` archives, and the executables inside them, are not signed.

On certificate renewal, update `THUMBPRINT` in `sign_release.cmd`; the comment above it has the command that prints the new value.

### Credential Check

#### `.github/workflows/verify-signing-credentials.yml`

Runs every Monday at 06:00 UTC, and on demand, without building anything. It imports the two Apple certificates (`APPLE_INSTALLER_CERT_*`, `APPLE_CERTIFICATE_*`) into a throwaway keychain with the same commands the release uses, and fails if a secret is missing, a password does not open its certificate, either Developer ID identity lacks its private key, or a certificate has expired. It warns when a certificate expires within 30 days. It does not check the notarization secrets (`APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD`) or anything on the Windows side.

## Secret Status

As of 2026-09-19 (`gh secret list`). The Apple certificates are re-checked weekly by `verify-signing-credentials.yml`:

| Secret | Status |
|---|---|
| `APPLE_CERTIFICATE_BASE64` | Set — Developer ID Application (G2 Sub-CA) |
| `APPLE_CERTIFICATE_PASSWORD` | Set |
| `KEYCHAIN_PASSWORD` | Set |
| `APPLE_INSTALLER_CERT_BASE64` | Set — Developer ID Installer (G2 Sub-CA) |
| `APPLE_INSTALLER_CERT_PASSWORD` | Set |
| `APPLE_ID` | Set — objeck@gmail.com |
| `APPLE_TEAM_ID` | Set — 37JDXYTCG2 |
| `APPLE_APP_PASSWORD` | Set — app-specific password for notarization |
| `CODESIGN_CERT_BASE64` | Retired — still set, referenced by no workflow; can be deleted |
| `CODESIGN_PASSWORD` | Retired — still set, referenced by no workflow; can be deleted |
