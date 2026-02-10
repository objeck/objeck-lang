# GitHub Actions Setup Guide

This guide walks through setting up the required secrets for the Objeck Language CI/CD pipeline.

## Required Secrets

Navigate to: **Repository Settings → Secrets and variables → Actions → New repository secret**

---

## 1. Code Signing Certificate (Windows)

### Required Secrets

- `WINDOWS_CERT_BASE64` - Base64-encoded PFX certificate
- `WINDOWS_CERT_PASSWORD` - Certificate password

### Setup Steps

**On Windows with your code signing certificate:**

```powershell
# 1. Convert PFX to Base64
certutil -encode certificate.pfx certificate.base64.txt

# 2. Open the file and copy everything EXCEPT the header/footer lines
#    (Remove "-----BEGIN CERTIFICATE-----" and "-----END CERTIFICATE-----")
notepad certificate.base64.txt

# 3. Copy the Base64 content to GitHub Secret: WINDOWS_CERT_BASE64
```

**In GitHub:**
1. Go to repository **Settings → Secrets and variables → Actions**
2. Click **New repository secret**
3. Name: `WINDOWS_CERT_BASE64`
4. Value: Paste the Base64-encoded certificate (without header/footer)
5. Click **Add secret**

6. Click **New repository secret** again
7. Name: `WINDOWS_CERT_PASSWORD`
8. Value: Your certificate password
9. Click **Add secret**

### Verify Certificate

```powershell
# Get certificate thumbprint (SHA-1)
certutil -dump certificate.pfx

# Look for "Cert Hash(sha1):" in output
# Example: 1a 2b 3c 4d 5e 6f ...
```

### Certificate Expiration

Code signing certificates typically expire after **1-3 years**. Remember to:
- Set a calendar reminder 30 days before expiration
- Purchase/renew certificate
- Update GitHub secret before expiration

---

## 2. Sourceforge Upload (Optional)

### Required Secrets

- `SOURCEFORGE_SSH_KEY` - SSH private key for Sourceforge
- `SOURCEFORGE_USERNAME` - Your Sourceforge username

### Setup Steps

**Generate SSH key:**

```bash
# Generate Ed25519 key (no passphrase)
ssh-keygen -t ed25519 -f sourceforge_key -N ""

# This creates two files:
# - sourceforge_key (private key) → GitHub secret
# - sourceforge_key.pub (public key) → Sourceforge account
```

**Add public key to Sourceforge:**

1. Login to Sourceforge: https://sourceforge.net
2. Go to **Account → SSH Settings**
3. Copy content of `sourceforge_key.pub`:
   ```bash
   cat sourceforge_key.pub
   ```
4. Paste into Sourceforge SSH settings
5. Save

**Add private key to GitHub:**

1. Copy private key content:
   ```bash
   cat sourceforge_key
   ```
2. Go to GitHub **Settings → Secrets and variables → Actions**
3. Click **New repository secret**
4. Name: `SOURCEFORGE_SSH_KEY`
5. Value: Paste the entire private key content (including header/footer)
6. Click **Add secret**

7. Click **New repository secret**
8. Name: `SOURCEFORGE_USERNAME`
9. Value: Your Sourceforge username
10. Click **Add secret**

**Test SSH access:**

```bash
# Test connection (should succeed without password prompt)
ssh -i sourceforge_key <username>@frs.sourceforge.net
```

**Cleanup:**

```bash
# Delete local key files (already in GitHub secrets)
rm sourceforge_key sourceforge_key.pub
```

---

## 3. API Documentation Deployment (Optional)

### Required Secrets

- `OBJECK_ORG_SSH_KEY` - SSH private key for objeck.org
- `OBJECK_ORG_USER` - SSH username for objeck.org

### Setup Steps

**Generate SSH key:**

```bash
# Generate Ed25519 key (no passphrase)
ssh-keygen -t ed25519 -f objeck_org_key -N ""
```

**Add public key to web server:**

```bash
# Copy public key to server
ssh-copy-id -i objeck_org_key.pub <user>@objeck.org

# Or manually:
cat objeck_org_key.pub
# Add to ~/.ssh/authorized_keys on server
```

**Add private key to GitHub:**

1. Copy private key:
   ```bash
   cat objeck_org_key
   ```
2. Go to GitHub **Settings → Secrets and variables → Actions**
3. Click **New repository secret**
4. Name: `OBJECK_ORG_SSH_KEY`
5. Value: Paste the private key
6. Click **Add secret**

7. Click **New repository secret**
8. Name: `OBJECK_ORG_USER`
9. Value: Your SSH username (e.g., `objeck`)
10. Click **Add secret**

**Test SSH access:**

```bash
# Test connection
ssh -i objeck_org_key <user>@objeck.org

# Verify web directory permissions
ssh -i objeck_org_key <user>@objeck.org "ls -la /var/www/objeck.org/api/"
```

**Cleanup:**

```bash
rm objeck_org_key objeck_org_key.pub
```

---

## 4. Verify Secrets

### Check All Secrets Set

Go to: **Repository Settings → Secrets and variables → Actions**

You should see:

✅ Required (for code signing):
- `WINDOWS_CERT_BASE64`
- `WINDOWS_CERT_PASSWORD`

✅ Optional (for distribution):
- `SOURCEFORGE_SSH_KEY`
- `SOURCEFORGE_USERNAME`
- `OBJECK_ORG_SSH_KEY`
- `OBJECK_ORG_USER`

**Note:** `GITHUB_TOKEN` is automatically provided (no need to create).

### Test Workflow

Trigger a test workflow to verify secrets:

```bash
# Trigger CI build (tests basic build without secrets)
git commit --allow-empty -m "Test CI build"
git push

# Monitor workflow
gh run watch

# Or check in browser
# https://github.com/objeck/objeck-lang/actions
```

For release workflows (which use secrets):
- Test on a non-production tag first
- Example: `git tag v0.0.0-test && git push origin v0.0.0-test`
- Monitor logs for secret-related errors
- Delete test tag after: `git tag -d v0.0.0-test && git push origin :refs/tags/v0.0.0-test`

---

## Security Best Practices

### Do's

✅ **Use dedicated keys** (not personal account keys)
✅ **Rotate regularly** (every 2 years for SSH keys)
✅ **Use strong passwords** (for certificate)
✅ **Set expiration reminders** (for certificates)
✅ **Limit permissions** (read-only where possible)
✅ **Monitor usage** (check GitHub audit logs)

### Don'ts

❌ **Never commit secrets** to repository
❌ **Never share secrets** with others
❌ **Never use personal credentials** in CI
❌ **Never log secret values** in workflows
❌ **Never skip cleanup steps** (always remove temp files)

---

## Troubleshooting

### "Secret not found" Error

**Problem:** Workflow can't access secret

**Solution:**
1. Verify secret name matches exactly (case-sensitive)
2. Check secret is in correct repository (not organization)
3. Ensure secret is not empty

### Code Signing Fails

**Problem:** `signtool` errors or signature verification fails

**Solution:**
1. Verify certificate is valid (not expired)
2. Check password is correct
3. Verify timestamp server is accessible
4. Check certificate format (must be PFX)

**Test locally:**
```powershell
# Decode and test certificate
$certBytes = [Convert]::FromBase64String("...paste base64...")
[IO.File]::WriteAllBytes("test.pfx", $certBytes)

# Try signing a test file
signtool sign /f test.pfx /p "password" /tr http://timestamp.sectigo.com /td sha256 /fd sha256 test.exe

# Clean up
Remove-Item test.pfx
```

### SSH Connection Fails

**Problem:** "Permission denied" or "Connection refused"

**Solution:**
1. Verify public key is added to remote server
2. Check SSH key format (must include header/footer)
3. Verify server hostname is correct
4. Check firewall rules allow SSH

**Test locally:**
```bash
# Save secret to file temporarily
echo "$SECRET_VALUE" > test_key
chmod 600 test_key

# Test connection
ssh -i test_key -v user@host

# Clean up
rm test_key
```

### Workflow Skips Steps

**Problem:** Code signing or uploads are skipped

**Solution:**
- Steps are skipped if secrets are not set (this is intentional)
- Workflows will still succeed, just with warnings
- Set secrets to enable those features

**Check workflow logs:**
```
⚠️  WINDOWS_CERT_BASE64 secret not set, skipping code signing
⚠️  Sourceforge SSH key not configured. Skipping upload.
```

---

## Updating Secrets

### To Update a Secret

1. Go to **Repository Settings → Secrets and variables → Actions**
2. Click on the secret name
3. Click **Update secret**
4. Enter new value
5. Click **Update secret**

### When to Update

- **Certificate password changed**
- **Certificate renewed** (new PFX file)
- **SSH key rotated** (security best practice)
- **Username changed** (rare)

---

## Getting Help

- **GitHub Actions Docs:** https://docs.github.com/actions/security-guides/encrypted-secrets
- **Objeck Issues:** https://github.com/objeck/objeck-lang/issues
- **CI/CD Architecture:** See `docs/CI_CD.md`

---

## Quick Reference

### Secret Names

```
WINDOWS_CERT_BASE64      - Base64-encoded PFX certificate
WINDOWS_CERT_PASSWORD    - Certificate password
SOURCEFORGE_SSH_KEY      - SSH private key for Sourceforge
SOURCEFORGE_USERNAME     - Sourceforge username
OBJECK_ORG_SSH_KEY       - SSH private key for objeck.org
OBJECK_ORG_USER          - SSH username for objeck.org
```

### Commands

```bash
# Generate SSH key
ssh-keygen -t ed25519 -f key_name -N ""

# Convert PFX to Base64 (Windows)
certutil -encode certificate.pfx certificate.base64.txt

# Convert PFX to Base64 (Linux/macOS)
base64 certificate.pfx > certificate.base64.txt

# Test SSH connection
ssh -i key_file user@host

# View GitHub secrets (names only, not values)
gh secret list

# Trigger test workflow
gh workflow run ci-build.yml
```
