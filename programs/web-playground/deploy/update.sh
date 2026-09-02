#!/bin/bash
# Objeck Playground - Zero-downtime update script
# Usage: sudo bash update.sh [VERSION]
#
# VERSION defaults to whatever core/shared/version.h says after the pull. Pass it
# explicitly to pin a different release (e.g. to roll back).

set -euo pipefail

REPO_DIR="/opt/playground/repo"
DEPLOY_DIR="$REPO_DIR/core/release/deploy"
STAMP_FILE="$DEPLOY_DIR/.installed-version"
WANT_VERSION="${1:-}"

echo "=== Updating Objeck Playground ==="

# ---------------------------------------------------------------- pull
#
# The plain pull is tried first. It fails on this host in a specific, recurring
# way: files land root-owned and read-only (obu.cpp, the updater makefiles,
# downloads.csv), so git cannot unlink them and the pull dies half-applied --
#
#   error: unable to unlink old 'core/utils/updater/obu.cpp': Permission denied
#
# Every subsequent pull then fails the same way, so the server silently stops
# tracking master. That is how the playground came to be months behind while
# reporting a current version. Recover and retry rather than abort.
echo ""
echo "--- Pulling latest code ---"
cd "$REPO_DIR"
if ! sudo -u playground git pull origin master; then
    echo "--- pull failed; recovering the working tree and retrying ---"
    chmod -R u+w "$REPO_DIR"
    sudo -u playground git stash
    sudo -u playground git clean -fd
    sudo -u playground git pull origin master
    # A stale hand-edited config.py conflicts here. Take the committed value:
    # a botched resolution leaves conflict markers and uvicorn dies on SyntaxError.
    sudo -u playground git stash pop || {
        echo "--- stash pop conflicted; taking the committed files ---"
        sudo -u playground git checkout --theirs . 2>/dev/null || true
        sudo -u playground git checkout HEAD -- .
        sudo -u playground git stash drop || true
    }
fi

# ------------------------------------------------------- objeck toolchain
#
# THE SANDBOX RUNS WHATEVER IS IN core/release/deploy, AND NOTHING USED TO PUT
# ANYTHING THERE. That directory is gitignored, so `git pull` cannot refresh it,
# and no step here ever rebuilt it -- build-sandbox.sh only *copies* out of it.
# The result: the image was rebuilt faithfully on every deploy, from a tree last
# built by hand months earlier. The playground served a 2026.6.1 engine while its
# header read v2026.8.4, and every check passed because they all measured the
# label rather than the binary.
#
# Install the published release tarball instead of building here: this box has
# one core and 1 GB of RAM, an LTO link would likely OOM, and a failed build on
# the live host is worse than a stale one. The tarball is also the exact artifact
# users get, so the playground cannot diverge from the release.
if [ -z "$WANT_VERSION" ]; then
    WANT_VERSION=$(sed -n 's/.*VERSION_STRING L"\([0-9.]*\)".*/\1/p' "$REPO_DIR/core/shared/version.h")
fi
[ -n "$WANT_VERSION" ] || { echo "ERROR: could not determine target version"; exit 1; }

HAVE_VERSION=""
[ -f "$STAMP_FILE" ] && HAVE_VERSION=$(cat "$STAMP_FILE")

echo ""
echo "--- Objeck toolchain (want $WANT_VERSION, have ${HAVE_VERSION:-none}) ---"
if [ "$HAVE_VERSION" = "$WANT_VERSION" ] && [ -x "$DEPLOY_DIR/bin/obr" ]; then
    echo "Toolchain already at $WANT_VERSION - skipping download"
else
    TGZ="objeck-linux-x64_${WANT_VERSION}.tgz"
    BASE="https://github.com/objeck/objeck-lang/releases/download/v${WANT_VERSION}"
    STAGE=$(mktemp -d)
    trap 'rm -rf "$STAGE"' EXIT

    echo "Downloading $TGZ ..."
    curl -sSLf -o "$STAGE/$TGZ" "$BASE/$TGZ"
    curl -sSLf -o "$STAGE/SHA256SUMS" "$BASE/SHA256SUMS"

    # Verify before installing. The manifest is regenerated after Windows signing,
    # so it describes the published bytes.
    EXPECT=$(grep " $TGZ\$" "$STAGE/SHA256SUMS" | cut -d' ' -f1)
    ACTUAL=$(sha256sum "$STAGE/$TGZ" | cut -d' ' -f1)
    [ -n "$EXPECT" ] || { echo "ERROR: $TGZ not listed in SHA256SUMS"; exit 1; }
    if [ "$EXPECT" != "$ACTUAL" ]; then
        echo "ERROR: SHA256 mismatch for $TGZ"
        echo "  expected $EXPECT"
        echo "  actual   $ACTUAL"
        exit 1
    fi
    echo "SHA256 verified"

    tar xzf "$STAGE/$TGZ" -C "$STAGE"
    [ -x "$STAGE/objeck-lang/bin/obr" ] || { echo "ERROR: tarball has no bin/obr"; exit 1; }

    # Swap, keeping the previous tree until the new one is proven below.
    if [ -d "$DEPLOY_DIR" ]; then
        rm -rf "$DEPLOY_DIR.prev"
        mv "$DEPLOY_DIR" "$DEPLOY_DIR.prev"
    fi
    mv "$STAGE/objeck-lang" "$DEPLOY_DIR"
    chmod -R a+rX "$DEPLOY_DIR"
    echo "$WANT_VERSION" > "$STAMP_FILE"
    echo "Installed Objeck $WANT_VERSION"
fi

# --------------------------------------------------------- python deps
echo ""
echo "--- Updating dependencies ---"
/opt/playground/venv/bin/pip install -q -r /opt/playground/backend/requirements.txt

# ------------------------------------------------------- sandbox image
echo ""
echo "--- Rebuilding sandbox image ---"
sudo -u playground bash /opt/playground/docker/build-sandbox.sh "$DEPLOY_DIR"

# ------------------------------------------------------------- restart
echo ""
echo "--- Restarting API ---"
systemctl restart playground

echo ""
echo "--- Cleaning up ---"
docker image prune -f
docker container prune -f

# -------------------------------------------------------- verification
#
# Two checks, because the first one alone is what let this rot for months.
# /api/health reports a hand-maintained constant in backend/app/config.py; it is
# a label, and it was correct while the engine underneath was three months old.
# Only running code through the sandbox says what actually executes.
echo ""
echo "--- Health check ---"
sleep 3
if curl -sf http://localhost:8000/api/health > /dev/null; then
    echo "API is healthy"
else
    echo "ERROR: API health check failed!"
    systemctl status playground --no-pager
    exit 1
fi

echo ""
echo "--- Engine check (runs code through the sandbox) ---"
PROBE='{"code":"class V { function : Main(args : String[]) ~ Nil { System.Runtime->GetVersion()->PrintLine(); } }"}'
ENGINE_OUT=$(curl -sf --max-time 60 -X POST http://localhost:8000/api/run \
    -H 'Content-Type: application/json' -d "$PROBE" || true)

if echo "$ENGINE_OUT" | grep -q "$WANT_VERSION"; then
    echo "Engine reports $WANT_VERSION"
else
    echo "ERROR: sandbox is NOT running $WANT_VERSION"
    echo "  response: $ENGINE_OUT"
    echo "  the previous toolchain is at $DEPLOY_DIR.prev if a rollback is needed"
    exit 1
fi

# Only now is the previous tree redundant.
rm -rf "$DEPLOY_DIR.prev"

echo ""
echo "=== Update complete (Objeck $WANT_VERSION) ==="
