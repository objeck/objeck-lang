# CI/CD Setup for Objeck Language Releases

## Overview

This document describes the automated Continuous Integration and Continuous Deployment (CI/CD) setup for building and releasing Objeck Language across multiple platforms using GitHub Actions.

## Architecture

### Three-Tier Workflow System

1. **CI Build** (`.github/workflows/c-cpp.yml`) - Runs on every push/PR
   - Quick validation builds
   - Basic regression tests
   - Fast feedback (~15-20 minutes)

2. **Release Build** (`.github/workflows/release-build.yml`) - Triggered by git tags
   - Full builds for all 6 platforms (parallel)
   - Windows MSI installer creation
   - Automated code signing (if configured)
   - API documentation generation
   - Estimated time: 25-35 minutes

3. **Release Publish** (future) - After release build
   - Binary renaming automation
   - Upload to GitHub Releases
   - Upload to Sourceforge
   - Deploy API docs to objeck.org

## Supported Platforms

| Platform | Runner | Arch | Build Time | Artifacts |
|----------|--------|------|------------|-----------|
| Windows x64 | `windows-latest` | x64 | 25 min | MSI + ZIP |
| Windows ARM64 | `windows-latest` | ARM64 | 28 min | MSI + ZIP (cross-compiled) |
| Linux x64 | `ubuntu-latest` | x64 | 20 min | TGZ |
| macOS ARM64 | `macos-14` | ARM64 | 30 min | TGZ |
| LSP Package | `windows-latest` | N/A | 10 min | ZIP |

**Note:** Windows ARM64 is cross-compiled on x64 runners for reliability.

## How to Create a Release

### Automated Release (Recommended)

```bash
# 1. Update version in update_version.ps1 or update_version.sh
# 2. Create and push a git tag
git tag v2026.2.1
git push origin v2026.2.1

# That's it! GitHub Actions will:
# - Build all platforms in parallel
# - Create MSI installers
# - Generate ZIP archives
# - Upload artifacts

# 3. Download artifacts from:
# https://github.com/objeck/objeck-lang/actions/runs/XXXXX
```

### Version Format

- **Release versions:** `v2026.2.1` (triggers full build + docs)
- **Test versions:** `v0.0.0-test` (skips API docs due to library version mismatch)
- **Pre-release:** `v2026.2.1-rc1` (same as release)

## Windows Build Details

### x64 Build Process

1. **Compile** - Build all components (compiler, VM, REPL, debugger)
2. **Libraries** - Build native libraries (crypto, ONNX, SDL, OpenCV, etc.)
3. **NuGet Restore** - Restore ONNX DirectML packages
4. **Copy Binaries** - Copy all binaries to deployment directory
5. **Embed Manifests** - Use mt.exe to embed Windows manifests
6. **Generate Docs** - Run code_doc to generate API documentation
7. **Create MSI** - Build MSI installer using Visual Studio
8. **Code Signing** - Sign MSI if certificate is configured
9. **Create ZIP** - Use PowerShell Compress-Archive

### ARM64 Build Process (Cross-Compilation)

Same as x64 but with these differences:

- **Cross-compile:** Build ARM64 binaries on x64 runner
- **Host Tools:** Use x64 version of mt.exe (ARM64 version can't run on x64)
- **Skip code_doc:** ARM64 binaries can't execute on x64 host
- **Documentation:** Use docs from x64 build

### Key Fixes Applied

1. **Binary Copy Order** - Copy all binaries BEFORE embedding manifests
   ```cmd
   # Before: mt.exe ran before obr.exe was copied (file not found)
   # After: Copy all binaries first, then embed manifests
   ```

2. **NuGet Package Restore** - Restore ONNX packages before building
   ```cmd
   nuget restore onnx_dml.sln
   devenv onnx_dml.sln /rebuild "Release|x64"
   ```

3. **ARM64 Cross-Compilation** - Use x64 host tools
   ```cmd
   "%WindowsSdkVerBinPath%x64\mt.exe" -manifest ...
   ```

4. **ZIP Creation** - Use PowerShell instead of 7-Zip
   ```cmd
   powershell -Command "Compress-Archive -Path ... -DestinationPath ... -Force"
   ```

5. **Crypto Library** - Check file existence instead of errorlevel
   ```cmd
   if exist Release\win64\*.dll (
       copy Release\win64\*.dll ...
   )
   ```

## Code Signing Setup (Optional)

### Prerequisites

1. Code signing certificate (.pfx file)
2. Certificate password

### Configuration

1. **Convert certificate to Base64:**
   ```powershell
   $bytes = [System.IO.File]::ReadAllBytes("path\to\certificate.pfx")
   $base64 = [System.Convert]::ToBase64String($bytes)
   [System.IO.File]::WriteAllText("certificate.base64.txt", $base64)
   ```

2. **Add GitHub Secrets:**
   - Go to: https://github.com/objeck/objeck-lang/settings/secrets/actions
   - Add secrets:
     - `WINDOWS_CERT_BASE64` - Content of certificate.base64.txt
     - `WINDOWS_CERT_PASSWORD` - Certificate password

3. **Test:**
   - Create a test tag and watch the build
   - MSI will be automatically signed if secrets are configured
   - Look for: "✅ MSI signed successfully" in logs

### How It Works

The workflow automatically:
1. Decodes the Base64 certificate
2. Imports it to the certificate store
3. Uses `signtool` to sign the MSI
4. Cleans up the certificate file

If secrets are not configured, the build continues with unsigned MSI (warning shown).

## Troubleshooting

### Build Fails: "mt.exe: file not found"

**Problem:** Manifest tool trying to embed before binaries are copied

**Solution:** Check that binaries are copied before mt.exe runs in deploy_windows.cmd

### Build Fails: "NuGet packages are missing"

**Problem:** ONNX build needs DirectML packages

**Solution:** Ensure `nuget restore` runs before devenv build

### ARM64 Build Stuck in Queue

**Problem:** ARM64 runners may have limited availability

**Solution:** We use cross-compilation on x64 runners instead (more reliable)

### API Docs Fail: "different version of tool chain"

**Problem:** Pre-compiled libraries don't match test version

**Solution:** API docs are automatically skipped for test builds (versions containing "test")

### Build Fails: "The system cannot find the path specified" (7-Zip)

**Problem:** 7-Zip not installed on GitHub runners

**Solution:** We use PowerShell Compress-Archive instead

## Performance

### Build Times (with caching)

- **First build:** ~30-35 minutes (parallel)
- **Cached builds:** ~20-25 minutes
- **Single platform:** ~25 minutes

### Caching Strategy

- **Windows:** Dependencies vendored in repo (no external caching needed)
- **Linux:** APT packages cached
- **macOS:** Homebrew packages cached

### Cost

**Free for public repositories!**

- Unlimited build minutes
- Unlimited artifact storage (with 7-day retention)
- All runner types included (including ARM64)

**If repository becomes private:**
- Estimated: $0-$5/month (within free tier of 2000 minutes)
- Windows runners: 2x multiplier
- macOS runners: 10x multiplier
- ARM64 runners: Same rate as standard runners

## Comparison: Before vs After

| Aspect | Manual Process | Automated CI/CD |
|--------|---------------|-----------------|
| Total time | 8+ hours | 30-35 minutes |
| Manual steps | 15+ | 1 (git tag) |
| Platforms built locally | All | None |
| Code signing | Manual password | Automatic |
| Error rate | High (manual) | Low (automated) |
| Parallel builds | No | Yes |
| Cost | $0 | $0 (public repo) |
| Can work on other tasks? | No | Yes |

## Files and Structure

### Workflow Files
- `.github/workflows/release-build.yml` - Main release workflow
- `.github/workflows/c-cpp.yml` - CI validation workflow

### Build Scripts
- `core/release/deploy_windows.cmd` - Windows deployment script
- `core/release/deploy_posix.sh` - Linux deployment script
- `core/release/deploy_macos_arm64.sh` - macOS deployment script
- `core/release/update_version.ps1` - Windows version updater
- `core/compiler/update_version.sh` - Linux/macOS version updater

### Artifact Locations
- **Windows:** `Objeck-Build/release-{arch}/` (created during build)
- **Linux/macOS:** `~/Desktop/` (created during build)
- **GitHub:** Downloaded from Actions run page

## Next Steps

### Planned Enhancements

1. **Release Publishing Workflow**
   - Automatic GitHub Release creation
   - Sourceforge upload via SFTP
   - API docs deployment to objeck.org

2. **Artifact Signing**
   - Windows: MSI signing (infrastructure ready)
   - macOS: Code signing and notarization
   - Linux: GPG signing of packages

3. **Enhanced Testing**
   - Run regression tests in CI
   - Platform-specific test suites
   - Performance benchmarks

4. **Notifications**
   - Slack/Discord webhooks for build status
   - Email notifications for failures

## Support

For issues or questions about the CI/CD setup:
- Check workflow run logs: https://github.com/objeck/objeck-lang/actions
- Review this documentation
- Check GitHub Actions status: https://www.githubstatus.com/

## References

- [GitHub Actions Documentation](https://docs.github.com/actions)
- [Windows Code Signing](https://docs.microsoft.com/windows/win32/seccrypto/signtool)
- [GitHub Actions Pricing](https://docs.github.com/billing/reference/actions-runner-pricing)
