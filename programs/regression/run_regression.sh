#!/bin/bash
# Regression test runner for Objeck language
# Usage: ./run_regression.sh [x64|arm64]

PLATFORM=${1:-x64}

# Detect platform-specific deployment directory
if [ -d "../../core/release/deploy-${PLATFORM}" ]; then
    DEPLOY_DIR="../../core/release/deploy-${PLATFORM}"
elif [ -d "../../core/release/deploy" ]; then
    DEPLOY_DIR="../../core/release/deploy"
else
    echo "ERROR: Could not find deployment directory"
    echo "Expected: ../../core/release/deploy-${PLATFORM} or ../../core/release/deploy"
    exit 1
fi

COMPILER="${DEPLOY_DIR}/bin/obc"
VM="${DEPLOY_DIR}/bin/obr"
RESULTS_DIR="./results"

mkdir -p "$RESULTS_DIR"

PASS_COUNT=0
FAIL_COUNT=0

echo "========================================"
echo "  Objeck Regression Test Suite"
echo "  Platform: $PLATFORM"
echo "========================================"
echo ""

# Get absolute paths
REGRESSION_DIR=$(pwd)
ABS_COMPILER=$(cd "$(dirname "$COMPILER")" && pwd)/$(basename "$COMPILER")
ABS_VM=$(cd "$(dirname "$VM")" && pwd)/$(basename "$VM")

# Run each test
for test in *.obs; do
    [ -f "$test" ] || continue
    NAME="${test%.obs}"
    echo "Running: $NAME..."

    # Get absolute test path
    ABS_TEST="${REGRESSION_DIR}/${test}"

    # Change to compiler directory and compile
    cd "${DEPLOY_DIR}/bin"
    "$ABS_COMPILER" -src "$ABS_TEST" -lib cipher,collect,xml,json -opt s3 -dest "${REGRESSION_DIR}/${NAME}.obe" 2>&1 | tee "${REGRESSION_DIR}/${RESULTS_DIR}/${NAME}_compile.log" > /dev/null

    if [ ${PIPESTATUS[0]} -ne 0 ]; then
        cd "$REGRESSION_DIR"
        echo "  [FAIL] Compilation error"
        ((FAIL_COUNT++))
        continue
    fi

    # Run from regression directory
    cd "$REGRESSION_DIR"
    "$ABS_VM" "$NAME.obe" > "$RESULTS_DIR/${NAME}_output.txt" 2>&1

    if [ $? -eq 0 ]; then
        echo "  [PASS]"
        ((PASS_COUNT++))
    else
        echo "  [FAIL] Runtime error"
        ((FAIL_COUNT++))
    fi
done

# Return to regression directory
cd "$REGRESSION_DIR"

echo ""
echo "========================================"
echo "  Results: $PASS_COUNT passed, $FAIL_COUNT failed"
echo "========================================"

[ $FAIL_COUNT -eq 0 ] && exit 0 || exit 1
