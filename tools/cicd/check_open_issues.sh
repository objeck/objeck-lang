#!/usr/bin/env bash
#
# Release gate: every open issue is either fixed in this release, or carries a
# written reason it is not.
#
# Releases were cut without ever looking at the issue list. Nothing forced the
# question, so an issue could sit open across several releases having never been
# considered -- indistinguishable, from the outside, from one deliberately
# deferred. The difference matters: "we decided this can wait, because X" is a
# decision, and "nobody looked" is not.
#
# This does not require issues to be FIXED before a release. It requires the
# deferral to be a recorded decision. Fix it, or say why not, in a place that
# outlives the conversation where it was decided.
#
# To defer an issue, comment on it (the first line is parsed as the reason):
#
#   gh issue comment <N> --body "Deferred from v2026.9.0: needs a reproducer on
#   real hardware; the loopback-only shape is not what users hit."
#
# Usage: check_open_issues.sh <version>        e.g. 2026.9.0
# Exit 0 = every open issue is accounted for. Exit 1 = at least one is not.

set -uo pipefail

VERSION="${1:-}"
[ -n "$VERSION" ] || { echo "usage: check_open_issues.sh <version>"; exit 1; }
REPO="${REPO:-objeck/objeck-lang}"
MARKER="Deferred from v$VERSION"

echo "=============================================================="
echo " open-issue triage for v$VERSION"
echo "=============================================================="

# Test hooks. A gate whose failing path has never run is not a gate -- with an
# empty issue list every path below "nothing to triage" is dead code, and this
# would pass identically if the parsing were broken. These exercise both paths
# without opening throwaway issues on the real repo.
#   ISSUES_FILE=<file>  tab-separated "<number>	<title>" lines, one per issue
#   REASONS_DIR=<dir>   <dir>/<number> holds that issue's comment body
if [ -n "${ISSUES_FILE:-}" ]; then
  ISSUES=$(cat "$ISSUES_FILE")
else
  ISSUES=$(gh issue list --repo "$REPO" --state open --limit 200 \
             --json number,title --jq '.[] | (.number|tostring) + "\t" + .title' 2>/dev/null)
  if [ $? -ne 0 ]; then
    echo "  cannot read the issue list for $REPO (auth/permissions?) -- aborting"
    exit 1
  fi
fi

if [ -z "$ISSUES" ]; then
  echo
  echo " no open issues -- nothing to triage"
  exit 0
fi

FAIL=0
DEFERRED=0

while IFS=$'\t' read -r num title; do
  [ -n "$num" ] || continue
  # Look for a deferral note naming THIS version. A note from an older release
  # does not carry forward: each release re-asks the question, which is the
  # whole point -- otherwise one deferral silences an issue permanently.
  if [ -n "${REASONS_DIR:-}" ]; then
    REASON=""
    if [ -f "$REASONS_DIR/$num" ] && grep -q "^$MARKER" "$REASONS_DIR/$num"; then
      REASON=$(cat "$REASONS_DIR/$num")
    fi
  else
    REASON=$(gh issue view "$num" --repo "$REPO" --json comments \
               --jq '[.comments[].body | select(startswith("'"$MARKER"'"))] | last // ""' 2>/dev/null)
  fi
  if [ -n "$REASON" ]; then
    # first line after the marker, trimmed
    TEXT=$(printf '%s' "$REASON" | sed "s/^$MARKER:*[[:space:]]*//" | head -3 | tr '\n' ' ')
    echo
    echo "  DEFERRED  #$num  $title"
    echo "            reason: $TEXT"
    DEFERRED=$((DEFERRED + 1))
  else
    echo
    echo "  UNTRIAGED #$num  $title"
    echo "            fix it before tagging, or record why it waits:"
    echo "            gh issue comment $num --body \"$MARKER: <reason>\""
    FAIL=$((FAIL + 1))
  fi
done <<< "$ISSUES"

echo
echo "=============================================================="
if [ "$FAIL" -eq 0 ]; then
  echo " all open issues accounted for ($DEFERRED deferred with a reason)"
  echo "=============================================================="
  exit 0
fi
echo " *** $FAIL open issue(s) neither fixed nor explained ***"
echo " Deferring is fine. Deferring silently is not."
echo "=============================================================="
exit 1
