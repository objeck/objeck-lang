#!/usr/bin/env bash
#
# Deploys the generated API reference to objeck.org.
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
#   deploy_website.sh [--source <dir|zip>] [--user U] [--host H] [--key K]
#                     [--version V] [--dry-run] [--no-verify]
#
# Needs only ssh and tar locally -- NOT rsync, which Git for Windows does not
# ship and which has to exist at both ends. The tree is uploaded beside the live
# one and swapped in, so a reader never sees a half-updated directory and a
# failed upload leaves the old tree serving.
#
# Exits non-zero on any refusal. Every refusal is deliberate: this writes to a
# live site, and "deployed something wrong" is worse than "did not deploy".
set -u -o pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"

SOURCE=""
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

# Below this many pages the source is not a real doc tree. A healthy build is
# ~509; an api.zip containing only the three style files shipped once and
# nothing noticed.
MIN_PAGES=400

while [ $# -gt 0 ]; do
  case "$1" in
    --source)     SOURCE="$2"; shift 2 ;;
    --user)       USER_NAME="$2"; shift 2 ;;
    --host)       HOST="$2"; shift 2 ;;
    --key)        KEY="$2"; shift 2 ;;
    --version)    VERSION="$2"; shift 2 ;;
    --dry-run)    DRY_RUN=1; shift ;;
    --no-verify)  VERIFY=0; shift ;;
    -h|--help)    sed -n '3,24p' "${BASH_SOURCE[0]}"; exit 0 ;;
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

# ---------------------------------------------------------------- source -----
# Prefer the freshly generated tree. The committed api.zip is produced by hand
# and drifts: on 2026-10-11 it was stamped v2026.10.0 (correct -- the bump was
# 10-01) while its CONTENT predated API.Inference (#1051, 10-08) and API.Models
# (#1062, 10-09), so it carried the right version and was missing two whole
# libraries. A correct stamp is not evidence of correct content.
TMP=""
cleanup() { [ -n "$TMP" ] && rm -rf "$TMP"; }
trap cleanup EXIT

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
SSH_OPTS="-o BatchMode=yes -o ConnectTimeout=20"
[ -n "$KEY" ] && SSH_OPTS="$SSH_OPTS -i $KEY"
TARGET="$USER_NAME@$HOST"

command -v tar >/dev/null || die "tar is required"

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
