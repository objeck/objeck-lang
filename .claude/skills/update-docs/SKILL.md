---
name: update-docs
description: Update release documentation (README.md, CHANGELOG.md, docs/readme.html, docs/readme.txt) for a new version
allowed-tools: Read Edit Write Bash Grep Glob
argument-hint: "[version] [summary] e.g. 2026.4.2 \"DTLS support, LTO optimization\""
---

Update the Objeck release documentation for a new version.

## Overview

This skill updates the four documentation files that track version history:
- `README.md` (GitHub landing page — What's New section)
- `CHANGELOG.md` (detailed changelog with categorized entries)
- `docs/readme.html` (website changelog)
- `docs/readme.txt` (plain text release notes)

## Arguments

- `$ARGUMENTS` may contain a version number and optional summary
- If no version is provided, read the current version from `core/shared/version.h` (`VERSION_STRING` line)

## Steps

### 1. Gather version info

Read `core/shared/version.h` to get the current version string. If `$ARGUMENTS` provides a version, use that instead.

Ask the user for the list of changes if not provided. The user should provide bullet points of what changed in this release.

### 2. Update README.md

Read the `## What's New` section in `README.md`.

- **Add** a new version section at the top of the "What's New" area (after the Web Playground paragraph, before the previous version)
- **Remove** the oldest version section to keep only the 3 most recent versions visible
- The format follows the existing pattern:

```markdown
**vYEAR.MONTH.RELEASE**
  * **Feature name** — description
  * **Another feature** — description
  * Bug fix or minor item description
```

### 3. Update docs/readme.html

Read `docs/readme.html`.

- **Update** the `<p>` summary at the top of `<main>` with a brief summary of the new version
- **Add** a new `<h3><u>vX.Y.Z</u></h3>` section with `<ul><li>` items at the top (before the previous version)
- **Remove** the underline `<u>` tag from the previous version's `<h3>` (only the latest version gets underlined)
- Keep the existing older versions — don't remove any from the HTML
- The format follows the existing pattern:

```html
<h3><u>vYEAR.MONTH.RELEASE</u></h3>
 <ul>
     <li><strong>Feature</strong> &mdash; description</li>
     <li>Bug fix description</li>
 </ul>
```

### 4. Update docs/readme.txt

Read `docs/readme.txt`.

- **Add** a new version header and change list at the top of the file
- Keep all existing versions — don't remove any from the text file
- The format follows the existing pattern:

```
vYEAR.MONTH.RELEASE (Month Day, Year)
===
Brief one-line summary.

vYEAR.MONTH.RELEASE
- Feature or change description
- Another change
```

Use the current date for the release date.

### 5. Update CHANGELOG.md

Read `CHANGELOG.md`.

- **Add** a new `## [vX.Y.Z] - YYYY-MM-DD` section at the top (after the header, before the previous version)
- Group changes into subsections following the existing pattern: `### New Features`, `### Bug Fixes`, `### Performance`, `### Libraries`, `### Infrastructure`, etc. — only include subsections that have entries
- Use the same level of detail as the existing entries — more detailed than README.md bullets, with specific function/class names and technical context
- Keep all existing versions — don't remove any
- The format follows the existing pattern:

```markdown
## [vYEAR.MONTH.RELEASE] - YYYY-MM-DD

### New Features
- **Feature name**: Description with technical detail

### Bug Fixes
- Fixed specific issue with context

### Infrastructure
- CI/tooling changes
```

### 6. Verify

- Show the user the changes made to each file (brief summary, not full diffs)
- Do NOT commit automatically — let the user decide when to commit
