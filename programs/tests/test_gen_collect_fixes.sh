#!/bin/bash
# Test script for gen_collect.obs fixes
# Run from: programs/tests directory

echo "=========================================="
echo "Testing gen_collect.obs Fixes"
echo "=========================================="
echo ""

PASS=0
FAIL=0

# Color codes
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Resolve paths relative to this script
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OBC="$ROOT_DIR/core/compiler/obc"
OBR="$ROOT_DIR/core/vm/obr"
COMPILER_DIR="$ROOT_DIR/core/compiler"

test_file() {
    local src=$1
    local lib=$2
    local desc=$3
    local abs_src="$SCRIPT_DIR/$src"
    local abs_dest="$SCRIPT_DIR/${src%.obs}.obe"

    echo "Testing: $desc ($src)"
    echo -n "  Compiling... "

    # Run compiler from its own directory so it finds libraries
    compile_output=$(cd "$COMPILER_DIR" && "$OBC" -src "$abs_src" -lib $lib -dest "$abs_dest" 2>&1)
    if [ $? -ne 0 ]; then
        echo -e "${RED}FAILED${NC}"
        echo "$compile_output"
        ((FAIL++))
        return 1
    fi

    echo -e "${GREEN}OK${NC}"
    echo -n "  Running... "

    if "$OBR" "$abs_dest" > /tmp/test_output.txt 2>&1; then
        echo -e "${GREEN}OK${NC}"
        ((PASS++))
        return 0
    else
        echo -e "${RED}FAILED${NC}"
        cat /tmp/test_output.txt
        ((FAIL++))
        return 1
    fi
}

echo "1. Vector and CompareVector Tests"
echo "   (First, Swap, Zip, Limit, Remove)"
test_file "prgm65.obs" "gen_collect" "Vector/CompareVector operations"
echo ""

echo "2. Hash Tests"
echo "   (Insert duplicate handling)"
test_file "prgm64.obs" "gen_collect" "Hash operations"
echo ""

echo "3. Stack Tests"
echo "   (Pop, ToArray non-destructive)"
test_file "prgm59.obs" "gen_collect" "Stack operations"
echo ""

echo "4. Queue Tests"
echo "   (RemoveBack fix)"
test_file "prgm126.obs" "gen_collect" "Queue operations"
echo ""

echo "5. Map and Vector.Zip Tests"
echo "   (Map.Each, Zip logic)"
test_file "prgm100.obs" "gen_collect" "Map and Zip operations"
echo ""

echo "=========================================="
echo "Test Results"
echo "=========================================="
echo -e "${GREEN}Passed: $PASS${NC}"
echo -e "${RED}Failed: $FAIL${NC}"
echo ""

if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}✓ All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}✗ Some tests failed${NC}"
    exit 1
fi
