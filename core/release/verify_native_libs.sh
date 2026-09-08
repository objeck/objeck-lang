#!/bin/sh
# Fail a POSIX deploy if a native library the tree is supposed to ship is
# missing or cannot resolve its dependencies. The Windows deploy has had this
# check (verify_native_libs.ps1) since the obu incident; the four POSIX deploy
# scripts had none, so a library whose build failed was silently dropped from
# the release with a green exit.
#
# usage: verify_native_libs.sh <native_dir> <ext> <name>...
#   e.g. verify_native_libs.sh deploy/lib/native so odbc crypto lame ml opencv onnx sdl diags
set -u
dir="$1"
ext="$2"
shift 2

failures=0
checked=0
for name in "$@"; do
  lib="$dir/libobjk_$name.$ext"
  if [ ! -f "$lib" ]; then
    echo "ERROR: missing native library: $lib"
    failures=$((failures + 1))
    continue
  fi
  checked=$((checked + 1))
  # Linux: an unresolved dependency shows as "not found". macOS's otool does
  # not report absence, and the macOS deploy has its own install_name checks.
  if [ "$ext" = "so" ] && command -v ldd >/dev/null 2>&1; then
    if ldd "$lib" 2>/dev/null | grep -q "not found"; then
      echo "ERROR: unresolved dependency in $lib:"
      ldd "$lib" | grep "not found" | sed 's/^/    /'
      failures=$((failures + 1))
    fi
  fi
done

if [ "$failures" -ne 0 ]; then
  echo "============================================================"
  echo " ERROR: $failures native-library verification failure(s)"
  echo "============================================================"
  exit 1
fi
echo "native libraries verified: $checked in $dir"
