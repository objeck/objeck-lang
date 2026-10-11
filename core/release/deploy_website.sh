#!/usr/bin/env bash
#
# Deploys objeck.org: the generated API reference, and the site's own pages.
#
# IT USED TO DO ONLY THE API REFERENCE, which is why objeck.org kept going
# stale in the other half. v2026.10.0 published with api/latest correctly at
# v2026.10.0 while the site's own readme.html still offered "Download v2026.9.7"
# -- the page a reader actually lands on. docs/web/ is the source of those pages
# and nothing deployed it, so every release needed someone to remember an
# undocumented hand-upload. The `docs` manual-step hint named this script, which
# did half the job and reported success.
#
# ONE implementation, called BOTH from release-publish.yml and by hand. The
# previous arrangement had the rsync written inline in the workflow and nothing
# equivalent for a maintainer, and it had never worked: it targeted
# /var/www/objeck.org/api/latest/, a VPS layout, while objeck.org is GoDaddy
# shared hosting serving ~/public_html. With the secrets unset the step was
# skipped, so the wrong path was never exercised -- v2026.8.4 published with the
# site still serving v2026.8.3 docs while the pipeline reported the deploy
# green. A single script means a path fix cannot land in one caller and not the
# other.
#
#   deploy_website.sh [--source <dir|zip>] [--site-source <dir>]
#                     [--api-only | --site-only]
#                     [--user U] [--host H] [--key K]
#                     [--version V] [--dry-run] [--no-verify]
#
# With no mode flag it does both halves, so one call leaves nothing stale.
#
# Needs only ssh and tar locally -- NOT rsync, which Git for Windows does not
# ship and which has to exist at both ends. The reference tree is uploaded
# beside the live one and swapped in, so a reader never sees a half-updated
# directory and a failed upload leaves the old tree serving.
#
# The site pages CANNOT be swapped that way: they live directly in
# ~/public_html, alongside api/, .htaccess and objeck-release.pub, so replacing
# that directory would delete things this script knows nothing about. They are
# uploaded per file instead -- staged, hash-checked on the server, then moved
# into place one atomic rename at a time -- and a remote file with no local
# counterpart is left alone rather than treated as stale.
#
# Exits non-zero on any refusal. Every refusal is deliberate: this writes to a
# live site, and "deployed something wrong" is worse than "did not deploy".
set -u -o pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"

SOURCE=""
SITE_SOURCE=""
DO_API=1
DO_SITE=1
USER_NAME="${OBJECK_ORG_USER:-objecklang}"
HOST="${OBJECK_ORG_HOST:-objeck.org}"
KEY=""
VERSION=""
DRY_RUN=0
VERIFY=1

# api/latest/ is a REAL DIRECTORY, not a symlink to a versioned one: an earlier
# workflow uploaded to api/v<VERSION>/ and ran `ln -sfn v<VERSION> latest`,
# which against a real directory creates latest/v<VERSION> and leaves latest
# stale.
REMOTE_PARENT="public_html/api"
REMOTE_NAME="latest"

# The site's own pages. ~/public_html is shared with api/ and with files no
# release produces, so this is a per-file destination, never a directory to
# replace.
SITE_REMOTE="public_html"

# Below this many pages the source is not a real doc tree. A healthy build is
# ~509; an api.zip containing only the three style files shipped once and
# nothing noticed.
MIN_PAGES=400

while [ $# -gt 0 ]; do
  case "$1" in
    --source)      SOURCE="$2"; shift 2 ;;
    --site-source) SITE_SOURCE="$2"; shift 2 ;;
    --api-only)    DO_SITE=0; shift ;;
    --site-only)   DO_API=0; shift ;;
    --user)        USER_NAME="$2"; shift 2 ;;
    --host)        HOST="$2"; shift 2 ;;
    --key)         KEY="$2"; shift 2 ;;
    --version)     VERSION="$2"; shift 2 ;;
    --dry-run)     DRY_RUN=1; shift ;;
    --no-verify)   VERIFY=0; shift ;;
    -h|--help)     sed -n '3,41p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *)            echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

die() { echo "ERROR: $*" >&2; exit 1; }
note() { echo "  $*"; }

# ---------------------------------------------------------------- version ----
if [ -z "$VERSION" ]; then
  VERSION="$(sed -n 's/.*VERSION_STRING L"\([^"]*\)".*/\1/p' \
             "$REPO/core/shared/version.h" | head -1)"
  [ -n "$VERSION" ] || die "could not read VERSION_STRING from core/shared/version.h"
fi
note "version: $VERSION"

# ------------------------------------------------------------ transport ------
# Shared by both halves, so it is settled before either runs.
# -T: no pty for a non-interactive command. LogLevel=ERROR: objeck.org's sshd
# greets every connection with a post-quantum advisory, which is not this
# script's news to deliver and buried the output it does have to show.
SSH_OPTS="-o BatchMode=yes -o ConnectTimeout=20 -T -o LogLevel=ERROR"
[ -n "$KEY" ] && SSH_OPTS="$SSH_OPTS -i $KEY"
TARGET="$USER_NAME@$HOST"

command -v tar >/dev/null || die "tar is required"
command -v sha256sum >/dev/null || die "sha256sum is required"

# One cleanup for both halves. TMP holds an unzipped api.zip; WORKERR catches
# ssh's stderr so a failure can be reported with its reason instead of without.
TMP=""
WORKERR="$(mktemp 2>/dev/null || echo "${TMPDIR:-/tmp}/deploy_website.$$.err")"
cleanup() {
  [ -n "$TMP" ] && rm -rf "$TMP"
  rm -f "$WORKERR"
}
trap cleanup EXIT

# ======================================================== the site's pages ===
if [ "$DO_SITE" = 1 ]; then
  [ -n "$SITE_SOURCE" ] || SITE_SOURCE="$REPO/docs/web"
  [ -d "$SITE_SOURCE" ] || die "site source does not exist: $SITE_SOURCE"

  echo
  note "site:    $SITE_SOURCE"

  # Both must be there. A source directory that happens to exist but holds
  # neither of the two pages the site is built around is not docs/web.
  for required in readme.html index.html; do
    [ -f "$SITE_SOURCE/$required" ] || \
      die "$SITE_SOURCE has no $required -- that is not the docs/web tree"
  done

  # readme.html carries the Download button, and the button is hardcoded. It is
  # the single thing that goes stale: it sat on v2026.8.1 through the whole of
  # v2026.8.2, and on v2026.9.7 while v2026.10.0 was live. Uploading a page that
  # offers the previous release is worse than leaving the old one, because it
  # looks freshly deployed.
  if ! grep -q "Download v$VERSION" "$SITE_SOURCE/readme.html"; then
    offers="$(grep -oE 'Download v[0-9.]+' "$SITE_SOURCE/readme.html" | head -1)"
    die "$SITE_SOURCE/readme.html offers '${offers:-no Download button at all}', expected 'Download v$VERSION' -- update docs/web before deploying"
  fi
  note "button:  Download v$VERSION"

  SITE_FILES="$(cd "$SITE_SOURCE" && find . -type f -printf '%P\n' 2>/dev/null \
                || (cd "$SITE_SOURCE" && find . -type f | sed 's|^\./||'))"
  [ -n "$SITE_FILES" ] || die "no files found under $SITE_SOURCE"
  note "files:   $(printf '%s\n' "$SITE_FILES" | grep -c .)"

  ssh $SSH_OPTS "$TARGET" "test -d ~/$SITE_REMOTE" 2>/dev/null || \
    die "$TARGET:~/$SITE_REMOTE does not exist (or SSH failed) -- not creating it from here"

  # What is live, so only what differs travels and the output says what changed
  # rather than "deployed" every time. A path absent remotely hashes to nothing
  # and therefore counts as changed.
  #
  # Retried, and the SSH error SHOWN. This is shared hosting and it drops a
  # connection now and then: a single hiccup used to end the deploy with
  # "could not read the live site's hashes" and the reason sent to /dev/null,
  # which reads as a permanent fault on a site that was perfectly reachable a
  # second later. Read-only, so a retry cannot half-do anything.
  REMOTE_HASHES=""
  for attempt in 1 2 3; do
    if REMOTE_HASHES="$(printf '%s\n' "$SITE_FILES" | \
      ssh $SSH_OPTS "$TARGET" "cd ~/$SITE_REMOTE && while read -r f; do
          if [ -f \"\$f\" ]; then sha256sum \"\$f\"; else echo \"- \$f\"; fi
        done" 2>"$WORKERR")" && [ -n "$REMOTE_HASHES" ]; then
      break
    fi
    REMOTE_HASHES=""
    [ "$attempt" = 3 ] && break
    note "  could not read the live hashes (attempt $attempt/3), retrying"
    sleep 3
  done
  if [ -z "$REMOTE_HASHES" ]; then
    [ -s "$WORKERR" ] && sed 's/^/  ssh: /' "$WORKERR" >&2
    die "could not read the live site's hashes after 3 attempts"
  fi

  CHANGED=""
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    local_h="$(sha256sum "$SITE_SOURCE/$f" | awk '{print $1}')"
    remote_h="$(printf '%s\n' "$REMOTE_HASHES" | awk -v p="$f" '$2==p {print $1; exit}')"
    if [ "$local_h" != "$remote_h" ]; then
      CHANGED="$CHANGED$f
"
      if [ "$remote_h" = "-" ] || [ -z "$remote_h" ]; then
        note "  new      $f"
      else
        note "  changed  $f"
      fi
    fi
  done <<SITE_LIST
$SITE_FILES
SITE_LIST

  if [ -z "$CHANGED" ]; then
    note "site pages already current -- nothing to upload"
  elif [ "$DRY_RUN" = 1 ]; then
    note "DRY RUN -- would upload $(printf '%s' "$CHANGED" | grep -c .) file(s) to ~/$SITE_REMOTE"
  else
    # Staged, hash-checked on the server, then moved into place. The move is one
    # rename per file, which is atomic, so a reader gets the old page or the new
    # one and never a truncated upload. Nothing is deleted: a remote file with
    # no local counterpart -- .htaccess, objeck-release.pub, api/ -- is none of
    # this script's business.
    printf '%s' "$CHANGED" | tar -czf - -C "$SITE_SOURCE" -T - | \
      ssh $SSH_OPTS "$TARGET" "
        set -e
        cd ~/$SITE_REMOTE
        rm -rf .site.new .site.list
        mkdir -p .site.new
        tar -xzf - -C .site.new
        ( cd .site.new && find . -type f | sed 's|^\./||' ) > .site.list
        while read -r f; do
          want=\$(sha256sum \".site.new/\$f\" | awk '{print \$1}')
          mkdir -p \"\$(dirname \"\$f\")\"
          # The staging name is the path with its slashes flattened, so
          # style/styles.css cannot collide with a styles.css at the root.
          tmp=\".\$(echo \"\$f\" | tr / _).incoming\"
          cp -p \".site.new/\$f\" \"\$tmp\"
          got=\$(sha256sum \"\$tmp\" | awk '{print \$1}')
          if [ \"\$got\" != \"\$want\" ]; then
            echo \"ERROR: \$f arrived corrupt; leaving the live page alone\" >&2
            rm -f \"\$tmp\"
            exit 1
          fi
          mv \"\$tmp\" \"\$f\"
          chmod 664 \"\$f\" 2>/dev/null || true
          echo \"  placed   \$f\"
        done < .site.list
        rm -rf .site.new .site.list
      " || die "site upload failed -- any page not reported as placed was left as it was"
  fi

  # What a reader gets, cache and all. The upload exiting 0 is not the site
  # serving the new page.
  if [ "$VERIFY" = 1 ] && [ "$DRY_RUN" = 0 ] && command -v curl >/dev/null; then
    live_btn="$(curl -fsSL --max-time 30 "https://www.$HOST/readme.html" 2>/dev/null \
                | grep -oE 'Download v[0-9.]+' | head -1)"
    if [ -z "$live_btn" ]; then
      echo "WARNING: could not read a Download button from https://www.$HOST/readme.html" >&2
      echo "  The upload reported success; confirm by hand before announcing it." >&2
    elif [ "$live_btn" != "Download v$VERSION" ]; then
      die "https://www.$HOST/readme.html still offers '$live_btn', expected 'Download v$VERSION'"
    else
      note "live:    https://www.$HOST/readme.html offers $live_btn"
    fi
  fi
fi

# A dry run must not sign off as a deploy. The first version of this line said
# "site pages deployed" after --dry-run had written nothing, which is the kind
# of success message this whole file exists to stop producing.
if [ "$DO_API" = 0 ]; then
  echo
  if [ "$DRY_RUN" = 1 ]; then
    echo "DRY RUN -- validated and reachable, nothing written (v$VERSION)"
  else
    echo "site pages deployed to https://www.$HOST/ (v$VERSION)"
  fi
  exit 0
fi

# ---------------------------------------------------------------- source -----
# Prefer the freshly generated tree. The committed api.zip is produced by hand
# and drifts: on 2026-10-11 it was stamped v2026.10.0 (correct -- the bump was
# 10-01) while its CONTENT predated API.Inference (#1051, 10-08) and API.Models
# (#1062, 10-09), so it carried the right version and was missing two whole
# libraries. A correct stamp is not evidence of correct content.
if [ -z "$SOURCE" ]; then
  for cand in "$REPO/core/release/deploy-x64/doc/api" \
              "$REPO/core/release/deploy-arm64/doc/api" \
              "$REPO/docs/api.zip"; do
    if [ -e "$cand" ]; then SOURCE="$cand"; break; fi
  done
  [ -n "$SOURCE" ] || die "no doc source found; run code_doc64.cmd <arch> deploy first"
fi

if [ -d "$SOURCE" ]; then
  PAGES_DIR="$SOURCE"
elif [ -f "$SOURCE" ]; then
  command -v unzip >/dev/null || die "unzip is needed to read $SOURCE"
  TMP="$(mktemp -d)"
  unzip -q "$SOURCE" -d "$TMP" || die "could not unzip $SOURCE"
  PAGES_DIR="$TMP/api"           # an api.zip holds api/ at its root
  [ -d "$PAGES_DIR" ] || PAGES_DIR="$TMP"
else
  die "source does not exist: $SOURCE"
fi
note "source:  $SOURCE"

# ------------------------------------------------------- source is sane ------
PAGES="$(find "$PAGES_DIR" -maxdepth 1 -name '*.html' | wc -l | tr -d ' ')"
note "pages:   $PAGES"
[ "$PAGES" -ge "$MIN_PAGES" ] || \
  die "only $PAGES pages (expected >= $MIN_PAGES) -- refusing to publish a partial reference"

# Every BUNDLE declared in lib_src should have at least one page. Per bundle ON
# PURPOSE: a total page count cannot see two missing libraries among 500 files,
# which is how API.Inference and API.Models were absent from the published
# reference for three days while the count looked healthy.
#
# Keyed on the bundle, not the source filename, because pages are named after
# the bundle: ml_core.obs, ml_tree.obs and five more all declare System.ML and
# produce system.ml-*.html. Checking filenames reported 26 libraries missing
# when nothing was, and a check that cries wolf 26 times is one nobody reads.
MISSING=""
BUNDLES="$(grep -hoE '^bundle +[A-Za-z0-9_.]+' "$REPO/core/compiler/lib_src"/*.obs 2>/dev/null \
           | awk '{print tolower($2)}' | sort -u)"
[ -n "$BUNDLES" ] || echo "WARNING: found no bundle declarations in lib_src; skipping the per-bundle check" >&2
for b in $BUNDLES; do
  # Default.* is lang.obs's implicit bundle and has no pages of its own
  case "$b" in default*) continue ;; esac
  if ! ls "$PAGES_DIR" | grep -qiE "^${b}([.-]|\.html)"; then
    MISSING="$MISSING $b"
  fi
done
if [ -n "$MISSING" ]; then
  echo "WARNING: no pages found for:$MISSING" >&2
  echo "  A library with no pages is invisible in the published reference. If" >&2
  echo "  these are genuinely undocumented that is a doc-comment problem, not a" >&2
  echo "  deploy problem -- regenerate before publishing." >&2
fi

# The stamp, read from a page the listing actually contains. Naming a file that
# is not there makes the check pass on any version -- the bump skill records
# that exact trap for api.zip.
SAMPLE="$(find "$PAGES_DIR" -maxdepth 1 -name '*.html' | head -1)"
STAMP="$(grep -ohE 'class="version">v[0-9.]+' "$SAMPLE" 2>/dev/null | head -1 | sed 's/.*>v//')"
if [ -n "$STAMP" ]; then
  note "stamp:   v$STAMP"
  [ "$STAMP" = "$VERSION" ] || \
    die "pages are stamped v$STAMP but the tree is $VERSION -- regenerate before publishing"
else
  echo "WARNING: no version stamp found in $(basename "$SAMPLE"); cannot confirm freshness" >&2
fi

# ------------------------------------------------------------ transport ------
# The destination must already exist. Uploading into a path that does not is how
# a deploy creates a plausible-looking tree somewhere nobody serves -- the
# failure this script was written to end. The directory is created once, by
# hand, deliberately.
ssh $SSH_OPTS "$TARGET" "test -d ~/$REMOTE_PARENT/$REMOTE_NAME" 2>/dev/null || \
  die "$TARGET:~/$REMOTE_PARENT/$REMOTE_NAME does not exist (or SSH failed) -- not creating it from here"
note "remote:  $TARGET:~/$REMOTE_PARENT/$REMOTE_NAME"

if [ "$DRY_RUN" = 1 ]; then
  echo
  echo "  DRY RUN -- validated and reachable, nothing written"
  echo "  would upload $PAGES pages (v$VERSION) to ~/$REMOTE_PARENT/$REMOTE_NAME"
  exit 0
fi

# Upload beside the live tree, then swap. A reader never sees a partially
# written directory, and a transfer that dies leaves the old tree serving.
echo
tar -czf - -C "$PAGES_DIR" . | \
  ssh $SSH_OPTS "$TARGET" "
    set -e
    cd ~/$REMOTE_PARENT
    rm -rf .${REMOTE_NAME}.new .${REMOTE_NAME}.old
    mkdir -p .${REMOTE_NAME}.new
    tar -xzf - -C .${REMOTE_NAME}.new
    n=\$(ls .${REMOTE_NAME}.new/*.html 2>/dev/null | wc -l)
    if [ \"\$n\" -lt $MIN_PAGES ]; then
      echo \"ERROR: only \$n pages arrived; leaving the live tree alone\" >&2
      rm -rf .${REMOTE_NAME}.new
      exit 1
    fi
    mv $REMOTE_NAME .${REMOTE_NAME}.old
    mv .${REMOTE_NAME}.new $REMOTE_NAME
    rm -rf .${REMOTE_NAME}.old
    echo \"  swapped in \$n pages\"
  " || die "upload or swap failed -- the live tree was left as it was"

# ------------------------------------------------------------- verify --------
# "the upload exited 0" is not "the site serves the new docs". Check over
# HTTPS, which is what a reader gets, including any caching in front.
if [ "$VERIFY" = 1 ] && command -v curl >/dev/null; then
  base="https://www.$HOST/api/$REMOTE_NAME"
  page="$(basename "$SAMPLE")"
  live="$(curl -fsSL --max-time 30 "$base/$page" 2>/dev/null \
          | grep -ohE 'class="version">v[0-9.]+' | head -1 | sed 's/.*>v//')"
  if [ -z "$live" ]; then
    echo "WARNING: could not read a version stamp from $base/$page" >&2
    echo "  The upload reported success; confirm by hand before announcing it." >&2
  elif [ "$live" != "$VERSION" ]; then
    die "$base/$page still reports v$live, expected v$VERSION"
  else
    note "live:    $base/$page reports v$live"
  fi
fi

echo
echo "published $PAGES pages to https://www.$HOST/api/$REMOTE_NAME/ (v$VERSION)"
