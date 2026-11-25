#!/usr/bin/env bash
# obl_platform_diff.sh <lib> [<obc> [<outdir>]]
#
# Finds WHERE a library's bytes depend on the platform that compiled it (docs/DESIGN_OBL_FROM_SOURCE.md,
# "the arm64 rebuild differs from the x64 .obl"). Run it on two machines and diff the two output
# directories: the first optimization level whose output differs names the pass to look at.
#
#   <lib>     a name from core/compiler/build_libs.sh, e.g. xml
#   <obc>     the compiler (default core/compiler/obc, run from the repository root)
#   <outdir>  default /tmp/obl_platform_diff/<lib>
#
# For each of -opt s0 s1 s2 s3 it writes <lib>.<level>.obl, the compiler's assembly listing
# (-asm) and an md5 of both, and a SUMMARY. The libraries it depends on are taken from core/lib
# (the same on both machines when they come from the same commit's x64 build).
set -euo pipefail
lib="${1:?usage: obl_platform_diff.sh <lib> [<obc> [<outdir>]]}"
obc="${2:-core/compiler/obc}"
out="${3:-/tmp/obl_platform_diff/$lib}"
line=$(sed -n "s#^\$OBC \(.*-dest \.\./lib/$lib\.obl.*\)\$#\1#p" core/compiler/build_libs.sh | head -n 1)
[ -n "$line" ] || { echo "no $lib line in build_libs.sh" >&2; exit 1; }
mkdir -p "$out"; rm -f "$out"/*
root=$(pwd); obc_abs=$(cd "$(dirname "$obc")" && pwd)/$(basename "$obc"); out_abs=$(cd "$out" && pwd)
export OBJECK_LIB_PATH="$root/core/lib"
: > "$out_abs/SUMMARY"
for lvl in s0 s1 s2 s3; do
  args=$(echo "$line" | sed "s#-opt s[0-3]#-opt $lvl#; s#-dest \.\./lib/$lib\.obl#-dest $out_abs/$lib.$lvl.obl#")
  ( cd core/compiler && "$obc_abs" $args -asm > "$out_abs/$lib.$lvl.log" 2>&1 ) || true
  if [ -f "$out_abs/$lib.$lvl.obl" ]; then
    printf '%s obl %s\n' "$lvl" "$(cksum < "$out_abs/$lib.$lvl.obl")" >> "$out_abs/SUMMARY"
  else
    printf '%s obl MISSING (see %s)\n' "$lvl" "$lib.$lvl.log" >> "$out_abs/SUMMARY"
  fi
done
cat "$out_abs/SUMMARY"
