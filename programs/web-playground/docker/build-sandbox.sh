#!/bin/bash
# Build the sandbox Docker image for the Objeck Web Playground
# Usage: ./build-sandbox.sh [path-to-objeck-deploy]
#
# The deploy directory defaults to ../../core/release/deploy

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEPLOY_DIR="${1:-${SCRIPT_DIR}/../../core/release/deploy}"

if [ ! -d "$DEPLOY_DIR/bin" ] || [ ! -d "$DEPLOY_DIR/lib" ]; then
    echo "ERROR: Deploy directory not found at: $DEPLOY_DIR"
    echo "Usage: $0 [path-to-objeck-deploy]"
    exit 1
fi

echo "=== Preparing sandbox-deploy from: $DEPLOY_DIR ==="

SANDBOX_DEPLOY="${SCRIPT_DIR}/sandbox-deploy"
rm -rf "$SANDBOX_DEPLOY"
mkdir -p "$SANDBOX_DEPLOY/bin" "$SANDBOX_DEPLOY/lib"

# Copy binaries
echo "Copying obc and obr..."
cp "$DEPLOY_DIR/bin/obc" "$SANDBOX_DEPLOY/bin/"
cp "$DEPLOY_DIR/bin/obr" "$SANDBOX_DEPLOY/bin/"

# Copy only allowed libraries (no network, no SDL, no ODBC, no AI APIs)
ALLOWED_LIBS="lang gen_collect json xml regex cipher csv query ml nlp misc diags"
echo "Copying allowed libraries: $ALLOWED_LIBS"
for lib in $ALLOWED_LIBS; do
    if [ -f "$DEPLOY_DIR/lib/${lib}.obl" ]; then
        cp "$DEPLOY_DIR/lib/${lib}.obl" "$SANDBOX_DEPLOY/lib/"
        echo "  + ${lib}.obl"
    else
        echo "  ! WARNING: ${lib}.obl not found"
    fi
done

# Copy SSL certificates (needed for cipher lib)
if [ -f "$DEPLOY_DIR/lib/cacert.pem" ]; then
    cp "$DEPLOY_DIR/lib/cacert.pem" "$SANDBOX_DEPLOY/lib/"
fi

echo ""
echo "=== Building Docker image ==="
docker build -f "$SCRIPT_DIR/Dockerfile.sandbox" -t objeck-sandbox:latest "$SCRIPT_DIR"

echo ""
echo "=== Testing sandbox ==="
echo 'class T { function : Main(args : String[]) ~ Nil { "Sandbox OK"->PrintLine(); } }' > /tmp/sandbox-test.obs

docker run --rm \
    --network=none \
    --read-only \
    --tmpfs /tmp:rw,nosuid,size=10m \
    --memory=64m \
    --cpus=0.5 \
    --pids-limit=32 \
    --cap-drop=ALL \
    --security-opt=no-new-privileges \
    -v /tmp/sandbox-test.obs:/input/program.obs:ro \
    objeck-sandbox:latest \
    /bin/sh -c 'cp /input/program.obs /tmp/program.obs && obc -src /tmp/program.obs -dest /tmp/program.obe 2>&1 && obr /tmp/program.obe 2>&1'

rm -f /tmp/sandbox-test.obs

echo ""
echo "=== Build complete ==="
echo "Image: objeck-sandbox:latest"
echo "Sandbox deploy size: $(du -sh "$SANDBOX_DEPLOY" | cut -f1)"
