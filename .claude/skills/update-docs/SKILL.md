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

**Two further notes surfaces this skill does NOT own** — if you are running it standalone,
they will be left stale and nothing downstream will complain:
- `docs/web/` (`readme.html`, `index.html`) — the objeck.org copy, plus its hardcoded
  `Download v<VERSION>` button. Owned by the `release` skill, step 4.
- `programs/deploy/util/readme/readme.json` and its `readme.json.in` template — the README
  that ships **inside** the release archive. Written by `bump-version` and re-checked by
  `release` step 2d; only `@VERSION@`/`@YEAR@` are templated, so the feature list is
  hand-maintained and goes stale silently.

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

### 2b. Update README.md's **Quick Start** and **Downloads** (not just What's New)

Step 2 only touches the "What's New" list. Three other places in `README.md` carry a hardcoded
version and go stale silently -- none of them is a changelog, so nothing about editing the
changelog surfaces them:

- **Quick Start** -- a copy-pasteable install block with the release URL *and* the tarball
  name in it. If it is stale the very first command a new user runs downloads the previous
  release; if the old tag is ever deleted it 404s. At v2026.8.3 it still pointed at v2026.8.2.
- **Downloads** -- the `**Latest Release:** [vX.Y.Z](...)` line. The table below it uses
  `/releases/latest`, which needs no edit; this one line does.
- The static release **badge** near the top (`img.shields.io/badge/release-vX.Y.Z-blue`).
  It never calls the API, so it shows the previous version until edited by hand.

```bash
# Every hardcoded version left in README.md. Historical changelog entries are
# expected to name old versions; anything OUTSIDE "## What's New" is a bug.
grep -n "2026\.[0-9]*\.[0-9]*" README.md
```

**Then prove the install block works, rather than eyeballing the version number.** A URL that
looks right and 404s is the same failure as a link that renders and goes nowhere:

```bash
for u in $(grep -oE 'https://github.com/objeck/objeck-lang/releases/download/[^ )]*' README.md | sort -u); do
  curl -s -o /dev/null -w "%{http_code}  $u
" -L "$u"      # must be 200
done
```

Leave the extract paths alone unless the archive layout changed -- the tarball's single
top-level directory is `objeck-lang/`, so `./objeck-lang/bin` and `./objeck-lang/lib` are
correct. Confirm with `tar --force-local -tzf <archive>.tgz | cut -d/ -f1 | sort -u`
(**`--force-local` is required on Windows**, or GNU tar reads `C:/...` as a remote host).

**Do not restate a blanket signing promise.** README carried "All Windows installers are
digitally signed" throughout the four months every Windows MSI shipped unsigned. Say what is
verifiable and how to check it, not what is supposed to be true.

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

- **Cross-check all four files against `CHANGELOG.md` for the version.** They must tell a consistent story — every flagship feature in the changelog should appear in README.md, `docs/readme.html`, and `docs/readme.txt` (each at its own altitude). A version is often partially pre-authored (e.g. `CHANGELOG` + `README` done, `readme.html`/`readme.txt` not, or a draft `readme.txt` written before later features landed); finish the laggards rather than assuming one file represents all.
- **Give flagship features prominence.** A headline feature (e.g. a debugger overhaul, a new language feature) belongs near the top as its own bullet, not buried as a one-line "fix". Lead with what users will care about most.
- Confirm each file has the new version at the top and no stale current-version strings remain.
  Grep for the PREVIOUS version across every doc surface at once -- outside a historical
  changelog entry, every hit is a bug:

  ```bash
  PREV=2026.8.2   # the version being superseded
  grep -rn "$PREV" README.md docs/readme.html docs/readme.txt docs/web/        programs/deploy/util/readme/readme.json.in 2>/dev/null
  ```

  Known repeat offenders, each of which shipped stale at least once: README's **Quick Start**
  install URL and tarball name, README's **Downloads → Latest Release** line, README's static
  release **badge**, and `docs/web/readme.html`'s hardcoded **`Download v<OLD>`** button (which
  sat two releases behind, on v2026.8.1, through the whole of v2026.8.2).
- **Verify links resolve; do not just read them.** Every download URL in README must return
  HTTP 200. A stale-but-plausible URL and a dead link are indistinguishable by eye.
- Show the user the changes made to each file (brief summary, not full diffs)
- Do NOT commit automatically — let the user decide when to commit
