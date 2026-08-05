#!/bin/bash

cd "$(dirname "$0")"

# server -> lsp -> tools -> repo root
OBJECK_ROOT="${OBJECK_ROOT:-../../..}"

# The release tree is named deploy-x64/deploy-arm64 on some platforms and plain
# deploy on others, so probe instead of hardcoding one of them.
DEPLOY_DIR=""
for candidate in deploy-x64 deploy-arm64 deploy; do
	if [ -d "$OBJECK_ROOT/core/release/$candidate/bin" ]; then
		DEPLOY_DIR="$(cd "$OBJECK_ROOT/core/release/$candidate" && pwd)"
		break
	fi
done

if [ -z "$DEPLOY_DIR" ]; then
	echo "Build failed: no deploy tree under $OBJECK_ROOT/core/release"
	echo "  (looked for deploy-x64, deploy-arm64, deploy)"
	exit 1
fi

# Call the toolchain by absolute path. Relying on PATH picks up a system-wide
# Objeck install ahead of the build tree, which then fails against these
# libraries with a tool chain version mismatch.
OBC="$DEPLOY_DIR/bin/obc"
OBR="$DEPLOY_DIR/bin/obr"
export OBJECK_LIB_PATH="$DEPLOY_DIR/lib"
export PATH="$DEPLOY_DIR/lib/native:$PATH"

rm -f *.obe
rm -f /tmp/objk-*

echo ---

"$OBC" -src $OBJECK_ROOT/core/compiler/lib_src/diags.obs -lib gen_collect -tar lib -opt s3 -dest $OBJECK_ROOT/core/lib/diags.obl
if [ $? -ne 0 ]; then
	echo "Build failed: diags.obl"
	exit 1
fi
cp $OBJECK_ROOT/core/lib/diags.obl "$DEPLOY_DIR/lib/diags.obl"

echo ---

"$OBC" -src frameworks.obs,proxy.obs,server.obs,format_code/scanner.obs,format_code/formatter.obs -lib diags,net,json,regex,cipher -dest objeck_lsp.obe
if [ $? -ne 0 ]; then
	echo "Build failed: objeck_lsp.obe"
	exit 1
fi
cp objeck_lsp.obe ../clients/vscode/server

echo ---
echo "Build successful"

if [ "$1" = "brun" ]; then
	echo Running...
	"$OBR" objeck_lsp.obe objk_apis.json pipe debug
fi
