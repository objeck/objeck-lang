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

# Disable ANSI colors for test pattern matching
export NO_COLOR=1

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

# Collection-printing fixture (needs gen_collect)
COLL_SRC="debugger_coll_test.obs"
COLL_BIN="${REGRESSION_DIR}/debugger_coll_test.obe"

echo "Compiling collection test program..."
cd "${DEPLOY_DIR}/bin"
"$ABS_COMPILER" -src "${REGRESSION_DIR}/${COLL_SRC}" -lib gen_collect -dest "$COLL_BIN" -debug > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "  [FAIL] Collection test compilation error"
    exit 1
fi
cd "$REGRESSION_DIR"
echo "  Compiled successfully."
echo ""

# Evaluator / breakpoint-bookkeeping fixture
EVAL_SRC="debugger_eval_test.obs"
EVAL_BIN="${REGRESSION_DIR}/debugger_eval_test.obe"

echo "Compiling evaluator test program..."
cd "${DEPLOY_DIR}/bin"
"$ABS_COMPILER" -src "${REGRESSION_DIR}/${EVAL_SRC}" -dest "$EVAL_BIN" -debug > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "  [FAIL] Evaluator test compilation error"
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
    local BIN="${4:-$TEST_BIN}"

    echo -n "Running: ${TEST_NAME}..."

    # Write the expect script to a file rather than passing it with -c.
    #
    # With -c the script is interpolated into a double-quoted shell string, so
    # everything in it is expanded a second time by the shell before expect ever
    # sees it -- which makes an embedded double quote impossible to write. A
    # test that needs to send  p "text"  cannot be expressed at all that way.
    # printf passes the caller's script through verbatim; only the spawn line,
    # which is built here, needs expansion.
    local SCRIPT_FILE="${RESULTS_DIR}/expect_${TEST_NAME}.exp"
    {
        echo "log_user 1"
        echo "set timeout 10"
        echo "spawn $ABS_DEBUGGER -b $BIN -src $REGRESSION_DIR"
        printf '%s
' "$EXPECT_SCRIPT"
    } > "$SCRIPT_FILE"

    OUTPUT=$(expect -f "$SCRIPT_FILE" 2>&1)

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
    expect "q, quit"
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

# ========================================
# Test 15: Conditional breakpoint (b file:line if <expr>)
# Factorial(5) recurses n = 5,4,3,2,1; the condition makes line 51
# fire only when n = 3, exercising the "if <expr>" clause.
# ========================================
run_test "conditional_break" '
    expect ">"
    send "b debugger_test.obs:51 if n = 3\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "p n\r"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' 'added breakpoint|break: file=|Main->Factorial|print: type=Int/Byte/Bool, value=3'

# ========================================
# Test 16: Frame navigation (frame/up/down/locals across frames)
# ========================================
run_test "frame_nav" '
    expect ">"
    send "b debugger_test.obs:51 if n = 3\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "locals\r"
    expect ">"
    send "up\r"
    expect ">"
    send "locals\r"
    expect ">"
    send "down\r"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' "locals (frame #|value=3|frame #|value=4"

# ========================================
# Test 17: set <var> = <value> mutates a live variable
# ========================================
run_test "set_var" '
    expect ">"
    send "b debugger_test.obs:51 if n = 3\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "set n = 99\r"
    expect ">"
    send "p n\r"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' "set: value=99|print: type=Int/Byte/Bool, value=99"

# ========================================
# Test 18: breakpoint by method (b Class->Method)
# ========================================
run_test "method_break" '
    expect ">"
    send "b Main->Factorial\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "p n\r"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' "added breakpoint|Main->Factorial|print: type=Int/Byte/Bool, value=5"

# ========================================
# Test 19: temporary (one-shot) breakpoint fires once
# ========================================
run_test "tbreak" '
    expect ">"
    send "tbreak debugger_test.obs:51\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "p n\r"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' "[temporary]|break: file=|print: type=Int/Byte/Bool, value=5|Factorial(5)=120"

# ========================================
# Test 20: disable suppresses a breakpoint
# ========================================
run_test "disable_break" '
    expect ">"
    send "b debugger_test.obs:51\r"
    expect ">"
    send "disable 1\r"
    expect ">"
    send "r\r"
    expect ">"
    send "q\r"
    expect eof
' "disabled 1 breakpoint|Factorial(5)=120"

# ========================================
# Test 21: ignore count skips the next N hits
# ========================================
run_test "ignore_count" '
    expect ">"
    send "b debugger_test.obs:51\r"
    expect ">"
    send "ignore 1 2\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "p n\r"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' "will be ignored|print: type=Int/Byte/Bool, value=3"

# ========================================
# Test 22: until <line> runs to a line in the current frame
# ========================================
run_test "until_line" '
    expect ">"
    send "b debugger_test.obs:46\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "until 47\r"
    expect ">"
    send "p result\r"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' "running until|break: file=|print: type=Int/Byte/Bool, value=120"

# ========================================
# Test 23: breakpoint on a non-executable line is relocated with a note
# ========================================
run_test "nonexec_line" '
    expect ">"
    send "b debugger_test.obs:1\r"
    expect ">"
    send "q\r"
    expect eof
' "has no executable code|added breakpoint"

# ========================================
# Test 24: watchpoint breaks when a watched variable changes
# ========================================
run_test "watchpoint" '
    expect ">"
    send "b debugger_test.obs:51 if n = 3\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "watch n\r"
    expect ">"
    send "c\r"
    expect ">"
    send "q\r"
    expect eof
' "added watchpoint|watch #1 changed"

# ========================================
# Collection sizes. `print` reached through slot 0 into the backing array or
# root node, so a Vector always reported 1, a Map reported a child pointer, and
# printing a List dereferenced its @size as a pointer and crashed. The counts
# below are all different so a header value cannot pass by coincidence.
# ========================================
run_test "print_collections" '
    expect ">"
    send "b debugger_coll_test.obs:35"
    expect ">"
    send "r"
    expect "break:"
    expect ">"
    send "p vec"
    expect ">"
    send "p map"
    expect ">"
    send "p hash"
    expect ">"
    send "p list"
    expect ">"
    send "c"
    expect ">"
    send "q"
    expect eof
' "type=Collection.Vector, size=5|type=Collection.Map, size=4|type=Collection.Hash, size=3|type=Collection.List, size=6" "$COLL_BIN"

# ===========================================================================
# Expression evaluator and breakpoint bookkeeping.
#
# Each of these fails on the previous build. Four of the behaviours pinned down
# here used to report SUCCESS while doing the wrong thing, which is why none was
# noticed -- an assertion that a command merely "ran" would still have passed.
# ===========================================================================

# Test 25: 'delete <id>' removes THAT breakpoint, not the one at the current line
run_test "delete_by_id" '
    expect ">"
    send "b debugger_eval_test.obs:56\r"
    expect ">"
    send "b debugger_eval_test.obs:58\r"
    expect ">"
    send "delete 2\r"
    expect ">"
    send "breaks\r"
    expect ">"
    send "delete 99\r"
    expect ">"
    send "q\r"
    expect eof
' "removed breakpoint #2|break #1:|no breakpoint with id #99" "$EVAL_BIN"

# Test 26: 'unwatch' with no id removes EVERY watchpoint (erase already advances)
run_test "unwatch_all" '
    expect ">"
    send "b debugger_eval_test.obs:56\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "watch zero\r"
    expect ">"
    send "watch five\r"
    expect ">"
    send "watch total\r"
    expect ">"
    send "unwatch\r"
    expect ">"
    send "watches\r"
    expect ">"
    send "q\r"
    expect eof
' "removed 3 watchpoint(s)|no watchpoints defined" "$EVAL_BIN"

# Test 27: 'watches' names the expression it is watching
run_test "watches_show_expression" '
    expect ">"
    send "b debugger_eval_test.obs:56\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "watch five\r"
    expect ">"
    send "watches\r"
    expect ">"
    send "q\r"
    expect eof
' "watch #1: five" "$EVAL_BIN"

# Test 28: an unknown watch id is reported rather than silently ignored
run_test "unwatch_missing_id" '
    expect ">"
    send "b debugger_eval_test.obs:56\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "watch five\r"
    expect ">"
    send "unwatch 42\r"
    expect ">"
    send "q\r"
    expect eof
' "no watchpoint #42" "$EVAL_BIN"

# Test 29: a string literal prints -- it used to print nothing at all
run_test "print_string_literal" '
    expect ">"
    send "b debugger_eval_test.obs:56\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "p \"widget\"\r"
    expect ">"
    send "q\r"
    expect eof
' "print: type=System.String, value=\"widget\"" "$EVAL_BIN"

# Test 30: a String variable compares against a literal (conditional breakpoints)
run_test "string_comparison" '
    expect ">"
    send "b debugger_eval_test.obs:56\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "p text = \"widget\"\r"
    expect ">"
    send "p text = \"other\"\r"
    expect ">"
    send "q\r"
    expect eof
' "print: type=Bool, value=true|print: type=Bool, value=false" "$EVAL_BIN"

# Test 31: true/false are literals, not variable lookups
run_test "boolean_literals" '
    expect ">"
    send "b debugger_eval_test.obs:56\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "p true\r"
    expect ">"
    send "p false\r"
    expect ">"
    send "q\r"
    expect eof
' "print: type=Bool, value=true|print: type=Bool, value=false" "$EVAL_BIN"

# Test 32: Nil is a literal, so an object can be tested for it
run_test "nil_literal" '
    expect ">"
    send "b debugger_eval_test.obs:56\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "p empty = Nil\r"
    expect ">"
    send "p holder = Nil\r"
    expect ">"
    send "q\r"
    expect eof
' "print: type=Bool, value=true|print: type=Bool, value=false" "$EVAL_BIN"

# Test 33: printing an object lists its fields instead of a bare hex address
run_test "print_object_fields" '
    expect ">"
    send "b debugger_eval_test.obs:56\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "p holder\r"
    expect ">"
    send "q\r"
    expect eof
' "print: type=Holder|@count = 7|@ratio = 1.5|@next = Nil" "$EVAL_BIN"

# Test 34: 'set' refuses a value it cannot store instead of writing zero
run_test "set_rejects_string" '
    expect ">"
    send "b debugger_eval_test.obs:56\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "set five = \"oops\"\r"
    expect ">"
    send "p five\r"
    expect ">"
    send "q\r"
    expect eof
' "cannot set: only Int, Char and Float|value=5" "$EVAL_BIN"

# Test 35: a zero numerator is valid modulus
run_test "modulus_zero_numerator" '
    expect ">"
    send "b debugger_eval_test.obs:56\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "p zero % five\r"
    expect ">"
    send "q\r"
    expect eof
' "print: type=Int, value=0" "$EVAL_BIN"

# Test 36: division/modulus by zero are reported, not executed
run_test "divide_by_zero" '
    expect ">"
    send "b debugger_eval_test.obs:56\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "p five / zero\r"
    expect ">"
    send "p five % zero\r"
    expect ">"
    send "q\r"
    expect eof
' "division by zero|modulus by zero" "$EVAL_BIN"

# Test 37: a class holding only statics shows them
run_test "info_static_only_class" '
    expect ">"
    send "b debugger_eval_test.obs:56\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "info class=Registry\r"
    expect ">"
    send "q\r"
    expect eof
' "class: type=Registry|@seen|@label_text" "$EVAL_BIN"

# Test 38: a leading space no longer breaks every command
run_test "leading_whitespace" '
    expect ">"
    send "b debugger_eval_test.obs:56\r"
    expect ">"
    send "r\r"
    expect "break:"
    expect ">"
    send "   p five\r"
    expect ">"
    send "q\r"
    expect eof
' "value=5" "$EVAL_BIN"

echo ""
echo "========================================"
echo "  Results: $PASS_COUNT passed, $FAIL_COUNT failed"
echo "========================================"

# Clean up
rm -f "$TEST_BIN" "$EVAL_BIN"

[ $FAIL_COUNT -eq 0 ] && exit 0 || exit 1
