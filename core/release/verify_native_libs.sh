#!/bin/sh
# Fail a POSIX deploy if a native library the tree is supposed to ship is
# missing or cannot resolve its dependencies. The Windows deploy has had this
# check (verify_native_libs.ps1) since the obu incident; the four POSIX deploy
# scripts had none, so a library whose build failed was silently dropped from
# the release with a green exit.
#
# usage: verify_native_libs.sh <native_dir> <ext> <required>... [--optional <name>...]
#   e.g. verify_native_libs.sh deploy/lib/native so crypto diags lame ml odbc opencv sdl --optional onnx
#
# Exits 1 on a verification failure (2 on a usage error), and the CALLER has to
# act on it with '|| exit 1': no deploy script uses 'set -e'. For the first
# release that ran this check none did -- v2026.9.1 printed "native-library
# verification failure(s)" on linux-x64, linux-arm64 and macos-arm64, and every
# one of those jobs went green and shipped.
#
#   required  missing, or (Linux) present with an unresolved dependency: failure
#   optional  missing: a warning, plus a ::warning:: annotation under GitHub
#             Actions so it reaches the run summary instead of the scrollback.
#             Present: checked exactly like a required library -- whatever the
#             tree ships has to load, however optional it was to ship it.
# A library named in neither list is not checked at all, so a new native library
# has to be added to its caller's list.
#
# What each platform requires (the deploy scripts pass these lists):
#
#   platform     required                                   optional
#   linux-x64    crypto diags lame ml odbc onnx opencv sdl  -
#   linux-arm64  crypto diags lame ml odbc opencv sdl       onnx
#   macos-arm64  crypto diags lame ml odbc onnx opencv sdl  -
#
#   onnx is optional on linux-arm64 because the only vendored ONNX Runtime is
#   x86-64 (core/lib/onnx/eq/cuda/lib/x64) and Ubuntu 24.04 packages none, so
#   there is nothing to link against. Vendor an aarch64 runtime under
#   cuda/lib/arm64/lib -- build.sh and deploy_posix.sh pick it up from there --
#   then move onnx to the required list.
#
#   The two MSYS2 deploys pass all eight as required but still ignore the exit
#   status; no CI leg runs them to hold them to it.
set -u

usage() {
  echo "usage: verify_native_libs.sh <native_dir> <ext> <required>... [--optional <name>...]" >&2
  exit 2
}

[ $# -ge 3 ] || usage
dir="$1"
ext="$2"
shift 2

required=""
optional=""
list=required
for arg in "$@"; do
  case "$arg" in
    --optional)
      list=optional
      ;;
    -*)
      echo "verify_native_libs.sh: unknown option: $arg" >&2
      usage
      ;;
    *)
      if [ "$list" = required ]; then
        required="$required $arg"
      else
        case " $required " in
          *" $arg "*)
            echo "verify_native_libs.sh: $arg is both required and optional" >&2
            exit 2
            ;;
        esac
        optional="$optional $arg"
      fi
      ;;
  esac
done

# annotate <error|warning> <message>: surface a line on the GitHub Actions run
annotate() {
  if [ "${GITHUB_ACTIONS:-}" = "true" ]; then
    echo "::$1::$2"
  fi
}

failures=0
checked=0
not_built=""

# check <name> <required|optional>
check() {
  lib="$dir/libobjk_$1.$ext"
  if [ ! -f "$lib" ]; then
    if [ "$2" = optional ]; then
      echo "WARNING: optional native library not built: $lib"
      annotate warning "optional native library not built: libobjk_$1.$ext"
      not_built="$not_built $1"
    else
      echo "ERROR: missing native library: $lib"
      annotate error "missing native library: libobjk_$1.$ext"
      failures=$((failures + 1))
    fi
    return
  fi
  checked=$((checked + 1))
  # Linux: an unresolved dependency shows as "not found", and so does one that
  # resolves to the wrong build ("version `VERS_x' not found", on stderr).
  # macOS's otool does not report absence, and the macOS deploy has its own
  # install_name checks.
  if [ "$ext" = "so" ] && command -v ldd >/dev/null 2>&1; then
    unresolved=$(ldd "$lib" 2>&1 | grep "not found")
    if [ -n "$unresolved" ]; then
      echo "ERROR: unresolved dependency in $lib:"
      echo "$unresolved" | sed 's/^[[:space:]]*/    /'
      annotate error "unresolved dependency in libobjk_$1.$ext"
      failures=$((failures + 1))
    fi
  fi
}

for name in $required; do
  check "$name" required
done
for name in $optional; do
  check "$name" optional
done

if [ "$failures" -ne 0 ]; then
  echo "============================================================"
  echo " ERROR: $failures native-library verification failure(s)"
  echo "============================================================"
  exit 1
fi
if [ -n "$not_built" ]; then
  echo "native libraries verified: $checked in $dir (optional, not built:$not_built)"
else
  echo "native libraries verified: $checked in $dir"
fi
