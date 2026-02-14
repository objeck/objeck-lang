#!/bin/bash
# ============================================
# Clean up failed/cancelled GitHub Actions runs
# ============================================
# Usage: ./cleanup_runs.sh [--all]
#   No args: deletes failed and cancelled runs
#   --all:   deletes all runs except the latest successful Release Build
#
# Prerequisites:
#   - GitHub CLI (gh) authenticated
# ============================================

set -e

REPO="objeck/objeck-lang"

if [ "$1" = "--all" ]; then
    echo "Deleting ALL workflow runs (except latest successful Release Build)..."

    # Get the latest successful Release Build run ID to keep
    KEEP_ID=$(gh run list --repo "$REPO" --workflow="release-build.yml" --status=success --limit 1 --json databaseId --jq '.[0].databaseId' 2>/dev/null || echo "")

    gh api "repos/$REPO/actions/runs" --paginate --jq '.workflow_runs[].id' | while read -r id; do
        if [ "$id" != "$KEEP_ID" ]; then
            gh api -X DELETE "repos/$REPO/actions/runs/$id" 2>/dev/null && echo "Deleted $id" || true
        else
            echo "Keeping $id (latest successful Release Build)"
        fi
    done
else
    echo "Deleting failed and cancelled workflow runs..."

    gh api "repos/$REPO/actions/runs" --paginate \
        --jq '.workflow_runs[] | select(.conclusion == "cancelled" or .conclusion == "failure") | .id' | \
    while read -r id; do
        gh api -X DELETE "repos/$REPO/actions/runs/$id" 2>/dev/null && echo "Deleted $id" || true
    done
fi

echo ""
echo "Cleanup complete!"
echo ""
echo "Remaining runs:"
gh run list --repo "$REPO" --limit 10
