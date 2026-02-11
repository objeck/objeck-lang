# Code Signing Setup for Automated Builds

## Overview

This guide explains how to configure automated code signing for Windows MSI installers in GitHub Actions.

## Prerequisites

1. **Code Signing Certificate**
   - Valid code signing certificate (.pfx file)
   - Certificate password
   - Certificate must be from a trusted CA (e.g., DigiCert, Sectigo)

2. **GitHub Repository Access**
   - Admin access to repository settings
   - Ability to create GitHub Secrets

## Setup Steps

### Step 1: Convert Certificate to Base64

On Windows (PowerShell):
```powershell
# Read the certificate file
$certPath = "path\to\your\certificate.pfx"
$bytes = [System.IO.File]::ReadAllBytes($certPath)

# Convert to Base64
$base64 = [System.Convert]::ToBase64String($bytes)

# Save to file
[System.IO.File]::WriteAllText("certificate.base64.txt", $base64)

Write-Host "Certificate converted successfully!"
Write-Host "File saved to: certificate.base64.txt"
```

On Linux/macOS (bash):
```bash
# Convert certificate to Base64
base64 -i certificate.pfx -o certificate.base64.txt

# Or in one line:
base64 certificate.pfx > certificate.base64.txt
```

### Step 2: Add GitHub Secrets

1. **Navigate to Repository Settings:**
   ```
   https://github.com/objeck/objeck-lang/settings/secrets/actions
   ```

2. **Click "New repository secret"**

3. **Add First Secret:**
   - **Name:** `WINDOWS_CERT_BASE64`
   - **Value:** Paste entire contents of `certificate.base64.txt`
   - Click "Add secret"

4. **Add Second Secret:**
   - **Name:** `WINDOWS_CERT_PASSWORD`
   - **Value:** Your certificate password
   - Click "Add secret"

### Step 3: Verify Secrets

After adding, you should see:
- ✅ WINDOWS_CERT_BASE64
- ✅ WINDOWS_CERT_PASSWORD

**Security Note:** Secret values are encrypted and hidden. You won't be able to view them after creation.

### Step 4: Test

Create a test build to verify signing works:

```bash
# Create test tag
git tag v0.0.0-sign-test
git push origin v0.0.0-sign-test

# Monitor build at:
# https://github.com/objeck/objeck-lang/actions
```

**Expected output in build logs:**
```
REM Try to sign MSI if certificate is available
signtool sign /tr http://timestamp.sectigo.com /td sha256 /fd sha256 /a ...
✅ MSI signed successfully
```

**If signing fails:**
```
Warning: Code signing failed or no certificate available - continuing with unsigned MSI
```

### Step 5: Clean Up

**IMPORTANT:** Delete `certificate.base64.txt` after adding to GitHub Secrets!

```bash
# Windows
del certificate.base64.txt

# Linux/macOS
rm certificate.base64.txt
```

**Never commit the certificate file or Base64 text to git!**

## How It Works

### Workflow Process

1. **Certificate Import** (during build):
   ```yaml
   - name: Import code signing certificate (Windows)
     if: runner.os == 'Windows'
     shell: pwsh
     run: |
       # Decode certificate from Base64
       $certBytes = [Convert]::FromBase64String("${{ secrets.WINDOWS_CERT_BASE64 }}")
       $certPath = "$env:TEMP\codesign.pfx"
       [IO.File]::WriteAllBytes($certPath, $certBytes)

       # Import to certificate store
       $pwd = ConvertTo-SecureString -String "${{ secrets.WINDOWS_CERT_PASSWORD }}" -Force -AsPlainText
       Import-PfxCertificate -FilePath $certPath -CertStoreLocation Cert:\CurrentUser\My -Password $pwd

       # Clean up certificate file
       Remove-Item $certPath -Force
   ```

2. **MSI Signing** (after MSI creation):
   ```cmd
   signtool sign /tr http://timestamp.sectigo.com /td sha256 /fd sha256 /a setup.msi
   ```

3. **Verification** (automatic):
   ```cmd
   signtool verify /pa setup.msi
   ```

### Security Features

- ✅ Certificate stored encrypted in GitHub Secrets
- ✅ Certificate only exists in memory during build
- ✅ Temporary certificate file deleted immediately after import
- ✅ SHA256 signing algorithm
- ✅ RFC 3161 timestamping (Sectigo)
- ✅ Signature valid even after certificate expires (due to timestamp)

## Signing Details

### SignTool Parameters

```cmd
signtool sign \
  /tr http://timestamp.sectigo.com \  # Timestamp server (RFC 3161)
  /td sha256 \                        # Timestamp digest algorithm
  /fd sha256 \                        # File digest algorithm
  /a \                                # Auto-select certificate
  setup.msi                           # File to sign
```

### Why Timestamping Matters

Without timestamping:
- ❌ Signature becomes invalid when certificate expires
- ❌ Users can't install after expiration

With timestamping:
- ✅ Signature remains valid forever
- ✅ Proves code was signed while certificate was valid
- ✅ Users can install even after certificate expires

## Troubleshooting

### "No certificate available"

**Problem:** Secrets not configured or incorrectly named

**Solution:**
1. Verify secret names are exact: `WINDOWS_CERT_BASE64` and `WINDOWS_CERT_PASSWORD`
2. Check secrets in repository settings
3. Ensure secrets are set for Actions (not Dependabot or Codespaces)

### "The specified PFX password is not correct"

**Problem:** Wrong certificate password

**Solution:**
1. Verify password is correct (test locally first)
2. Update `WINDOWS_CERT_PASSWORD` secret with correct password
3. Check for special characters that might need escaping

### "Error: This project references NuGet package(s)..."

**Problem:** NuGet packages not restored (unrelated to signing)

**Solution:** This is fixed in deploy_windows.cmd with `nuget restore` commands

### "Signing failed - invalid certificate"

**Problem:** Certificate expired or not from trusted CA

**Solution:**
1. Check certificate expiration date
2. Ensure certificate is from a trusted CA
3. Use a valid code signing certificate (not SSL/TLS certificate)

### "Timestamp server unavailable"

**Problem:** Sectigo timestamp server down or network issue

**Solution:**
- Build continues with unsigned MSI (warning shown)
- Try again later when timestamp server is available
- Alternatively, use Microsoft timestamp server:
  ```cmd
  /tr http://timestamp.digicert.com
  ```

## Local Testing

### Test Certificate Import

```powershell
# Test certificate import locally
$certPath = "path\to\certificate.pfx"
$pwd = ConvertTo-SecureString -String "YourPassword" -Force -AsPlainText
Import-PfxCertificate -FilePath $certPath -CertStoreLocation Cert:\CurrentUser\My -Password $pwd

# List certificates
Get-ChildItem Cert:\CurrentUser\My | Where-Object {$_.Subject -like "*Your Company*"}
```

### Test Signing Locally

```cmd
# Sign a test MSI
signtool sign /tr http://timestamp.sectigo.com /td sha256 /fd sha256 /a test.msi

# Verify signature
signtool verify /pa test.msi
```

## Certificate Management

### Certificate Renewal

When your certificate expires:

1. **Obtain new certificate** from CA
2. **Convert to Base64** (Step 1 above)
3. **Update GitHub Secret:**
   - Go to repository secrets
   - Click "Update" next to `WINDOWS_CERT_BASE64`
   - Paste new Base64 content
   - If password changed, update `WINDOWS_CERT_PASSWORD` too

4. **Test:**
   - Create test build
   - Verify new certificate is used

### Multiple Certificates

To use different certificates for different purposes:

1. Add additional secrets:
   - `WINDOWS_CERT_EV_BASE64` (Extended Validation cert)
   - `WINDOWS_CERT_EV_PASSWORD`

2. Modify workflow to use specific certificate based on build type

### Certificate Rotation Best Practices

- 🔄 Set calendar reminder 30 days before expiration
- ✅ Test new certificate before old one expires
- 📋 Document certificate serial number for audit trail
- 🔐 Store certificate backup securely

## Cost Considerations

### Free Tier (Public Repository)

- ✅ Code signing during build: **$0**
- ✅ GitHub Actions minutes: **Unlimited (public repo)**
- ✅ Certificate storage: **Free (encrypted secrets)**

### Paid Tier (Private Repository)

- GitHub Actions minutes are metered
- Signing adds ~30 seconds per build
- Minimal cost impact (<$0.01 per build)

### Certificate Costs

- **Standard Code Signing:** ~$100-$200/year
- **Extended Validation (EV):** ~$300-$500/year
- **Organization Validation (OV):** ~$150-$300/year

**Note:** Certificate cost is separate from GitHub Actions costs.

## Security Best Practices

### Do's ✅

- ✅ Use GitHub Secrets for certificate storage
- ✅ Delete Base64 file after adding to secrets
- ✅ Use strong certificate password
- ✅ Rotate certificate before expiration
- ✅ Use timestamping for signatures
- ✅ Test signing process regularly

### Don'ts ❌

- ❌ Never commit certificate files to git
- ❌ Never store certificate password in code
- ❌ Never share secrets between repositories
- ❌ Never use expired certificates
- ❌ Don't skip signature verification

## Alternative: Hardware Security Module (HSM)

For enhanced security, consider using an HSM:

**Advantages:**
- 🔐 Private key never leaves hardware
- ✅ FIPS 140-2 Level 2/3 compliance
- ✅ Better protection against key theft

**Implementation:**
- Use Azure Key Vault or AWS CloudHSM
- Requires different workflow configuration
- Higher cost but better security

**When to use HSM:**
- Large organizations
- Regulatory compliance requirements
- High-value software
- Frequent signing operations

## FAQ

**Q: Is it safe to store certificates in GitHub Secrets?**
A: Yes, GitHub Secrets are encrypted at rest and in transit. However, HSM is more secure for high-security requirements.

**Q: Can I use the same certificate for multiple repositories?**
A: Technically yes, but it's better to have separate certificates per project for security isolation.

**Q: What happens if I don't set up code signing?**
A: Builds continue successfully but MSI files are unsigned. Users will see "Unknown Publisher" warnings.

**Q: Does signing affect build time?**
A: Minimal impact - adds ~10-30 seconds per MSI.

**Q: Can I use self-signed certificates?**
A: Not recommended - users will see security warnings. Use certificates from trusted CAs.

## References

- [SignTool Documentation](https://docs.microsoft.com/windows/win32/seccrypto/signtool)
- [GitHub Secrets Documentation](https://docs.github.com/actions/security-guides/encrypted-secrets)
- [Code Signing Best Practices](https://docs.microsoft.com/windows-hardware/drivers/dashboard/code-signing-best-practices)
- [RFC 3161 Timestamping](https://tools.ietf.org/html/rfc3161)

---

**Need help?** Check the CI/CD documentation or create an issue.
