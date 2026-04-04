# Apple Code Signing for CI

## GitHub Secrets

| Secret | Description |
|---|---|
| `APPLE_CERTIFICATE_BASE64` | Developer ID Application certificate (p12, base64-encoded) |
| `APPLE_CERTIFICATE_PASSWORD` | Password for the p12 file |
| `KEYCHAIN_PASSWORD` | Password for the temporary CI keychain |

## Signing Identity

```
Developer ID Application: Randy Hollines (37JDXYTCG2)
```

## How It Works

### CI Workflow (`.github/workflows/ci-build.yml`)

The "Install Apple signing certificate" step runs on macOS and:
1. Decodes the p12 from `APPLE_CERTIFICATE_BASE64`
2. Creates a temporary keychain at `$RUNNER_TEMP/app-signing.keychain-db`
3. Imports the certificate into that keychain
4. Adds the keychain to the search list so xcodebuild can find it

A cleanup step deletes the temporary keychain after the build.

### Deploy Script (`core/release/deploy_macos_arm64.sh`)

In CI, the script checks for the temporary keychain:
- **If present:** passes `OTHER_CODE_SIGN_FLAGS=--keychain=$RUNNER_TEMP/app-signing.keychain-db` to xcodebuild
- **If absent:** falls back to ad-hoc signing (`CODE_SIGN_IDENTITY=- CODE_SIGNING_REQUIRED=NO CODE_SIGNING_ALLOWED=NO`)

Locally, no flags are passed — xcodebuild uses the default keychain.
