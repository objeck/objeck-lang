#!/bin/bash
# Debugger regression tests for Objeck language
# Usage: ./run_debugger_tests.sh [x64|arm64]
#
# Requires: expect (apt-get install expect / brew install expect)

PLATFORM=${1:-x64}

# Detect platform-specific deployment directory
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

# Check for expect
if ! command -v expect &> /dev/null; then
    echo "WARNING: 'expect' not found, skipping debugger tests"
    exit 0
fi

# Check for debugger binary
if [ ! -f "$DEBUGGER" ]; then
    echo "WARNING: debugger binary not found at $DEBUGGER, skipping"
    exit 0
fi

# Get absolute paths
REGRESSION_DIR=$(pwd)
ABS_COMPILER=$(cd "$(dirname "$COMPILER")" && pwd)/$(basename "$COMPILER")
ABS_DEBUGGER=$(cd "$(dirname "$DEBUGGER")" && pwd)/$(basename "$DEBUGGER")

# Set library paths
export OBJECK_LIB_PATH="$(cd "$(dirname "$COMPILER")/../lib" && pwd)/"
if [ "$(uname)" = "Darwin" ]; then
    export DYLD_LIBRARY_PATH="$(cd "$(dirname "$COMPILER")/../lib/native" && pwd)"
else
    export LD_LIBRARY_PATH="$(cd "$(dirname "$COMPILER")/../lib/native" && pwd)"
fi

PASS_COUNT=0
FAIL_COUNT=0

echo "========================================"
echo "  Objeck Debugger Test Suite"
echo "  Platform: $PLATFORM"
echo "========================================"
echo ""

# Compile test program with debug symbols
TEST_SRC="debugger_test.obs"
TEST_BIN="${REGRESSION_DIR}/debugger_test.obe"

echo "Compiling debugger test program..."
cd "${DEPLOY_DIR}/bin"
"$ABS_COMPILER" -src "${REGRESSION_DIR}/${TEST_SRC}" -dest "$TEST_BIN" -debug 2>&1 | tee "${REGRESSION_DIR}/${RESULTS_DIR}/debugger_compile.log" > /dev/null
if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo "  [FAIL] Compilation error"
    exit 1
fi
cd "$REGRESSION_DIR"
echo "  Compiled successfully."
echo ""

# Helper function to run an expect test
run_test() {
    local TEST_NAME="$1"
    local EXPECT_SCRIPT="$2"
    local EXPECTED_PATTERNS="$3"

    echo -n "Running: ${TEST_NAME}..."

    # Run expect script and capture output
    OUTPUT=$(expect -c "
        log_user 1
        set timeout 10
        spawn $ABS_DEBUGGER -b $TEST_BIN -src $REGRESSION_DIR
        $EXPECT_SCRIPT
    " 2>&1)

    # Save output
    echo "$OUTPUT" > "${RESULTS_DIR}/debugger_${TEST_NAME}.log"

    # Check all expected patterns
    local ALL_PASS=true
    IFS='|' read -ra PATTERNS <<< "$EXPECTED_PATTERNS"
    for pattern in "${PATTERNS[@]}"; do
        if ! echo "$OUTPUT" | grep -qF "$pattern"; then
            ALL_PASS=false
            echo ""
            echo "  Missing expected output: '$pattern'"
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

# ========================================
# Test 1: Help command
# ========================================
run_test "help" '
    expect ">"
    send "h\r"
    expect ">"
    send "q\r"
    expect eof
' 'Commands:|b, break <file>:<line>|s, step|n, next|j, jump|p, print <expr>|q, quit'

# ========================================
# Test 2: Breakpoint set/list/delete
# ========================================
run_test "breakpoints" '
    expect ">"
    send "b debugger_test.obs:30\r"
    expect ">"
    send "b debugger_test.obs:34\r"
    expect ">"
    send "breaks\r"
    expect ">"
    send "d debugger_test.obs:30\r"
    expect ">"
    send "breaks\r"
    expect ">"
    send "q\r"
    expect eof
' "added breakpoint: file='debugger_test.obs:30'|added breakpoint: file='debugger_test.obs:34'|removed breakpoint: file='debugger_test.obs:30'"

# ========================================
# Test 3: Run and hit breakpoint
# ========================================
run_test "run_break" '
    expect ">"
    send "b debugger_test.obs:34\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' "added breakpoint|break: file=|method='Main->Main(..)'"

# ========================================
# Test 4: Print variables
# ========================================
run_test "print_vars" '
    expect ">"
    send "b debugger_test.obs:38\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "p sum\r"
    expect ">"
    send "p values\r"
    expect ">"
    send "p counter\r"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' 'print: type=Int/Byte/Bool, value=100|print: type=Int[], value=|dimension=1, size=5|print: type=Counter'

# ========================================
# Test 5: Step into method
# ========================================
run_test "step_into" '
    expect ">"
    send "b debugger_test.obs:34\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "s\r"
    expect ">"
    send "l\r"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' "Counter->Increment(..)|@count += 1"

# ========================================
# Test 6: Step over (next)
# ========================================
run_test "step_over" '
    expect ">"
    send "b debugger_test.obs:34\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "n\r"
    expect ">"
    send "n\r"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' "break: file=|Main->Main(..)"

# ========================================
# Test 7: Stack trace
# ========================================
run_test "stack_trace" '
    expect ">"
    send "b debugger_test.obs:9\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "stack\r"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' "stack:|frame: pos="

# ========================================
# Test 8: List source
# ========================================
run_test "list_source" '
    expect ">"
    send "b debugger_test.obs:34\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "l\r"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' "counter->Increment()"

# ========================================
# Test 9: Memory command
# ========================================
run_test "memory" '
    expect ">"
    send "b debugger_test.obs:38\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "m\r"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' 'memory: allocated='

# ========================================
# Test 10: Info command
# ========================================
run_test "info" '
    expect ">"
    send "b debugger_test.obs:34\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "i\r"
    expect ">"
    send "i class=Counter\r"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' 'general info:|class: type=Counter'

# ========================================
# Test 11: Print @self and instance vars
# ========================================
run_test "print_self" '
    expect ">"
    send "b debugger_test.obs:9\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "p @self\r"
    expect ">"
    send "p @count\r"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' 'print: type=Counter|print: type=Int/Byte/Bool'

# ========================================
# Test 12: Step out (jump)
# ========================================
run_test "step_out" '
    expect ">"
    send "b debugger_test.obs:34\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "s\r"
    expect "Counter->Increment"
    expect ">"
    send "j\r"
    expect "Main->Main"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' "Counter->Increment(..)|Main->Main(..)"

# ========================================
# Test 13: Full program execution
# ========================================
run_test "full_run" '
    expect ">"
    send "r\r"
    expect ">"
    send "q\r"
    expect eof
' 'Sum=100, Counter=3|Count is greater than 2|Factorial(5)=120'

# ========================================
# Test 14: Clear breakpoints
# ========================================
run_test "clear_breaks" '
    expect ">"
    send "b debugger_test.obs:30\r"
    expect ">"
    send "b debugger_test.obs:34\r"
    expect ">"
    send "clear\r"
    expect "?"
    send "y\r"
    expect ">"
    send "breaks\r"
    expect ">"
    send "q\r"
    expect eof
' 'no breakpoints defined.'

echo ""
echo "========================================"
echo "  Results: $PASS_COUNT passed, $FAIL_COUNT failed"
echo "========================================"

# Clean up
rm -f "$TEST_BIN"

[ $FAIL_COUNT -eq 0 ] && exit 0 || exit 1
