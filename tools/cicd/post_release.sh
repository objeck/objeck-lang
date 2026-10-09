#!/usr/bin/env bash
#
# post_release.sh <VERSION> [--sign] [--body FILE] [--playground]
#
# ONE step for everything after release-publish.yml goes green. This used to be
# four prose sections in .claude/skills/release (steps 9, 10, 11, 11b), which
# meant the commands were re-derived by hand every release -- and re-broken the
# same ways every release. Every gate below exists because a real release shipped
# wrong when it was checked loosely or not at all:
#
#   * v2026.5.0/5.1 shipped with no obr VM       -> assert the FULL binary set,
#                                                   in EVERY archive
#   * v2026.8.0 shipped with no obu at all       -> obu is in the required list;
#                                                   a CI-tested tool is not a
#                                                   packaged one
#   * v2026.4.0..v2026.8.2 shipped UNSIGNED      -> verify the SIGNATURE of the
#     while the notes claimed otherwise             PUBLISHED file, never the
#                                                   file's existence
#   * v2026.8.3 SHA256SUMS described pre-signing -> signing rewrites the MSIs;
#     bytes, so verification FAILED for users       regenerate and prove it
#   * regenerating it then invalidated the        -> re-sign, and verify the
#     SIGNATURE, which since #723 phase 3            SIGNATURE against the
#     makes the release UNINSTALLABLE                PUBLISHED manifest [4b]
#   * the body advertised objeck-lsp-X.zip       -> diff every asset filename in
#     (hyphen) which is not an asset                the body against reality
#   * the body linked compare/v...vX            -> reject unsubstituted markers
#
# Exit 0 only when every gate passes. Read-only unless --sign/--body/--playground
# are given.
set -uo pipefail

VERSION="${1:-}"
[ -n "$VERSION" ] || { echo "usage: post_release.sh <VERSION> [--sign] [--body FILE] [--playground]"; exit 2; }
shift
TAG="v${VERSION}"
REPO="objeck/objeck-lang"
DO_SIGN=0; DO_PLAYGROUND=0; BODY_FILE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --sign)       DO_SIGN=1 ;;
    --playground) DO_PLAYGROUND=1 ;;
    --body)       shift; BODY_FILE="${1:-}" ;;
    *) echo "unknown option: $1"; exit 2 ;;
  esac
  shift
done

WORK=$(mktemp -d 2>/dev/null || echo "${TMPDIR:-/tmp}/post_release.$$")
mkdir -p "$WORK"
FAIL=0
note() { echo "  $*"; }
bad()  { echo "  FAIL: $*"; FAIL=1; }

echo "=============================================================="
echo " post-release gates for $TAG"
echo "=============================================================="

# ---------------------------------------------------------------- 1. assets
echo
echo "[1] published assets"
gh release view "$TAG" -R "$REPO" --json isDraft,assets \
   --jq '"draft=" + (.isDraft|tostring), (.assets[].name)' > "$WORK/assets.raw" 2>/dev/null \
   || { echo "  cannot read release $TAG"; exit 1; }
grep -q '^draft=false$' "$WORK/assets.raw" || bad "release is still a DRAFT"
grep -v '^draft=' "$WORK/assets.raw" | sort > "$WORK/have.txt"
sed 's/^/  /' "$WORK/have.txt"

for want in "objeck-windows-x64_${VERSION}.msi" \
            "objeck-windows-arm64_${VERSION}.msi" \
            "objeck-macos-arm64_${VERSION}.pkg" \
            "objeck-macos-arm64_${VERSION}.zip" \
            "SHA256SUMS" \
            "SHA256SUMS.minisig"; do
  grep -qx "$want" "$WORK/have.txt" || bad "missing required asset: $want"
done

# --------------------------------------------------- 2. binaries in archives
echo
echo "[2] binaries inside every POSIX archive"
# NOTE: --force-local is REQUIRED. Without it GNU tar reads a Windows path
# 'C:/...' as a remote host 'C' (rsh host:path syntax), fails to connect, and
# reports EVERY binary missing -- a false 'release broken' on a healthy release.
( cd "$WORK" && gh release download "$TAG" -R "$REPO" --pattern "objeck-*.tgz" --clobber >/dev/null 2>&1 )
shopt -s nullglob
ARCHIVES=("$WORK"/objeck-*.tgz)
[ ${#ARCHIVES[@]} -gt 0 ] || bad "no .tgz archives downloaded"
for tgz in "${ARCHIVES[@]}"; do
  base=$(basename "$tgz")
  list=$( cd "$WORK" && tar --force-local -tzf "./$base" 2>/dev/null )
  n=$(printf '%s\n' "$list" | grep -c . )
  [ "$n" -gt 100 ] || bad "$base: only $n entries -- archive did not open"
  for bin in obc obr obd obi obb obu; do
    printf '%s\n' "$list" | grep -q "/bin/$bin\$" || bad "$base: MISSING $bin"
  done
  note "$base: $n entries, all of obc obr obd obi obb obu present"
done
shopt -u nullglob

# ------------------------------------------------------------- 3. signing
echo
echo "[3] Windows installer signatures"
if [ "$DO_SIGN" -eq 1 ]; then
  note "running sign_release.cmd (needs the eToken plugged in; expect a password prompt)"
  cmd //c ".\\tools\\cicd\\sign_release.cmd $VERSION" || bad "sign_release.cmd failed"
fi
# Verify the PUBLISHED files, never the staging copies.
powershell -NoProfile -ExecutionPolicy Bypass -File tools/cicd/check_release_signatures.ps1 \
  "$VERSION" -Quiet > "$WORK/sig.txt" 2>&1
SIG_RC=$?
sed 's/^/  /' "$WORK/sig.txt" | grep -E "OK|UNSIGNED|signed" | head -6
[ "$SIG_RC" -eq 0 ] || bad "installers are UNSIGNED -- run with --sign (eToken required)"

# --------------------------------------------- 4. SHA256SUMS matches reality
echo
echo "[4] SHA256SUMS describes the PUBLISHED bytes"
# Signing rewrites the MSIs, so a manifest generated before signing makes every
# user's verification FAIL -- worse than shipping no checksum.
# Do NOT suppress these. The manifest download failed once here and the error went
# to /dev/null, so the gate sailed on and died three lines later reading a file
# that was never fetched:
#
#   post_release.sh: line 123: /tmp/tmp.XXXX/SHA256SUMS: No such file or directory
#
# which reads as a broken script rather than as "the download failed" -- the same
# indistinguishable-failure problem this file's header warns about. Note also that
# the four downloads were newline-separated after a single '&&', so a failure of
# the first did not stop the rest; only the manifest is required, so check it.
( cd "$WORK" || exit 1
  gh release download "$TAG" -R "$REPO" --pattern "SHA256SUMS" --clobber 2>&1 | sed 's/^/    gh: /'
  for pat in "*.msi" "*.pkg" "*.zip"; do
    gh release download "$TAG" -R "$REPO" --pattern "$pat" --clobber >/dev/null 2>&1 || true
  done )
if [ ! -f "$WORK/SHA256SUMS" ]; then
  bad "could not download SHA256SUMS -- cannot verify the manifest (see gh output above)"
else
STALE=0
while read -r h n; do
  [ -f "$WORK/$n" ] || continue
  g=$(sha256sum "$WORK/$n" | awk '{print $1}')
  if [ "$h" = "$g" ]; then note "ok       $n"; else echo "  STALE    $n"; STALE=1; fi
done < "$WORK/SHA256SUMS"
if [ "$STALE" -eq 1 ]; then
  echo "  regenerating SHA256SUMS over the published files..."
  ( cd "$WORK"
    : > SHA256SUMS.new
    while read -r h n; do
      if [ -f "$n" ]; then sha256sum "$n" | sed 's|\*||' >> SHA256SUMS.new
      else echo "$h  $n" >> SHA256SUMS.new; fi
    done < SHA256SUMS
    mv SHA256SUMS.new SHA256SUMS
    gh release upload "$TAG" -R "$REPO" SHA256SUMS --clobber >/dev/null 2>&1 )
  # The published signature now describes the manifest we just replaced, and obu
  # refuses a manifest whose signature does not verify -- so stopping here would
  # leave a release no 'obu update' will install. Re-signing runs in CI, where
  # the key lives; this machine has the eToken and never needs the manifest key.
  echo "  re-signing the regenerated manifest (resign-manifest.yml)..."
  if gh workflow run resign-manifest.yml -R "$REPO" -f version="$VERSION" >/dev/null 2>&1; then
    note "re-sign requested -- gate [4b] below checks the result"
  else
    bad "could not start resign-manifest.yml; the published signature still describes the OLD manifest, so 'obu update' will refuse this release"
  fi
  # prove it, against a fresh download
  ( cd "$WORK" && rm -rf v && mkdir -p v && gh release download "$TAG" -R "$REPO" --pattern SHA256SUMS --dir v --clobber >/dev/null 2>&1 )
  RC=0
  while read -r h n; do
    [ -f "$WORK/$n" ] || continue
    g=$(sha256sum "$WORK/$n" | awk '{print $1}')
    [ "$h" = "$g" ] || RC=1
  done < "$WORK/v/SHA256SUMS"
  [ "$RC" -eq 0 ] && note "regenerated and re-verified against the published files" \
                  || bad "SHA256SUMS still does not match after regeneration"
fi
fi

# ------------------------------- 4b. the signature describes that manifest
echo
echo "[4b] SHA256SUMS.minisig verifies against the PUBLISHED SHA256SUMS"
# This is the property obu enforces since #723 phase 3: it refuses a manifest
# whose signature does not verify. A release whose manifest was regenerated
# after signing fails here -- and fails for every user running 'obu update',
# which is why it is a gate and not a note.
#
# Checked with obu itself where possible: it is the consumer whose refusal
# decides whether the release installs, it needs nothing installed on this
# machine, and it carries the trusted key compiled in. minisign is the fallback.
( cd "$WORK" && gh release download "$TAG" -R "$REPO" --pattern "SHA256SUMS.minisig" --clobber 2>&1 | sed 's/^/    gh: /' )
if [ ! -f "$WORK/SHA256SUMS.minisig" ]; then
  bad "no SHA256SUMS.minisig published -- obu refuses an unsigned manifest, so no obu can install $TAG"
else
  SIG_OBU=""
  for cand in "core/release/deploy-x64/bin/obu.exe" "core/release/deploy-x64/bin/obu" \
              "core/release/deploy/bin/obu.exe" "core/release/deploy/bin/obu"; do
    # -f only: Git Bash reports the extensionless name as present when just
    # obu.exe exists, and the name is handed to a program that opens it.
    if [ -f "$cand" ]; then SIG_OBU="$cand"; break; fi
  done
  if [ -n "$SIG_OBU" ]; then
    # 'verify' checks the signature before it reads a line of the manifest, so
    # naming the manifest as its own archive reaches the signature check and
    # nothing else. Exit 2 with a signature message is the failure to catch;
    # exit 1 (hash mismatch) means the signature PASSED.
    OUT=$("$SIG_OBU" verify "$WORK/SHA256SUMS" "$WORK/SHA256SUMS" 2>&1)
    case "$OUT" in
      *"SIGNATURE VERIFICATION FAILED"*|*"No signature beside the manifest"*)
        bad "the published signature does not verify against the published manifest -- 'obu update' will refuse $TAG. Run: gh workflow run resign-manifest.yml -f version=$VERSION" ;;
      *)
        note "ok       obu accepts the published manifest's signature" ;;
    esac
  elif command -v minisign >/dev/null 2>&1; then
    if minisign -V -p core/release/objeck-release.pub -x "$WORK/SHA256SUMS.minisig" -m "$WORK/SHA256SUMS" >/dev/null 2>&1; then
      note "ok       minisign accepts the published manifest's signature"
    else
      bad "the published signature does not verify against the published manifest -- 'obu update' will refuse $TAG. Run: gh workflow run resign-manifest.yml -f version=$VERSION"
    fi
  else
    bad "no obu and no minisign here, so the signature went UNCHECKED -- build obu or install minisign; an unverified release-critical property is what this script exists to prevent"
  fi
fi

# ------------------------------------------------------------- 5. body
echo
echo "[5] release notes name only files that exist"
if [ -n "$BODY_FILE" ]; then
  # Check the file first, and let gh's own error through. This reported
  # "could not set release body" for a file that `gh release edit` accepted
  # without complaint when run by hand a minute later -- with the reason sent to
  # /dev/null there was nothing to act on, and the gate looked like the defect.
  if [ ! -f "$BODY_FILE" ]; then
    bad "body file not found: $BODY_FILE"
  else
    ERR=$(gh release edit "$TAG" -R "$REPO" --notes-file "$BODY_FILE" 2>&1 >/dev/null) \
      && note "release body set from $BODY_FILE" \
      || bad "could not set release body: $ERR"
  fi
fi
gh release view "$TAG" -R "$REPO" --json body --jq .body > "$WORK/body.md" 2>/dev/null
# Precise asset shape: 'objeck-lang' in a repo URL is NOT an advertised asset.
grep -oE "objeck-[A-Za-z0-9]+(-[A-Za-z0-9]+)*_[0-9]+\.[0-9]+\.[0-9]+\.(msi|zip|pkg|tgz)" \
  "$WORK/body.md" | sort -u > "$WORK/named.txt"
MISS=$(comm -23 "$WORK/named.txt" "$WORK/have.txt")
[ -z "$MISS" ] && note "all $(grep -c . "$WORK/named.txt") named files exist" \
               || { echo "$MISS" | sed 's/^/  ADVERTISED BUT ABSENT: /'; FAIL=1; }
# unsubstituted template markers / dead compare links
grep -q 'compare/v\.\.\.' "$WORK/body.md" && bad "body has an unsubstituted compare link (compare/v...)"
# Unsubstituted template markers. Two rules, because '$UPPERCASE' is not by
# itself a defect: the body carries shell the reader is meant to run, and
# 'export PATH=$PATH:$(pwd)/bin' is correct. A flat '\$[A-Z_]+' failed v2026.9.7
# on exactly that line, and the message named neither the variable nor the line,
# so finding it meant re-grepping the body by hand.
#
#   1. ANY '$UPPERCASE' OUTSIDE a fenced code block. Prose has no reason to carry
#      one, so a marker that leaked into the text is still caught.
#   2. The pipeline's OWN names anywhere, fenced or not. '$VERSION' inside a fence
#      is worse than in prose: it is a command someone will copy and run.
#
# Both print the offending line, which the old check did not.
awk '/^[[:space:]]*```/ { fence = !fence; next } !fence' "$WORK/body.md" > "$WORK/body.prose"
PROSE_VARS=$(grep -nE '\$[A-Z_]+' "$WORK/body.prose" | head -5)
if [ -n "$PROSE_VARS" ]; then
  echo "$PROSE_VARS" | sed 's/^/  UNSUBSTITUTED (outside a code block): /'
  bad "body has unsubstituted \$VARIABLES outside a code block"
fi
TEMPLATE_VARS=$(grep -nE '\$\{?(VERSION|TAG|REPO|SUMMARY|PREV_TAG|PUB_ID|RUN_ID|PLAYGROUND_HOST|OLD_VERSION)\}?' \
                     "$WORK/body.md" | head -5)
if [ -n "$TEMPLATE_VARS" ]; then
  echo "$TEMPLATE_VARS" | sed 's/^/  UNSUBSTITUTED release-pipeline marker: /'
  bad "body has an unsubstituted release-pipeline variable"
fi
# Empty-href links: '[text]()' renders as a clickable link that goes nowhere.
# The release-drafter template emitted these for every download for years.
# NOTE: keep this pattern on ONE line. Written across two lines the newline makes
# grep read it as two alternatives, '](' or ')', and ')' matches ordinary prose --
# a gate that fails on every healthy release, which is how this was found.
grep -q ']()' "$WORK/body.md" && bad "body has empty-href links: $(grep -c ']()' "$WORK/body.md") found"

# -------------------------------------------------------- 6. playground
echo
echo "[6] playground"
if [ "$DO_PLAYGROUND" -eq 1 ]; then
  HOST="${PLAYGROUND_HOST:-playground.objeck.org}"
  ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=15 "root@$HOST" \
      "bash /opt/playground/repo/programs/web-playground/deploy/update.sh $VERSION" 2>&1 \
      | tail -8 | sed 's/^/  /'
fi
PV=$(curl -fsS --max-time 20 https://playground.objeck.org/api/health 2>/dev/null | sed 's/.*"version":"\([^"]*\)".*/\1/')
if [ "$PV" = "$TAG" ]; then note "health ok, version=$PV"
else echo "  playground reports '$PV', expected '$TAG'"; [ "$DO_PLAYGROUND" -eq 1 ] && FAIL=1; fi

# The health version is a hand-maintained constant in backend/app/config.py -- a
# LABEL. It read v2026.8.4 while the sandbox was still executing a 2026.6.1
# toolchain built months earlier, because the image is assembled from
# core/release/deploy, which is gitignored and which nothing refreshed. Every
# check passed; none of them ran any code. So run some.
EV=$(curl -fsS --max-time 60 -X POST https://playground.objeck.org/api/run \
      -H 'Content-Type: application/json' \
      -d '{"code":"class V { function : Main(args : String[]) ~ Nil { System.Runtime->GetVersion()->PrintLine(); } }"}' \
      2>/dev/null)
if echo "$EV" | grep -q "$VERSION"; then
  note "engine executes $VERSION"
else
  echo "  playground ENGINE is not $VERSION (the header can be right while this is wrong)"
  echo "  response: $(echo "$EV" | head -c 200)"
  [ "$DO_PLAYGROUND" -eq 1 ] && FAIL=1
fi

echo
echo "=============================================================="
[ "$FAIL" -eq 0 ] && echo " ALL POST-RELEASE GATES PASS for $TAG" || echo " *** POST-RELEASE GATES FAILED for $TAG ***"
echo "=============================================================="
exit "$FAIL"
