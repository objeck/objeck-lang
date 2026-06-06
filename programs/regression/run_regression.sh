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
FAILED_TESTS=()

# Record a failure: print it, remember the test name + reason for the final
# summary, and bump the counter. Keeps the end-of-run report from forcing the
# reader to scroll back through hundreds of log lines to find what broke.
record_fail() {
    echo "  [FAIL] $1"
    FAILED_TESTS+=("${NAME} — $1")
    ((FAIL_COUNT++))
}

# Per-test wall-clock cap so a hung/infinite-loop test fails fast instead of
# pinning the CI job until GitHub's 6-hour limit. Use GNU coreutils 'timeout'
# (Linux) or 'gtimeout' (macOS+brew); degrade gracefully if neither exists.
TEST_TIMEOUT=${TEST_TIMEOUT:-60}
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT="timeout ${TEST_TIMEOUT}"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT="gtimeout ${TEST_TIMEOUT}"
else
    TIMEOUT=""
    echo "WARNING: no 'timeout'/'gtimeout' found — tests run without a per-test cap"
fi

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
                record_fail "compiler produced no output — possible crash"
            fi
        else
            record_fail "compilation error"
            cat "${RESULTS_DIR}/${NAME}_compile.log" 2>/dev/null | head -10
        fi
        continue
    fi

    if [ $EXPECT_ERR -eq 1 ]; then
        record_fail "should have failed to compile"
        continue
    fi

    # Per-test opt-out for auto-JIT (some analyzer tests hit a known
    # arm64 JIT bug; the marker keeps JIT coverage on for everything else).
    SECONDS=0
    if grep -q '# JIT_DISABLE' "$test" 2>/dev/null; then
        $TIMEOUT env OBJECK_JIT_DISABLE=1 "$ABS_VM" "$NAME.obe" > "$RESULTS_DIR/${NAME}_output.txt" 2>&1
    else
        $TIMEOUT "$ABS_VM" "$NAME.obe" > "$RESULTS_DIR/${NAME}_output.txt" 2>&1
    fi
    RUN_EXIT=$?
    ELAPSED=$SECONDS

    # 124 = killed by 'timeout'. Always a hard FAIL — never let a hang masquerade
    # as an expected runtime error (which only needs a non-zero exit + output).
    if [ $RUN_EXIT -eq 124 ]; then
        record_fail "timed out after ${TEST_TIMEOUT}s (possible hang / infinite loop)"
        continue
    fi

    if [ $EXPECT_RT_ERR -eq 1 ]; then
        # Runtime error expected — PASS if VM exited non-zero with output
        OUT_SIZE=$(wc -c < "$RESULTS_DIR/${NAME}_output.txt" 2>/dev/null || echo 0)
        if [ $RUN_EXIT -ne 0 ] && [ "$OUT_SIZE" -gt 0 ]; then
            echo "  [PASS] (runtime error as expected, ${ELAPSED}s)"
            ((PASS_COUNT++))
        elif [ $RUN_EXIT -eq 0 ]; then
            record_fail "should have failed at runtime"
        else
            record_fail "VM produced no output — possible crash"
        fi
    elif [ $RUN_EXIT -eq 0 ]; then
        echo "  [PASS] (${ELAPSED}s)"
        ((PASS_COUNT++))
    else
        record_fail "runtime error (exit ${RUN_EXIT})"
        cat "$RESULTS_DIR/${NAME}_output.txt" 2>/dev/null | head -200
    fi
done

# Return to regression directory
cd "$REGRESSION_DIR"

echo ""
echo "========================================"
echo "  Results: $PASS_COUNT passed, $FAIL_COUNT failed"
echo "========================================"

# List exactly what failed so the reader never has to scroll the full log.
if [ $FAIL_COUNT -gt 0 ]; then
    echo ""
    echo "Failed tests:"
    for t in "${FAILED_TESTS[@]}"; do
        echo "  ✗ ${t}"
    done
fi

# Emit a GitHub Actions step summary when running in CI, so the pass/fail
# breakdown (and the names of any failures) is visible on the run's summary
# page without opening the raw job log.
if [ -n "$GITHUB_STEP_SUMMARY" ]; then
    {
        echo "### Regression — ${PLATFORM}: ${PASS_COUNT} passed, ${FAIL_COUNT} failed"
        if [ $FAIL_COUNT -gt 0 ]; then
            echo ""
            echo "| Result | Test — reason |"
            echo "| :----: | :------------ |"
            for t in "${FAILED_TESTS[@]}"; do
                echo "| ❌ | ${t} |"
            done
        else
            echo ""
            echo "All regression tests passed ✅"
        fi
        echo ""
    } >> "$GITHUB_STEP_SUMMARY"
fi

[ $FAIL_COUNT -eq 0 ] && exit 0 || exit 1
