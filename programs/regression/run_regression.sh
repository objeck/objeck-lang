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
NATIVE_LIB_DIR=$(cd "$(dirname "$COMPILER")/../lib/native" 2>/dev/null && pwd)

# Set library paths for native module loading
ABS_LIB_DIR=$(cd "$(dirname "$COMPILER")/../lib" 2>/dev/null && pwd)
if [ -d "$ABS_LIB_DIR" ]; then
    export OBJECK_LIB_PATH="$ABS_LIB_DIR"
fi
if [ -d "$NATIVE_LIB_DIR" ]; then
    export LD_LIBRARY_PATH="${NATIVE_LIB_DIR}:${LD_LIBRARY_PATH}"
    export DYLD_LIBRARY_PATH="${NATIVE_LIB_DIR}:${DYLD_LIBRARY_PATH}"
fi

# Run each test
for test in *.obs; do
    [ -f "$test" ] || continue
    NAME="${test%.obs}"
    echo "Running: $NAME..."

    # Get absolute test path
    ABS_TEST="${REGRESSION_DIR}/${test}"

    # Build library list: base libs + any EXTRA_LIBS from test file
    LIBS="cipher,collect,xml,json"
    EXTRA=$(grep -m1 '# EXTRA_LIBS:' "$test" 2>/dev/null | sed 's/.*# EXTRA_LIBS:[[:space:]]*//')
    if [ -n "$EXTRA" ]; then
        LIBS="${LIBS},${EXTRA}"
    fi

    # Change to compiler directory and compile
    cd "${DEPLOY_DIR}/bin"
    "$ABS_COMPILER" -src "$ABS_TEST" -lib "$LIBS" -opt s3 -dest "${REGRESSION_DIR}/${NAME}.obe" 2>&1 | tee "${REGRESSION_DIR}/${RESULTS_DIR}/${NAME}_compile.log" > /dev/null

    COMPILE_EXIT=${PIPESTATUS[0]}
    EXPECT_ERR=0
    grep -q '# EXPECT_COMPILE_ERROR' "$ABS_TEST" 2>/dev/null && EXPECT_ERR=1

    EXPECT_RT_ERR=0
    grep -q '# EXPECT_RUNTIME_ERROR' "$ABS_TEST" 2>/dev/null && EXPECT_RT_ERR=1

    cd "$REGRESSION_DIR"

    if [ $COMPILE_EXIT -ne 0 ]; then
        if [ $EXPECT_ERR -eq 1 ]; then
            # Compiler rejected bad code as expected — verify it produced output
            LOG_SIZE=$(wc -c < "${RESULTS_DIR}/${NAME}_compile.log" 2>/dev/null || echo 0)
            if [ "$LOG_SIZE" -gt 0 ]; then
                echo "  [PASS] (compile error as expected)"
                ((PASS_COUNT++))
            else
                echo "  [FAIL] compiler produced no output — possible crash"
                ((FAIL_COUNT++))
            fi
        else
            echo "  [FAIL] Compilation error"
            cat "${RESULTS_DIR}/${NAME}_compile.log" 2>/dev/null | head -10
            ((FAIL_COUNT++))
        fi
        continue
    fi

    if [ $EXPECT_ERR -eq 1 ]; then
        echo "  [FAIL] should have failed to compile"
        ((FAIL_COUNT++))
        continue
    fi

    # Per-test opt-out for auto-JIT (some analyzer tests hit a known
    # arm64 JIT bug; the marker keeps JIT coverage on for everything else).
    if grep -q '# JIT_DISABLE' "$test" 2>/dev/null; then
        OBJECK_JIT_DISABLE=1 "$ABS_VM" "$NAME.obe" > "$RESULTS_DIR/${NAME}_output.txt" 2>&1
    else
        "$ABS_VM" "$NAME.obe" > "$RESULTS_DIR/${NAME}_output.txt" 2>&1
    fi
    RUN_EXIT=$?

    if [ $EXPECT_RT_ERR -eq 1 ]; then
        # Runtime error expected — PASS if VM exited non-zero with output
        OUT_SIZE=$(wc -c < "$RESULTS_DIR/${NAME}_output.txt" 2>/dev/null || echo 0)
        if [ $RUN_EXIT -ne 0 ] && [ "$OUT_SIZE" -gt 0 ]; then
            echo "  [PASS] (runtime error as expected)"
            ((PASS_COUNT++))
        elif [ $RUN_EXIT -eq 0 ]; then
            echo "  [FAIL] should have failed at runtime"
            ((FAIL_COUNT++))
        else
            echo "  [FAIL] VM produced no output — possible crash"
            ((FAIL_COUNT++))
        fi
    elif [ $RUN_EXIT -eq 0 ]; then
        echo "  [PASS]"
        ((PASS_COUNT++))
    else
        echo "  [FAIL] Runtime error"
        cat "$RESULTS_DIR/${NAME}_output.txt" 2>/dev/null | head -200
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
