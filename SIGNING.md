# Code Signing for CI

## GitHub Secrets

### macOS — Application Signing (Xcode builds)

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

### Windows — Code Signing

| Secret | Description |
|---|---|
| `CODESIGN_CERT_BASE64` | Sectigo code signing certificate (pfx, base64-encoded) |
| `CODESIGN_PASSWORD` | Password for the pfx file |

## Signing Identities

```
macOS:   Developer ID Application: Randy Hollines (37JDXYTCG2)
macOS:   Developer ID Installer: Randy Hollines (37JDXYTCG2)
Windows: Sectigo code signing certificate (pfx)
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

In CI, the script checks for the temporary keychain:
- **If present:** passes `OTHER_CODE_SIGN_FLAGS=--keychain=$RUNNER_TEMP/app-signing.keychain-db` to xcodebuild
- **If absent:** falls back to ad-hoc signing (`CODE_SIGN_IDENTITY=- CODE_SIGNING_REQUIRED=NO CODE_SIGNING_ALLOWED=NO`)

Locally, no flags are passed — xcodebuild uses the default keychain.

### macOS .pkg Installer

#### Release Workflow (`.github/workflows/release-build.yml`)

The .pkg creation step (`core/release/create_macos_pkg.sh`):
1. Stages the deploy directory into a pkg root at `/usr/local/objeck-lang`
2. Creates a postinstall script that sets up PATH via `/etc/paths.d/objeck`
3. Builds with `pkgbuild` + `productbuild` (distribution with welcome page and license)
4. Signs with `productsign` using the Developer ID Installer identity (if available)
5. Notarizes with `xcrun notarytool` and staples the ticket (if Apple ID secrets are set)

Falls back to an unsigned .pkg if installer secrets are not configured.

### Windows Code Signing

#### Release Workflow (`.github/workflows/release-build.yml`)

1. Decodes the pfx from `CODESIGN_CERT_BASE64`
2. Signs .msi files with `signtool` using SHA-256 + Sectigo timestamp
3. Signs .exe files in deploy bin directories
4. Cleans up the certificate file

Falls back to unsigned artifacts if secrets are not configured.

## Secret Status

All secrets are configured and ready (as of 2026-04-04):

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
| `CODESIGN_CERT_BASE64` | Set — Windows Sectigo cert |
| `CODESIGN_PASSWORD` | Set |
