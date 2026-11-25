#!/bin/sh
# require_obl.sh [<lib dir>]
#
# Fails, with the way out, unless every standard library is present in <lib dir>
# (default core/lib): lang.obl plus every library core/compiler/build_libs.sh builds.
# The deploy scripts run it before they copy core/lib/*.obl into a deploy tree.
#
# Why it exists: the .obl are build products, produced from core/compiler/lib_src
# (docs/DESIGN_OBL_FROM_SOURCE.md). A deploy that copies "whatever .obl are there"
# ships a tree that cannot link, and `cp ../lib/*.obl` with no match is only a
# warning. The list is READ from build_libs.sh, so a library added there is
# required here without an edit.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DIR="${1:-$ROOT/core/lib}"

missing=""
[ -f "$DIR/lang.obl" ] || missing="$missing lang"
for lib in $(sed -n 's#.*-dest \.\./lib/\([A-Za-z0-9_]*\)\.obl.*#\1#p' "$ROOT/core/compiler/build_libs.sh"); do
  [ -f "$DIR/$lib.obl" ] || missing="$missing $lib"
done

[ -z "$missing" ] && exit 0

echo "ERROR: $DIR is missing compiled libraries:$missing" >&2
echo "" >&2
echo "  The .obl files are build products of core/compiler/lib_src. Make them with" >&2
echo "    Linux/macOS:  cd core/compiler && ./update_version.sh          (arm64: ./update_version.sh arm64)" >&2
echo "    Windows:      core\\compiler\\build_libs_windows.cmd             (needs Git for Windows' sh)" >&2
echo "  or download the 'libs' artifact of a CI run into core/lib/ (CI jobs do this)." >&2
exit 1
