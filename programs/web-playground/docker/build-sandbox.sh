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
mkdir -p "$SANDBOX_DEPLOY/bin" "$SANDBOX_DEPLOY/lib/native"

# Copy binaries
echo "Copying obc and obr..."
cp "$DEPLOY_DIR/bin/obc" "$SANDBOX_DEPLOY/bin/"
cp "$DEPLOY_DIR/bin/obr" "$SANDBOX_DEPLOY/bin/"

# The libraries a playground program can use, plus the ones those were built
# against. KEEP IN STEP WITH backend/app/config.py:
#     allowed_libs + dependency_libs + lang + gen_collect
# backend/tests/test_policy.py compares the two and fails when they differ.
#
# The server works out which of these a program needs from its `use` lines and
# passes obc the whole dependency closure, so every one of them must be here.
# `net` is only here because query and misc are built against it: the container
# has no network and the validator refuses Web.HTTP.
#
# Nothing else is copied: no SDL, ODBC, OpenCV, ONNX or hosted-AI libraries.
ALLOWED_LIBS="lang gen_collect json xml regex cipher csv query query_ml ml nlp ai math misc diags concurrent net"

# Native modules those libraries load at run time (DllProxy looks in lib/native).
# cipher -> crypto, ml -> ml, diags -> diags. Without them a program that hashes,
# trains a model or analyses code fails at run time with "Unable to load shared
# library", after compiling cleanly.
NATIVE_LIBS="crypto ml diags"

MISSING_LIBS=""
echo "Copying allowed libraries: $ALLOWED_LIBS"
for lib in $ALLOWED_LIBS; do
    if [ -f "$DEPLOY_DIR/lib/${lib}.obl" ]; then
        cp "$DEPLOY_DIR/lib/${lib}.obl" "$SANDBOX_DEPLOY/lib/"
        echo "  + ${lib}.obl"
    else
        # Fatal, not a warning. set -e cannot catch an echo, so a dropped library
        # used to build a good-looking image: the engine probe in update.sh uses
        # no -lib and passes, the deploy reports success, and users selecting that
        # library get an obscure compile error while the UI still offers it. That
        # is the same "the check measures the wrong thing" shape as the stale
        # engine incident, one layer down.
        echo "  ! ERROR: ${lib}.obl not found in $DEPLOY_DIR/lib" >&2
        MISSING_LIBS="$MISSING_LIBS $lib"
    fi
done

echo "Copying native modules: $NATIVE_LIBS"
for native in $NATIVE_LIBS; do
    so="libobjk_${native}.so"
    if [ -f "$DEPLOY_DIR/lib/native/$so" ]; then
        cp "$DEPLOY_DIR/lib/native/$so" "$SANDBOX_DEPLOY/lib/native/"
        echo "  + native/$so"
    else
        echo "  ! ERROR: native/$so not found in $DEPLOY_DIR/lib" >&2
        MISSING_LIBS="$MISSING_LIBS native/$so"
    fi
done

if [ -n "$MISSING_LIBS" ]; then
    echo "" >&2
    echo "ERROR: the deploy tree is missing:$MISSING_LIBS" >&2
    echo "Refusing to build a sandbox image that advertises libraries it lacks." >&2
    exit 1
fi

# The configobjk.ini alias file is not needed (the server passes plain names),
# but SSL certificates are (cipher/TLS)
if [ -f "$DEPLOY_DIR/lib/cacert.pem" ]; then
    cp "$DEPLOY_DIR/lib/cacert.pem" "$SANDBOX_DEPLOY/lib/"
fi

echo ""
echo "=== Building Docker image ==="
docker build -f "$SCRIPT_DIR/Dockerfile.sandbox" -t objeck-sandbox:latest "$SCRIPT_DIR"

echo ""
echo "=== Testing sandbox ==="

# Three probes, each run under the same restrictions as production:
#   1. nothing linked: the engine runs at all
#   2. cipher: the native crypto module loads
#   3. Query.Structured: the full dependency closure links (query, net, regex,
#      csv, xml, json, misc, cipher) and fits the memory limit
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT

cat > "$TEST_DIR/hello.obs" <<'EOF'
class T { function : Main(args : String[]) ~ Nil { "Sandbox OK"->PrintLine(); } }
EOF
cat > "$TEST_DIR/cipher.obs" <<'EOF'
use Cipher;
class T { function : Main(args : String[]) ~ Nil { Hash->SHA256("abc"->ToByteArray())->ToHexString()->PrintLine(); } }
EOF
cat > "$TEST_DIR/query.obs" <<'EOF'
use Query.Structured;
class T {
  function : Main(args : String[]) ~ Nil {
    t := Table->New("t", ["n"]);
    t->Insert()->Set("n", IntRef->New(41));
    c := Catalog->New();
    c->Add(t);
    r := c->Query("select n as answer from t"); if(r = Nil) { c->GetError()->PrintLine(); } else { r->ToCsv()->PrintLine(); };
  }
}
EOF

probe() {  # name, libs
    local name="$1" libs="$2" flag=""
    [ -n "$libs" ] && flag="-lib $libs"
    echo "--- $name"
    docker run --rm \
        --network=none \
        --read-only \
        --tmpfs /tmp:rw,nosuid,nodev,noexec,size=10m \
        --memory=128m \
        --cpus=0.5 \
        --pids-limit=32 \
        --cap-drop=ALL \
        --security-opt=no-new-privileges \
        --user sandbox \
        -v "$TEST_DIR/$name.obs":/input/program.obs:ro \
        objeck-sandbox:latest \
        /bin/sh -c "cp /input/program.obs /tmp/program.obs && obc -src /tmp/program.obs $flag -dest /tmp/program.obe >/dev/null 2>&1 && obr /tmp/program.obe 2>&1"
}

probe hello ""
probe cipher "cipher"
probe query "query,net,regex,csv,xml,json,cipher,misc"

echo ""
echo "=== Build complete ==="
echo "Image: objeck-sandbox:latest"
echo "Sandbox deploy size: $(du -sh "$SANDBOX_DEPLOY" | cut -f1)"
