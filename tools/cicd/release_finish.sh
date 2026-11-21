#!/usr/bin/env bash
#
# release_finish.sh <VERSION> [--body FILE] [--from N] [--timeout MIN] [--dry-run] [--no-sign]
#
# EVERYTHING after the tag push, in order, with no one at the keyboard (signing included when the saved
# token password exists: tools\cicd\token_pin.ps1). Run it on the Windows release machine, from Git Bash,
# right after `git push origin v<VERSION>`. It works from a throwaway checkout of origin/master, so the branch
# you happen to have checked out cannot matter (a stale checkout once made the site upload refuse).
#
#   1  wait      the tag's release build passes, the release is published and its packages are up
#   2  publish   tools/cicd/post_release.sh --sign --playground --site: sign the MSIs, deploy the playground
#                and the Objeck-served site's engine, run every asset/signature/manifest gate
#   3  markers   flip the README and objeck.org "current release" markers (flip_markers.py) in a PR, wait for
#                its checks, merge it (--match-head-commit, never with a failing or unfinished check)
#   4  site      upload the site pages and API docs to the droplet (core/release/deploy_website.sh)
#   5  verify    post_release.sh again, read only, now that the site names the release
#
# --from N       restart at step N (every step is safe to repeat)
# --timeout MIN  longest wait for a build or a PR's checks (default 120)
# --dry-run      read-only: report what each step would do; nothing is signed, deployed, merged or pushed
# --no-sign      leave signing out (you sign by hand)
# --body FILE    release notes for post_release.sh to put on the release
#
# Stops at the first problem and says which step to resume from. Nothing here prints the token password.
# Prerequisites: gh signed in, ssh `playground` alias, the objeckorg key (OBJECK_ORG_KEY, default
# ~/.ssh/google_compute_engine), python, and for unattended signing the saved password file.
set -uo pipefail

VERSION="${1:-}"
[ -n "$VERSION" ] || { sed -n '3,34p' "${BASH_SOURCE[0]}"; exit 2; }
shift
TAG="v$VERSION"
REPO="objeck/objeck-lang"
FROM=1; TIMEOUT_MIN=120; DRY=0; SIGN=1; BODY=""
while [ $# -gt 0 ]; do
  case "$1" in
    --from)    FROM="$2"; shift ;;
    --timeout) TIMEOUT_MIN="$2"; shift ;;
    --dry-run) DRY=1 ;;
    --no-sign) SIGN=0 ;;
    --body)    BODY="$2"; shift ;;
    *) echo "unknown option: $1"; exit 2 ;;
  esac
  shift
done

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAIN="$(cd "$HERE/../.." && pwd)"
say()  { echo; echo "== $*"; }
note() { echo "   $*"; }
die()  { echo; echo "STOPPED: $*"; echo "   Fix it, then resume with:  tools/cicd/release_finish.sh $VERSION --from ${STEP:-1}"; exit 1; }

command -v gh >/dev/null || die "gh (GitHub CLI) is required"
command -v python >/dev/null || die "python is required"

# ---- a clean checkout of master for everything that edits or deploys --------------------------------
git -C "$MAIN" fetch -q origin master "refs/tags/$TAG:refs/tags/$TAG" 2>/dev/null || git -C "$MAIN" fetch -q origin master --tags
WT="$(mktemp -d 2>/dev/null || echo "${TMPDIR:-/tmp}/release_finish.$$")"
rm -rf "$WT"
REF="${RELEASE_FINISH_REF:-origin/master}"      # testing hook: run the scripts from another ref
git -C "$MAIN" fetch -q origin "+refs/heads/*:refs/remotes/origin/*" 2>/dev/null
git -C "$MAIN" worktree add -q --detach "$WT" "$REF" || die "could not create a checkout of $REF"
cleanup() { git -C "$MAIN" worktree remove --force "$WT" >/dev/null 2>&1 || rm -rf "$WT"; }
trap cleanup EXIT
refresh() { git -C "$MAIN" fetch -q origin "+refs/heads/*:refs/remotes/origin/*" 2>/dev/null; git -C "$WT" checkout -q --detach "$REF"; }


# An obu (or minisign) is needed to check SHA256SUMS.minisig. A clean checkout has no built deploy tree, so look in
# the primary checkout's, or take OBJECK_OBU from the environment.
if [ -z "${OBJECK_OBU:-}" ]; then
  PRIMARY="$(git -C "$MAIN" worktree list --porcelain | sed -n 's/^worktree //p' | head -1)"
  for c in "$PRIMARY/core/release/deploy-x64/bin/obu.exe" "$PRIMARY/core/release/deploy/bin/obu"; do
    [ -f "$c" ] && { OBJECK_OBU="$c"; break; }
  done
fi
export OBJECK_OBU

git -C "$MAIN" rev-parse -q --verify "refs/tags/$TAG" >/dev/null || die "tag $TAG does not exist locally or on origin: push it first"
note "release $TAG, master $(git -C "$WT" rev-parse --short HEAD), $([ $DRY = 1 ] && echo DRY RUN || echo live)"

# poll helper: wait_for "description" <command that exits 0 when ready, 2 when it can never be ready>
wait_for() {
  local what="$1"; shift
  local end=$(( $(date +%s) + TIMEOUT_MIN * 60 )) rc
  while :; do
    "$@"; rc=$?
    [ $rc = 0 ] && return 0
    [ $rc = 2 ] && return 2
    [ "$(date +%s)" -ge "$end" ] && { note "timed out after $TIMEOUT_MIN min waiting for: $what"; return 1; }
    sleep 60
  done
}

# ============================================================================================ 1. wait
if [ "$FROM" -le 1 ]; then
  STEP=1; say "1/5 wait for the build, the publish and the packages"
  build_ok() {
    local r; r="$(gh run list --repo "$REPO" --workflow release-build.yml --branch "$TAG" --limit 1 --json status,conclusion --jq '.[0]|"\(.status) \(.conclusion)"' 2>/dev/null)"
    case "$r" in
      "completed success") return 0 ;;
      "completed "*) note "release build ended: $r"; return 2 ;;
      *) note "release build: ${r:-not started yet}"; return 1 ;;
    esac
  }
  wait_for "the $TAG release build" build_ok || die "the release build did not pass (see https://github.com/$REPO/actions)"
  published_with_packages() {
    local j; j="$(gh release view "$TAG" --repo "$REPO" --json isDraft,assets --jq '"\(.isDraft) \([.assets[].name|select(endswith(".deb"))]|length) \([.assets[].name|select(endswith(".msi"))]|length)"' 2>/dev/null)" || { note "release not visible yet"; return 1; }
    note "draft/debs/msis: $j"
    case "$j" in "false 2 2"*|"false "[2-9]" "[2-9]*) return 0 ;; esac
    return 1
  }
  wait_for "the published release with both .deb and both .msi packages" published_with_packages || die "the release is not complete"
  note "build passed; release published with its packages"
fi

# ============================================================================================ 2. publish
if [ "$FROM" -le 2 ]; then
  STEP=2; say "2/5 sign, deploy the playground and the site engine, run the release gates"
  if [ "$DRY" = 1 ]; then
    note "would run: post_release.sh $VERSION $([ $SIGN = 1 ] && echo --sign) --playground --site ${BODY:+--body $BODY}"
    if [ -f "$USERPROFILE/.objeck/token_pin.dpapi" ]; then note "signing mode: UNATTENDED (saved password file present)"; else note "signing mode: ATTENDED (no saved password file: the Token Logon window will ask)"; fi
    note "read-only gates now:"; ( cd "$WT" && bash tools/cicd/post_release.sh "$VERSION" ) 2>&1 | sed 's/^/     /' | tail -25
  else
    if [ $SIGN = 1 ]; then
      if [ -f "$USERPROFILE/.objeck/token_pin.dpapi" ]; then note "signing UNATTENDED with the saved password file"; else note "signing ATTENDED: the Token Logon window will come to the front; type the password there"; fi
    fi
    LOG="$WT/post_release.log"
    ARGS=(--playground --site); [ $SIGN = 1 ] && ARGS=(--sign "${ARGS[@]}"); [ -n "$BODY" ] && ARGS+=(--body "$BODY")
    ( cd "$WT" && bash tools/cicd/post_release.sh "$VERSION" "${ARGS[@]}" ) 2>&1 | tee "$LOG"
    # The site-page gates cannot pass yet: markers and pages are steps 3 and 4. Anything else failing is real.
    REAL="$(grep -E '^\s*FAIL:' "$LOG" | grep -v -i -E 'readme\.html|api/latest|download v|site page' || true)"
    [ -z "$REAL" ] || die "post_release.sh reported failures other than the not-yet-updated site pages:
$REAL"
    note "publish step done (any site-page gate failure above is expected until step 4)"
  fi
fi

# ============================================================================================ 3. markers
if [ "$FROM" -le 3 ]; then
  STEP=3; say "3/5 flip the README and objeck.org markers to $TAG"
  refresh
  if [ "$DRY" = 1 ]; then
    python "$WT/tools/cicd/flip_markers.py" "$VERSION" --root "$WT" 2>&1 | sed 's/^/   /' || true
    git -C "$WT" checkout -q -- . ; note "dry run: nothing committed"
  else
    OUT="$(python "$WT/tools/cicd/flip_markers.py" "$VERSION" --root "$WT" 2>&1)"; RC=$?
    echo "$OUT" | sed 's/^/   /'
    [ $RC = 0 ] || die "flip_markers.py refused (a marker moved, or the v$VERSION notes are not in docs/readme.html): fix that on master first"
    if echo "$OUT" | grep -q '^nothing to do'; then
      note "markers already name $TAG"
    else
      ( cd "$WT" && python tools/docs/guides_to_html.py >/dev/null 2>&1 )
      BR="docs/flip-markers-$VERSION"
      git -C "$WT" checkout -q -B "$BR"
      git -C "$WT" add -A README.md docs
      git -C "$WT" commit -q -m "Flip release markers to $TAG" || die "nothing to commit after the flip"
      git -C "$WT" push -q -u origin "$BR" || die "could not push $BR"
      PR="$(gh pr create --repo "$REPO" --base master --head "$BR" --title "Flip release markers to $TAG" --body "Moves the README and objeck.org \"current release\" markers to $TAG, now that it is published and signed. Generated by tools/cicd/flip_markers.py." | grep -o '[0-9]*$')"
      [ -n "$PR" ] || die "could not open the markers PR"
      note "opened PR #$PR; waiting for its checks"
      checks_done() {
        local s; s="$(gh pr view "$PR" --repo "$REPO" --json statusCheckRollup --jq '[.statusCheckRollup[]|select(.name|test("fuzz";"i")|not)] | "\(length) \([.[]|select(.status!="COMPLETED")]|length) \([.[]|select(.conclusion=="FAILURE" or .conclusion=="CANCELLED")]|length)"' 2>/dev/null)"
        set -- $s
        [ "${3:-0}" != 0 ] && { note "PR #$PR has failing checks"; return 2; }
        [ "${1:-0}" != 0 ] && [ "${2:-1}" = 0 ] && return 0
        note "PR #$PR checks: ${2:-?} still running"; return 1
      }
      wait_for "PR #$PR checks" checks_done || die "PR #$PR is not green: not merging it (https://github.com/$REPO/pull/$PR)"
      SHA="$(gh pr view "$PR" --repo "$REPO" --json headRefOid --jq .headRefOid)"
      gh pr merge "$PR" --repo "$REPO" --merge --match-head-commit "$SHA" --delete-branch >/dev/null 2>&1 || die "could not merge PR #$PR"
      note "merged PR #$PR ($SHA)"
    fi
  fi
fi

# ============================================================================================ 4. site
if [ "$FROM" -le 4 ]; then
  STEP=4; say "4/5 upload the site pages and API docs to the droplet"
  refresh
  KEY="${OBJECK_ORG_KEY:-$HOME/.ssh/google_compute_engine}"
  KEYARG=(); [ -f "$KEY" ] && KEYARG=(--key "$KEY")
  DRYARG=(); [ "$DRY" = 1 ] && DRYARG=(--dry-run)
  bash "$WT/core/release/deploy_website.sh" --source "$WT/docs/api.zip" --version "$VERSION" --host "${OBJECK_ORG_HOST:-playground.objeck.org}" "${KEYARG[@]}" "${DRYARG[@]}" || die "the site upload refused (its message above says why)"
fi

# ============================================================================================ 5. verify
if [ "$FROM" -le 5 ]; then
  STEP=5; say "5/5 final gates (read only)"
  ( cd "$WT" && bash tools/cicd/post_release.sh "$VERSION" ) || die "a gate failed: the release is NOT fully done"
fi

echo
echo "== release_finish: $TAG complete$([ $DRY = 1 ] && echo ' (dry run: nothing was changed)')"
