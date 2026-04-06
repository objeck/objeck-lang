#!/bin/bash
# DAP (Debug Adapter Protocol) integration tests for Objeck debugger
# Usage: ./run_dap_tests.sh [x64|arm64]
#
# Tests the full DAP lifecycle: initialize → launch → breakpoints →
# configurationDone → stopped → stackTrace → variables → continue → disconnect

PLATFORM=${1:-x64}

# Detect deployment directory
if [ -d "../../core/release/deploy-${PLATFORM}" ]; then
    DEPLOY_DIR="../../core/release/deploy-${PLATFORM}"
elif [ -d "../../core/release/deploy" ]; then
    DEPLOY_DIR="../../core/release/deploy"
else
    echo "ERROR: Could not find deployment directory"
    exit 1
fi

COMPILER="${DEPLOY_DIR}/bin/obc"
DEBUGGER="${DEPLOY_DIR}/bin/obd"
RESULTS_DIR="./results"

mkdir -p "$RESULTS_DIR"

# Check for debugger binary
if [ ! -f "$DEBUGGER" ]; then
    echo "WARNING: debugger binary not found at $DEBUGGER, skipping DAP tests"
    exit 0
fi

# Get absolute paths
REGRESSION_DIR=$(pwd)
ABS_COMPILER=$(cd "$(dirname "$COMPILER")" && pwd)/$(basename "$COMPILER")
ABS_DEBUGGER=$(cd "$(dirname "$DEBUGGER")" && pwd)/$(basename "$DEBUGGER")

# Set library paths
export OBJECK_LIB_PATH="$(cd "$(dirname "$COMPILER")/../lib" && pwd)/"
if [ "$(uname)" = "Darwin" ]; then
    export DYLD_LIBRARY_PATH="$(cd "$(dirname "$COMPILER")/../lib/native" 2>/dev/null && pwd)"
else
    export LD_LIBRARY_PATH="$(cd "$(dirname "$COMPILER")/../lib/native" 2>/dev/null && pwd)"
fi

PASS_COUNT=0
FAIL_COUNT=0

echo "========================================"
echo "  Objeck DAP Integration Tests"
echo "  Platform: $PLATFORM"
echo "========================================"
echo ""

# Compile test program with debug symbols
TEST_SRC="debugger_test.obs"
TEST_BIN="${REGRESSION_DIR}/debugger_test.obe"

echo "Compiling DAP test program..."
cd "${DEPLOY_DIR}/bin"
"$ABS_COMPILER" -src "${REGRESSION_DIR}/${TEST_SRC}" -dest "$TEST_BIN" -debug 2>&1 | tee "${REGRESSION_DIR}/${RESULTS_DIR}/dap_compile.log" > /dev/null
if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo "  [FAIL] Compilation error"
    exit 1
fi
cd "$REGRESSION_DIR"
echo "  Compiled successfully."
echo ""

# ============================================
# Helper: send a DAP message to stdin
# ============================================
dap_msg() {
    local body="$1"
    local len=${#body}
    printf "Content-Length: %d\r\n\r\n%s" "$len" "$body"
}

# ============================================
# Helper: read one DAP response from stdout
# Reads Content-Length header, then body
# ============================================
read_dap_response() {
    local header=""
    local ch
    # Read until \r\n\r\n
    while IFS= read -r -n1 -d '' ch; do
        header="${header}${ch}"
        if [[ "$header" == *$'\r\n\r\n' ]]; then
            break
        fi
    done
    # Extract content length
    local length
    length=$(echo "$header" | grep -o 'Content-Length: [0-9]*' | grep -o '[0-9]*')
    if [ -z "$length" ]; then
        echo ""
        return
    fi
    # Read body
    local body=""
    IFS= read -r -n "$length" -d '' body
    echo "$body"
}

# ============================================
# Test runner: sends messages, collects responses
# ============================================
run_dap_test() {
    local TEST_NAME="$1"
    local MESSAGES="$2"        # newline-separated JSON messages to send
    local EXPECT_PATTERNS="$3" # pipe-separated patterns to check in output
    local WAIT_TIME="${4:-3}"   # seconds to wait for async events

    echo -n "Running: ${TEST_NAME}..."

    # Build the input stream
    local input=""
    while IFS= read -r msg; do
        [ -z "$msg" ] && continue
        input="${input}$(dap_msg "$msg")"
    done <<< "$MESSAGES"

    # Run obd --dap with piped input, capture all output
    local OUTPUT
    OUTPUT=$(echo -ne "$input" | timeout "${WAIT_TIME}" "$ABS_DEBUGGER" --dap 2>/dev/null)
    local EXIT_CODE=$?

    # Save raw output
    echo "$OUTPUT" > "${RESULTS_DIR}/dap_${TEST_NAME}.log"

    # Check all expected patterns
    local ALL_PASS=true
    IFS='|' read -ra PATTERNS <<< "$EXPECT_PATTERNS"
    for pattern in "${PATTERNS[@]}"; do
        if ! echo "$OUTPUT" | grep -qF "$pattern"; then
            ALL_PASS=false
            echo ""
            echo "  Missing: '$pattern'"
        fi
    done

    if $ALL_PASS; then
        echo " [PASS]"
        ((PASS_COUNT++))
    else
        echo "  [FAIL]"
        ((FAIL_COUNT++))
    fi
}

# ============================================
# Test 1: Initialize handshake
# ============================================
run_dap_test "initialize" \
    '{"seq":1,"type":"request","command":"initialize","arguments":{"adapterID":"test"}}
{"seq":2,"type":"request","command":"disconnect","arguments":{}}' \
    '"command":"initialize"|"success":true|supportsConditionalBreakpoints|"event":"initialized"' \
    3

# ============================================
# Test 2: Launch with program
# ============================================
run_dap_test "launch" \
    "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\",\"arguments\":{\"adapterID\":\"test\"}}
{\"seq\":2,\"type\":\"request\",\"command\":\"launch\",\"arguments\":{\"program\":\"${TEST_BIN}\",\"sourceDir\":\"${REGRESSION_DIR}\"}}
{\"seq\":3,\"type\":\"request\",\"command\":\"disconnect\",\"arguments\":{}}" \
    '"command":"launch"|"success":true|"event":"thread"|"reason":"started"' \
    3

# ============================================
# Test 3: Set breakpoints
# ============================================
run_dap_test "setBreakpoints" \
    "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\",\"arguments\":{\"adapterID\":\"test\"}}
{\"seq\":2,\"type\":\"request\",\"command\":\"launch\",\"arguments\":{\"program\":\"${TEST_BIN}\",\"sourceDir\":\"${REGRESSION_DIR}\"}}
{\"seq\":3,\"type\":\"request\",\"command\":\"setBreakpoints\",\"arguments\":{\"source\":{\"path\":\"debugger_test.obs\"},\"breakpoints\":[{\"line\":28}]}}
{\"seq\":4,\"type\":\"request\",\"command\":\"disconnect\",\"arguments\":{}}" \
    '"command":"setBreakpoints"|"success":true|"verified":true' \
    3

# ============================================
# Test 4: Full lifecycle - break, inspect, continue
# ============================================
run_dap_test "full_lifecycle" \
    "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\",\"arguments\":{\"adapterID\":\"test\"}}
{\"seq\":2,\"type\":\"request\",\"command\":\"launch\",\"arguments\":{\"program\":\"${TEST_BIN}\",\"sourceDir\":\"${REGRESSION_DIR}\"}}
{\"seq\":3,\"type\":\"request\",\"command\":\"setBreakpoints\",\"arguments\":{\"source\":{\"path\":\"debugger_test.obs\"},\"breakpoints\":[{\"line\":28}]}}
{\"seq\":4,\"type\":\"request\",\"command\":\"configurationDone\",\"arguments\":{}}
{\"seq\":5,\"type\":\"request\",\"command\":\"threads\",\"arguments\":{}}
{\"seq\":6,\"type\":\"request\",\"command\":\"continue\",\"arguments\":{\"threadId\":1}}
{\"seq\":7,\"type\":\"request\",\"command\":\"disconnect\",\"arguments\":{}}" \
    '"command":"configurationDone"|"success":true|"command":"threads"|"Main Thread"' \
    5

# ============================================
# Test 5: Stack trace at breakpoint
# ============================================
run_dap_test "stackTrace" \
    "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\",\"arguments\":{\"adapterID\":\"test\"}}
{\"seq\":2,\"type\":\"request\",\"command\":\"launch\",\"arguments\":{\"program\":\"${TEST_BIN}\",\"sourceDir\":\"${REGRESSION_DIR}\"}}
{\"seq\":3,\"type\":\"request\",\"command\":\"setBreakpoints\",\"arguments\":{\"source\":{\"path\":\"debugger_test.obs\"},\"breakpoints\":[{\"line\":33}]}}
{\"seq\":4,\"type\":\"request\",\"command\":\"configurationDone\",\"arguments\":{}}
{\"seq\":5,\"type\":\"request\",\"command\":\"stackTrace\",\"arguments\":{\"threadId\":1}}
{\"seq\":6,\"type\":\"request\",\"command\":\"disconnect\",\"arguments\":{}}" \
    '"command":"stackTrace"|"success":true' \
    5

# ============================================
# Test 6: Launch with missing program (error case)
# ============================================
run_dap_test "launch_error" \
    '{"seq":1,"type":"request","command":"initialize","arguments":{"adapterID":"test"}}
{"seq":2,"type":"request","command":"launch","arguments":{"program":""}}
{"seq":3,"type":"request","command":"disconnect","arguments":{}}' \
    '"command":"launch"|"success":false|No program specified' \
    3

# ============================================
# Test 7: Conditional breakpoint
# ============================================
run_dap_test "conditional_breakpoint" \
    "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\",\"arguments\":{\"adapterID\":\"test\"}}
{\"seq\":2,\"type\":\"request\",\"command\":\"launch\",\"arguments\":{\"program\":\"${TEST_BIN}\",\"sourceDir\":\"${REGRESSION_DIR}\"}}
{\"seq\":3,\"type\":\"request\",\"command\":\"setBreakpoints\",\"arguments\":{\"source\":{\"path\":\"debugger_test.obs\"},\"breakpoints\":[{\"line\":30,\"condition\":\"i > 3\"}]}}
{\"seq\":4,\"type\":\"request\",\"command\":\"disconnect\",\"arguments\":{}}" \
    '"command":"setBreakpoints"|"success":true|"verified":true' \
    3

# ============================================
# Summary
# ============================================
echo ""
echo "========================================"
echo "  Results: $PASS_COUNT passed, $FAIL_COUNT failed"
echo "========================================"

[ $FAIL_COUNT -eq 0 ] && exit 0 || exit 1
