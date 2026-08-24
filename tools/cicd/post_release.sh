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
            "SHA256SUMS"; do
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
( cd "$WORK" && gh release download "$TAG" -R "$REPO" --pattern "SHA256SUMS" --clobber >/dev/null 2>&1
  gh release download "$TAG" -R "$REPO" --pattern "*.msi" --clobber >/dev/null 2>&1
  gh release download "$TAG" -R "$REPO" --pattern "*.pkg" --clobber >/dev/null 2>&1
  gh release download "$TAG" -R "$REPO" --pattern "*.zip" --clobber >/dev/null 2>&1 )
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

# ------------------------------------------------------------- 5. body
echo
echo "[5] release notes name only files that exist"
if [ -n "$BODY_FILE" ]; then
  gh release edit "$TAG" -R "$REPO" --notes-file "$BODY_FILE" >/dev/null 2>&1 \
    || bad "could not set release body"
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
grep -qE '\$[A-Z_]+' "$WORK/body.md" && bad "body has unsubstituted \$VARIABLES"
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
      'bash /opt/playground/repo/programs/web-playground/deploy/update.sh' 2>&1 | tail -5 | sed 's/^/  /'
fi
PV=$(curl -fsS --max-time 20 https://playground.objeck.org/api/health 2>/dev/null | sed 's/.*"version":"\([^"]*\)".*/\1/')
if [ "$PV" = "$TAG" ]; then note "health ok, version=$PV"
else echo "  playground reports '$PV', expected '$TAG'"; [ "$DO_PLAYGROUND" -eq 1 ] && FAIL=1; fi

echo
echo "=============================================================="
[ "$FAIL" -eq 0 ] && echo " ALL POST-RELEASE GATES PASS for $TAG" || echo " *** POST-RELEASE GATES FAILED for $TAG ***"
echo "=============================================================="
exit "$FAIL"
