#!/bin/bash
# probe: loop tests under OBJECK_GC_VERIFY=1 + nursery, JIT on vs off
# usage: loop.sh <tree> <runs> <budget-seconds-per-config> test...
TREE="$1"; RUNS="$2"; BUDGET="$3"; shift 3
TO=timeout
command -v timeout >/dev/null 2>&1 || TO=gtimeout
command -v $TO >/dev/null 2>&1 || TO=""
OBC="$TREE/bin/obc"; OBR="$TREE/bin/obr"
export OBJECK_LIB_PATH="$TREE/lib"
export LD_LIBRARY_PATH="$TREE/lib/native:$LD_LIBRARY_PATH"
export DYLD_LIBRARY_PATH="$TREE/lib/native:$DYLD_LIBRARY_PATH"
REG="$PWD/programs/regression"
WORK="$PWD/probe-work"; mkdir -p "$WORK"
for t in "$@"; do
  for opt in s3 s0; do
    LIBS="cipher,collect,xml,json"
    EXTRA=$(grep -m1 '# EXTRA_LIBS:' "$REG/$t.obs" 2>/dev/null | sed 's/.*# EXTRA_LIBS:[[:space:]]*//' | tr -d '\r')
    [ -n "$EXTRA" ] && LIBS="$LIBS,$EXTRA"
    SRC="$REG/$t.obs"; [ -f "$SRC" ] || SRC="$PWD/probe/$t.obs"
    (cd "$TREE/bin" && ${TO:+$TO 300} "$OBC" -src "$SRC" -lib "$LIBS" -opt $opt -dest "$WORK/${t}_$opt.obe") > "$WORK/${t}_$opt.compile.txt" 2>&1 || { echo "COMPILE FAIL $t $opt"; cat "$WORK/${t}_$opt.compile.txt" | tail -5; continue; }
    for mode in jit nojit; do
      for cfg in "nursery" "major"; do
        JF=""; [ "$mode" = nojit ] && JF="--jit=off"
        GF=""; NUR="256k"; [ "$cfg" = major ] && { GF="--gc-threshold=64k"; NUR=""; }
        fails=0; n=0; start=$(date +%s); first=""
        while [ $n -lt $RUNS ]; do
          now=$(date +%s); [ $((now-start)) -ge $BUDGET ] && break
          n=$((n+1))
          if [ -n "$NUR" ]; then
            out=$(cd "$REG" && OBJECK_GC_VERIFY=1 OBJECK_NURSERY=$NUR ${TO:+$TO 300} "$OBR" $JF $GF "$WORK/${t}_$opt.obe" 2>&1); rc=$?
          else
            out=$(cd "$REG" && OBJECK_GC_VERIFY=1 ${TO:+$TO 300} "$OBR" $JF $GF "$WORK/${t}_$opt.obe" 2>&1); rc=$?
          fi
          if echo "$out" | grep -q "violation"; then
            fails=$((fails+1))
            [ -z "$first" ] && first=$(echo "$out" | grep -m2 "violation")
          elif [ $rc -ne 0 ]; then
            echo "  nonzero rc=$rc (no violation) $t $opt $mode $cfg: $(echo "$out" | tail -3 | tr '\n' ' ')"
          fi
        done
        echo "RESULT $t opt=$opt $mode $cfg: violations $fails/$n"
        [ -n "$first" ] && echo "  first: $first"
      done
    done
  done
done
