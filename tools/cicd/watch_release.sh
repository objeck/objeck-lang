#!/usr/bin/env bash
#
# watch_release.sh <VERSION> [--build-id N]
#
# Watch release-build.yml through release-publish.yml for a tag, and report what
# is actually true.
#
# WHY THIS IS A SCRIPT AND NOT A PARAGRAPH
# ----------------------------------------
# During the v2026.8.3 release an ad-hoc watcher reported "RELEASE-BUILD FAILED"
# for a run that was perfectly healthy: it had simply exhausted a 65-minute poll
# budget while the windows-arm64 leg took its usual ~70 minutes (vcpkg rebuilds
# opencv4:arm64-windows from source on a cache miss). The loop ended, the last
# observed state was still "in_progress", and the script treated "I stopped
# looking" as "it failed".
#
# Conflating those two is not a cosmetic bug. A false red on a release invites
# exactly the panic response the release process forbids -- deleting a pushed
# tag, or patching CI mid-release, which is what broke v2026.5.0 and v2026.5.1.
#
# So: TIMEOUT and FAILURE exit differently and print differently.
#     exit 0  = finished green
#     exit 1  = genuinely failed (a conclusion other than success)
#     exit 2  = still running when the budget ran out -- NOT a failure
#
# Nothing here mutates anything. It only observes.
set -uo pipefail

VERSION="${1:-}"
[ -n "$VERSION" ] || { echo "usage: watch_release.sh <VERSION> [--build-id N]"; exit 3; }
shift
TAG="v${VERSION}"
REPO="objeck/objeck-lang"
BUILD_ID=""
while [ $# -gt 0 ]; do
  case "$1" in
    --build-id) shift; BUILD_ID="${1:-}" ;;
    *) echo "unknown option: $1"; exit 3 ;;
  esac
  shift
done

if [ -z "$BUILD_ID" ]; then
  for _ in 1 2 3 4 5 6; do
    BUILD_ID=$(gh run list --workflow=release-build.yml -R "$REPO" --branch="$TAG" \
                 --limit=1 --json databaseId --jq '.[0].databaseId' 2>/dev/null)
    [ -n "$BUILD_ID" ] && break
    sleep 5
  done
fi
[ -n "$BUILD_ID" ] || { echo "no release-build.yml run found for $TAG"; exit 3; }

ST=""; TIMED_OUT=0
wait_run() {
  local id="$1" label="$2" max="$3" i
  ST=""; TIMED_OUT=0
  for i in $(seq 1 "$max"); do
    ST=$(gh run view "$id" -R "$REPO" --json status,conclusion \
           --jq '.status + "/" + (.conclusion // "-")' 2>/dev/null)
    case "$ST" in
      completed/*) echo "$(date +%H:%M:%S) [$label] $ST"; return 0 ;;
      # An empty result is a FAILED QUERY, not a finished run. `gh run watch`
      # exits 0 on a network error, which is why this polls status explicitly.
      "") echo "$(date +%H:%M:%S) [$label] (gh query failed, retrying)" ;;
      *)  if [ $((i % 10)) -eq 1 ]; then
            pending=$(gh run view "$id" -R "$REPO" --json jobs \
                       --jq '[.jobs[]|select(.status!="completed")|.name]|join(", ")' 2>/dev/null)
            echo "$(date +%H:%M:%S) [$label] $ST  waiting on: ${pending:-?}"
          fi ;;
    esac
    sleep 30
  done
  TIMED_OUT=1
  return 0
}

echo "=== release-build.yml run $BUILD_ID ($TAG) ==="
wait_run "$BUILD_ID" build 240        # 2h; the arm64 leg alone is ~70 min
if [ "$TIMED_OUT" -eq 1 ]; then
  echo "TIMEOUT: last observed '$ST' after the poll budget."
  echo "  This is NOT a failure -- the run may still be healthy. Re-check with:"
  echo "  gh run view $BUILD_ID -R $REPO"
  exit 2
fi
if [ "$ST" != "completed/success" ]; then
  echo "RELEASE-BUILD FAILED: $ST"
  gh run view "$BUILD_ID" -R "$REPO" --json jobs --jq '.jobs[]|"  " + .name + "  " + (.conclusion // "-")'
  echo "  The tag is already pushed. Fix on master, delete the tag, re-tag."
  echo "  Do NOT patch CI mid-release -- that broke v2026.5.0 and v2026.5.1."
  exit 1
fi
echo "RELEASE-BUILD GREEN"

# Confirm the per-platform binary gates RAN. Skipped is not failed: both steps are
# guarded by `if: runner.os ...`, so each platform job runs one and skips the other.
# Match the matrix jobs only -- "Build LSP Package"/"Build Summary" verify nothing
# by design, and a gate that is noisy on a healthy run gets ignored.
echo "--- per-platform binary verification ---"
gh run view "$BUILD_ID" -R "$REPO" --json jobs --jq '.jobs[]
  | select(.name|test("^Build (windows|linux|macos)-"))
  | .name as $j
  | [.steps[] | select(.name|startswith("Verify required binaries")) | .conclusion]
  | "  " + $j + " -> " + (map(select(. == "success")) | length | tostring) + " success"'
BAD=$(gh run view "$BUILD_ID" -R "$REPO" --json jobs --jq '[.jobs[]
  | select(.name|test("^Build (windows|linux|macos)-"))
  | select([.steps[] | select(.name|startswith("Verify required binaries")) | .conclusion]
           | map(select(. == "success")) | length != 1)] | length')
[ "$BAD" = "0" ] || { echo "  *** $BAD platform job(s) never ran a binary verification ***"; exit 1; }

echo "=== release-publish.yml ==="
PUB_ID=""
for _ in $(seq 1 30); do
  PUB_ID=$(gh run list --workflow=release-publish.yml -R "$REPO" --limit=1 \
             --json databaseId --jq '.[0].databaseId' 2>/dev/null)
  [ -n "$PUB_ID" ] && break
  sleep 15
done
[ -n "$PUB_ID" ] || { echo "no release-publish run appeared"; exit 1; }
echo "publish run: $PUB_ID"

wait_run "$PUB_ID" publish 90
if [ "$TIMED_OUT" -eq 1 ]; then echo "TIMEOUT waiting on publish (last '$ST') -- not a failure"; exit 2; fi
if [ "$ST" != "completed/success" ]; then
  echo "RELEASE-PUBLISH FAILED: $ST"
  gh run view "$PUB_ID" -R "$REPO" --json jobs --jq '.jobs[]|"  " + .name + "  " + (.conclusion // "-")'
  exit 1
fi
echo "RELEASE-PUBLISH GREEN"
gh run view "$PUB_ID" -R "$REPO" --json jobs --jq '.jobs[]|"  " + .name + "  " + (.conclusion // "-")'

echo
echo "Next: tools/cicd/post_release.sh $VERSION --sign --body <file> --playground"
exit 0
