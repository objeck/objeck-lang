#!/bin/bash

cd "$(dirname "$0")"

# format_code -> server -> lsp -> tools -> repo root
OBJECK_ROOT="${OBJECK_ROOT:-../../../..}"

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

rm -f formatter_regression.obe

echo "Building formatter regression test..."
"$OBC" -src regression_test.obs,formatter.obs,scanner.obs -lib gen_collect -dest formatter_regression
if [ $? -ne 0 ]; then
	echo "Build failed"
	exit 1
fi

echo "Running formatter regression test..."
echo "---"
"$OBR" formatter_regression
