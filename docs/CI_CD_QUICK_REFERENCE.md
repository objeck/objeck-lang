# CI/CD Quick Reference

Quick command reference for Objeck Language CI/CD workflows.

---

## 🚀 Creating a Release

### Step 1: Create Git Tag

```bash
# Standard release (e.g., v2026.2.1)
git tag v2026.2.1
git push origin v2026.2.1
```

This automatically triggers the **release build workflow**.

### Step 2: Trigger Release Publish

```bash
# Get the run ID from the release build
RUN_ID=$(gh run list --workflow=release-build.yml --limit 1 --json databaseId --jq '.[0].databaseId')

# Trigger release publish
gh workflow run release-publish.yml \
  -f version=2026.2.1 \
  -f run_id=$RUN_ID
```

**Or use GitHub UI:**
1. Go to **Actions** → **Release Publish**
2. Click **Run workflow**
3. Enter version and run ID
4. Click **Run workflow**

---

## 🔍 Monitoring Workflows

### Watch Live Build

```bash
# Watch the latest workflow run
gh run watch

# Watch specific workflow
gh run watch <run-id>
```

### List Recent Runs

```bash
# List all recent runs
gh run list --limit 10

# List specific workflow
gh run list --workflow=ci-build.yml --limit 5

# List failed runs only
gh run list --status=failure
```

### View Workflow Logs

```bash
# View summary
gh run view <run-id>

# View full logs
gh run view <run-id> --log

# View specific job
gh run view <run-id> --job=<job-id> --log
```

### Check Workflow Status

```bash
# Check status of latest run
gh run list --workflow=release-build.yml --limit 1 --json status,conclusion

# Get run ID
gh run list --workflow=release-build.yml --limit 1 --json databaseId --jq '.[0].databaseId'
```

---

## 🔄 Re-running Workflows

### Re-run Failed Jobs

```bash
# Re-run only failed jobs
gh run rerun <run-id> --failed
```

### Re-run Entire Workflow

```bash
# Re-run all jobs
gh run rerun <run-id>
```

### Cancel Running Workflow

```bash
# Cancel a workflow run
gh run cancel <run-id>
```

---

## 📦 Managing Releases

### View Releases

```bash
# List all releases
gh release list

# View specific release
gh release view v2026.2.1

# View latest release
gh release view --latest
```

### Download Release Assets

```bash
# Download all assets for a release
gh release download v2026.2.1

# Download specific asset
gh release download v2026.2.1 --pattern "objeck-windows-x64_*"
```

### Delete Release (Testing)

```bash
# Delete release and tag
gh release delete v0.0.0-test --yes

# Delete tag locally and remotely
git tag -d v0.0.0-test
git push origin :refs/tags/v0.0.0-test
```

---

## 🔐 Managing Secrets

### List Secrets

```bash
# List all repository secrets (names only, not values)
gh secret list
```

### Set Secret

```bash
# Set secret from stdin
echo "secret_value" | gh secret set SECRET_NAME

# Set secret from file
gh secret set SECRET_NAME < secret_file.txt
```

### Delete Secret

```bash
# Delete a secret
gh secret remove SECRET_NAME
```

---

## 🧹 Cache Management

### Clear Caches

**Via GitHub UI:**
1. Go to **Settings** → **Actions** → **Caches**
2. Select caches to delete
3. Click **Delete**

**Via API:**
```bash
# List all caches
gh api repos/objeck/objeck-lang/actions/caches

# Delete specific cache
gh api --method DELETE repos/objeck/objeck-lang/actions/caches/<cache-id>
```

---

## 🐛 Troubleshooting

### Build Fails

```bash
# 1. View logs
gh run view <run-id> --log

# 2. Re-run failed jobs
gh run rerun <run-id> --failed

# 3. If issue persists, fix code and re-tag
git tag -d v2026.2.1
git push origin :refs/tags/v2026.2.1

# Fix code, commit, push

git tag v2026.2.1
git push origin v2026.2.1
```

### Artifacts Not Found

```bash
# List artifacts for a run
gh api repos/objeck/objeck-lang/actions/runs/<run-id>/artifacts

# Download artifacts
gh run download <run-id>
```

### Workflow Not Triggering

```bash
# Check if workflow file is valid
gh workflow list

# View workflow details
gh workflow view release-build.yml

# Manually trigger workflow
gh workflow run release-build.yml -f version=2026.2.1
```

---

## 📊 Build Status

### Check CI Status

**Command Line:**
```bash
# Check latest CI build
gh run list --workflow=ci-build.yml --limit 1
```

**Web:**
- https://github.com/objeck/objeck-lang/actions

**Badges (for README):**
```markdown
[![CI Build](https://github.com/objeck/objeck-lang/actions/workflows/ci-build.yml/badge.svg)](https://github.com/objeck/objeck-lang/actions/workflows/ci-build.yml)
```

---

## 🎯 Common Workflows

### Test Before Release

```bash
# 1. Create test tag
git tag v0.0.0-test
git push origin v0.0.0-test

# 2. Monitor build
gh run watch

# 3. Check artifacts
gh run view <run-id>

# 4. Delete test tag
gh release delete v0.0.0-test --yes
git tag -d v0.0.0-test
git push origin :refs/tags/v0.0.0-test
```

### Hotfix Release

```bash
# 1. Fix critical bug
git checkout -b hotfix-2026.2.2
git commit -am "Fix: critical bug"
git push origin hotfix-2026.2.2

# 2. Create PR and merge
gh pr create --title "Hotfix: critical bug" --body "Fixes #123"
gh pr merge --squash

# 3. Tag release
git checkout master
git pull
git tag v2026.2.2
git push origin v2026.2.2

# 4. Follow normal release process
```

### Check Release Distribution

```bash
# After release, verify downloads work
curl -I https://github.com/objeck/objeck-lang/releases/download/v2026.2.1/objeck-windows-x64_2026.2.1.msi

# Check Sourceforge (if configured)
curl -I https://sourceforge.net/projects/objeck/files/v2026.2.1/

# Check API docs (if configured)
curl -I https://objeck.org/api/v2026.2.1/
```

---

## 📝 Workflow Files

**Location:** `.github/workflows/`

| File | Purpose | Trigger |
|------|---------|---------|
| `ci-build.yml` | Continuous integration | Push/PR to master |
| `release-build.yml` | Release builds | Git tag `v*.*.*` |
| `release-publish.yml` | Release distribution | Manual dispatch |

---

## 🔗 Useful Links

- **Workflows:** https://github.com/objeck/objeck-lang/actions
- **Releases:** https://github.com/objeck/objeck-lang/releases
- **Documentation:**
  - [Release Process](release_process.md)
  - [CI/CD Architecture](CI_CD.md)
  - [GitHub Actions Setup](GITHUB_ACTIONS_SETUP.md)
  - [Implementation Summary](CI_CD_IMPLEMENTATION_SUMMARY.md)

---

## 💡 Tips

1. **Use `gh` CLI** for faster workflow management
2. **Watch workflows live** with `gh run watch` instead of refreshing browser
3. **Test with temporary tags** (`v0.0.0-test`) before real releases
4. **Check cache hit rates** to optimize build times
5. **Set up email notifications** in GitHub settings for workflow failures

---

## 🆘 Getting Help

- **Documentation:** See `docs/` directory
- **Issues:** https://github.com/objeck/objeck-lang/issues
- **GitHub Actions Docs:** https://docs.github.com/actions
- **gh CLI Docs:** https://cli.github.com/manual/

---

## 📋 Pre-flight Checklist

Before creating a release:

- [ ] All tests passing on master
- [ ] CI build successful
- [ ] Version number decided (e.g., 2026.2.1)
- [ ] CHANGELOG updated (optional)
- [ ] Secrets configured (if first release)
- [ ] Ready to tag and push

After release published:

- [ ] GitHub Release verified
- [ ] Sourceforge upload verified (if enabled)
- [ ] API docs deployed (if enabled)
- [ ] Downloads tested
- [ ] Announcement prepared (optional)

---

**Last Updated:** 2026-02-10
**Version:** 1.0
