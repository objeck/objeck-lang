#!/bin/bash
# Native (non-Docker) cross-language benchmark harness.
#
# Runs Objeck head-to-head against the fast peer JITs (Java/HotSpot, LuaJIT) on
# the SAME machine in one pass, so the Objeck-vs-peer ratios are directly
# comparable and machine-noise-resistant. Python/Ruby are optional (slow) and
# off by default so this stays fast enough for CI.
#
# Usage:
#   run_native.sh <objeck_bin_dir> <out_csv> [runs] [--with-scripting]
#
#   <objeck_bin_dir>  dir containing obc/obr (e.g. core/release/deploy-x64/bin)
#   <out_csv>         output CSV path (language,benchmark,run,time_seconds)
#   [runs]            runs per benchmark (default 3)
#   --with-scripting  also run Python3 + Ruby (slow; off by default)
#
# Benchmarks + inputs are CI-scaled (each language stays well under ~20s/run).
set -u

BIN_DIR="${1:?objeck bin dir required}"
OUT_CSV="${2:?output csv path required}"
RUNS="${3:-3}"
WITH_SCRIPTING=0
for a in "$@"; do [ "$a" = "--with-scripting" ] && WITH_SCRIPTING=1; done

OBC="$BIN_DIR/obc"
OBR="$BIN_DIR/obr"
# obc/obr resolve the standard .obl libraries via OBJECK_LIB_PATH; the perf-gate
# workflow stages obc/obr + *.obl together in BIN_DIR. Without this, every Objeck
# compile fails ("Unable to read library: lang.obl") and the gate silently has no
# Objeck data to compare. Point it at BIN_DIR (override only if caller didn't set it).
export OBJECK_LIB_PATH="${OBJECK_LIB_PATH:-$BIN_DIR}"
SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_OBJ="$SELF_DIR/../../programs/tests/clbg"   # Objeck CLBG sources
SRC_X="$SELF_DIR/benchmarks"                     # cross-language sources
TMP="$(mktemp -d)"
JOUT="$TMP/java"; mkdir -p "$JOUT"

# benchmark -> CI-scaled input (kept small enough that even LuaJIT fannkuch and
# the Objeck interpreter spectralnorm finish quickly on a CI runner)
declare -A ARG=( [nbody]=50000000 [fannkuchredux]=11 [spectralnorm]=2000 [binarytrees]=15 [mandelbrot]=4000 )
BENCHES="nbody fannkuchredux spectralnorm binarytrees mandelbrot"

echo "language,benchmark,run,time_seconds" > "$OUT_CSV"

echo "============================================="
echo "  Native cross-language benchmark harness"
echo "  Objeck: $($OBC 2>&1 | head -1)"
command -v java  >/dev/null && echo "  Java:   $(java -version 2>&1 | head -1)"
command -v luajit>/dev/null && echo "  LuaJIT: $(luajit -v 2>&1 | head -1)"
echo "  Runs:   $RUNS"
echo "============================================="

# time a command, append median-friendly per-run rows to the CSV
time_runs() { # lang name cmd...
  local lang="$1" name="$2"; shift 2
  echo "=== $lang: $name (${ARG[$name]}) ==="
  for r in $(seq 1 "$RUNS"); do
    local s e ms sec
    s=$(date +%s%N); "$@" >/dev/null 2>&1; e=$(date +%s%N)
    ms=$(( (e - s) / 1000000 )); sec=$(awk "BEGIN{printf \"%.3f\", $ms/1000}")
    echo "  run $r: ${sec}s"
    echo "$lang,$name,$r,$sec" >> "$OUT_CSV"
  done
}

# ---- Objeck (compile once per benchmark, then time obr) ----
for b in $BENCHES; do
  libflag=""; [ "$b" = "binarytrees" ] && libflag="-lib gen_collect"
  "$OBC" -src "$SRC_OBJ/$b.obs" $libflag -opt s3 -dest "$TMP/$b.obe" >/dev/null 2>&1
  if [ -f "$TMP/$b.obe" ]; then time_runs objeck "$b" "$OBR" "$TMP/$b.obe" "${ARG[$b]}"
  else echo "=== objeck: $b SKIP (compile failed) ==="; fi
done

# ---- Java (HotSpot): compile once, time the run (excludes javac) ----
if command -v javac >/dev/null && command -v java >/dev/null; then
  for b in nbody fannkuchredux spectralnorm binarytrees; do
    [ -f "$SRC_X/$b.java" ] && javac -d "$JOUT" "$SRC_X/$b.java" 2>/dev/null
  done
  for b in nbody fannkuchredux spectralnorm binarytrees; do
    [ -f "$JOUT/$b.class" ] && time_runs java "$b" java -cp "$JOUT" "$b" "${ARG[$b]}"
  done
fi

# ---- LuaJIT ----
if command -v luajit >/dev/null; then
  for b in nbody fannkuchredux spectralnorm binarytrees; do
    [ -f "$SRC_X/$b.lua" ] && time_runs luajit "$b" luajit "$SRC_X/$b.lua" "${ARG[$b]}"
  done
fi

# ---- Python / Ruby (optional, slow) ----
if [ "$WITH_SCRIPTING" = "1" ]; then
  command -v python3 >/dev/null && for b in nbody fannkuchredux spectralnorm binarytrees; do
    [ -f "$SRC_X/$b.py" ] && time_runs python3 "$b" python3 "$SRC_X/$b.py" "${ARG[$b]}"
  done
  command -v ruby >/dev/null && for b in nbody fannkuchredux spectralnorm binarytrees; do
    [ -f "$SRC_X/$b.rb" ] && time_runs ruby "$b" ruby "$SRC_X/$b.rb" "${ARG[$b]}"
  done
fi

rm -rf "$TMP"
echo "Wrote $OUT_CSV"
