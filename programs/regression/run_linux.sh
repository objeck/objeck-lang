#!/bin/bash

DEPLOY="/mnt/c/Users/objec/Documents/Code/objeck-lang/core/release/deploy"
REGDIR="/mnt/c/Users/objec/Documents/Code/objeck-lang/programs/regression"
export OBJECK_LIB_PATH="$DEPLOY/lib"

OBC="$DEPLOY/bin/obc"
OBR="$DEPLOY/bin/obr"

mkdir -p "$REGDIR/results"

PASS=0
FAIL=0

echo "========================================"
echo "  Objeck Regression Test Suite (Linux)"
echo "========================================"
echo ""

cd "$REGDIR"
for test in *.obs; do
    [ -f "$test" ] || continue
    NAME="${test%.obs}"
    echo -n "Running: $NAME... "

    # Compile from deploy/bin dir
    cd "$DEPLOY/bin"
    "$OBC" -src "$REGDIR/$test" -lib cipher,collect,xml,json -opt s3 -dest "$REGDIR/${NAME}.obe" > "$REGDIR/results/${NAME}_compile.log" 2>&1
    COMPILE_RC=$?

    cd "$REGDIR"
    if [ $COMPILE_RC -ne 0 ]; then
        echo "[FAIL] Compile"
        FAIL=$((FAIL + 1))
        continue
    fi

    # Run
    "$OBR" "${NAME}.obe" > "results/${NAME}_output.txt" 2>&1
    RUN_RC=$?

    if [ $RUN_RC -eq 0 ]; then
        echo "[PASS]"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] Runtime (exit=$RUN_RC)"
        FAIL=$((FAIL + 1))
    fi
done

echo ""
echo "========================================"
echo "  Results: $PASS passed, $FAIL failed"
echo "========================================"

[ $FAIL -eq 0 ] && exit 0 || exit 1
