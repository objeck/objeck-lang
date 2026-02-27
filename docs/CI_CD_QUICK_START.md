# CI/CD Quick Start

Command reference for Objeck CI/CD workflows.

---

## Creating a Release

### 1. Tag and push

```bash
git tag v2026.2.1
git push origin v2026.2.1
```

This triggers `release-build.yml` automatically. On success, `release-publish.yml` auto-triggers.

### 2. Monitor

```bash
# Watch the release build
gh run watch

# Check status
gh run list --workflow=release-build.yml --limit 1
gh run list --workflow=release-publish.yml --limit 1
```

### 3. Update the GitHub Release body

The auto-generated release notes are generic. Update with proper notes from `docs/readme.txt`.

### 4. Verify

```bash
# Check release assets
gh release view v2026.2.1

# Test a download
curl -I https://github.com/objeck/objeck-lang/releases/download/v2026.2.1/objeck-windows-x64_2026.2.1.msi
```

- GitHub: https://github.com/objeck/objeck-lang/releases
- Sourceforge: https://sourceforge.net/projects/objeck/files/
- API Docs: https://objeck.org/api/latest/

---

## Workflows

| File | Trigger | Duration | Purpose |
|------|---------|----------|---------|
| `ci-build.yml` | Push/PR to master | ~10 min | Build + test all 5 platforms |
| `release-build.yml` | Tag `v*.*.*` or manual | ~45 min | Build release binaries + LSP + API docs |
| `release-publish.yml` | Auto on build success, or manual | ~15 min | Sign, rename, upload to GitHub/Sourceforge/objeck.org |

### Platform matrix (shared by CI and release builds)

| Platform | Runner | Architecture |
|----------|--------|-------------|
| Windows x64 | windows-latest | x64 |
| Windows ARM64 | windows-latest | x64 (cross-compile) |
| Linux x64 | ubuntu-latest | x64 |
| Linux ARM64 | ubuntu-24.04-arm | arm64 |
| macOS ARM64 | macos-14 | arm64 |

---

## Re-tagging a Release

If a build fails or you need to re-release:

```bash
# Delete release and tag
gh release delete v2026.2.1 --yes
git tag -d v2026.2.1
git push origin :refs/tags/v2026.2.1

# Fix, commit, push, then re-tag
git tag v2026.2.1
git push origin v2026.2.1
```

---

## Manual Publish Trigger

If auto-trigger didn't fire or you need to re-publish:

```bash
RUN_ID=$(gh run list --workflow=release-build.yml --limit 1 --json databaseId --jq '.[0].databaseId')

gh workflow run release-publish.yml \
  -f version=2026.2.1 \
  -f run_id=$RUN_ID
```

Optional flags: `-f skip_sourceforge=true`, `-f skip_docs_deploy=true`

---

## Troubleshooting

```bash
# View failed job logs
gh run view <run-id> --log-failed

# Re-run only failed jobs
gh run rerun <run-id> --failed

# Cancel a run
gh run cancel <run-id>

# Delete a run
gh run delete <run-id>
```

---

## Release Artifacts

The release build produces 8 assets:

| Asset | Source Job |
|-------|-----------|
| `objeck-windows-x64_*.msi` | Build windows-x64 |
| `objeck-windows-x64_*.zip` | Build windows-x64 |
| `objeck-windows-arm64_*.msi` | Build windows-arm64 |
| `objeck-windows-arm64_*.zip` | Build windows-arm64 |
| `objeck-linux-x64_*.tgz` | Build linux-x64 |
| `objeck-linux-arm64_*.tgz` | Build linux-arm64 |
| `objeck-macos-arm64_*.tgz` | Build macos-arm64 |
| `objeck-lsp_*.zip` | Build LSP Package |

API docs are deployed separately to `objeck.org/api/v{VERSION}/` with a `latest` symlink.

---

## Pre-Release Checklist

Before tagging:

- [ ] Version updated in `core/shared/version.h`
- [ ] Release notes in `README.md`, `docs/readme.txt`, `docs/readme.html`
- [ ] Download URLs in `README.md` Quick Start section
- [ ] Web playground version in `programs/web-playground/frontend/index.html`
- [ ] LSP repo updated (`objeck-lsp`) if keywords changed
- [ ] All CI tests passing on master

After publish:

- [ ] GitHub Release body updated with proper notes
- [ ] All 8 assets present
- [ ] Sourceforge upload verified
- [ ] API docs live at objeck.org
