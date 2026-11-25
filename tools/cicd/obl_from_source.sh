#!/usr/bin/env bash
# obl_from_source.sh build|compare
#
# Used by .github/workflows/libs.yml (docs/DESIGN_OBL_FROM_SOURCE.md): build every
# core/lib/*.obl from source, then check the result against the set git carries.
#
#   build    save the committed set to $OBL_KEEP (default /tmp/committed_obl), delete every
#            core/lib/*.obl, then run core/compiler/update_version.sh: sys_obc -> lang.obl ->
#            obc -> every library in build_libs.sh. The library list is read from
#            build_libs.sh and nothing else, so a library added to it needs no edit here.
#   compare  byte-compare the rebuilt core/lib/*.obl with the saved set and exit 1 on any
#            difference, any committed library the build did not produce, or any built
#            library that is not committed. With nothing committed it is a no-op (the
#            migration's last stage), because then there is nothing to compare with.
#
# Run from the repository root. build is destructive to core/lib/*.obl: CI checkouts only.
set -euo pipefail

KEEP="${OBL_KEEP:-/tmp/committed_obl}"
cmd="${1:-}"

case "$cmd" in
  build)
    rm -rf "$KEEP"; mkdir -p "$KEEP"
    if ls core/lib/*.obl >/dev/null 2>&1; then cp core/lib/*.obl "$KEEP"/; fi
    echo "saved $(ls "$KEEP" | wc -l) committed libraries to $KEEP"

    rm -f core/lib/*.obl
    echo "core/lib has no .obl: building from source"

    ( cd core/compiler && ./update_version.sh )

    echo "built $(ls core/lib/*.obl | wc -l) libraries"
    # lang.obl plus one per build_libs.sh line: a silent short build is a failure.
    want=$(( $(grep -c '^\$OBC ' core/compiler/build_libs.sh) + 1 ))
    [ "$(ls core/lib/*.obl | wc -l)" -ge "$want" ] || { echo "ERROR: build_libs.sh lists $((want-1)) libraries (+ lang), fewer were built"; exit 1; }
    ;;

  compare)
    if [ -z "$(ls "$KEEP" 2>/dev/null)" ]; then echo "no committed libraries to compare with"; exit 0; fi
    bad=0; same=0
    for f in "$KEEP"/*.obl; do
      n=$(basename "$f")
      if [ ! -f "core/lib/$n" ]; then echo "ERROR: committed but not built: $n"; bad=1
      elif cmp -s "$f" "core/lib/$n"; then same=$((same+1))
      else echo "ERROR: built $n differs from the committed copy ($(wc -c < "$f") vs $(wc -c < "core/lib/$n") bytes): rebuild it (core/compiler/update_version.sh) and commit"; bad=1; fi
    done
    for f in core/lib/*.obl; do
      n=$(basename "$f")
      [ -f "$KEEP/$n" ] || { echo "ERROR: built but not committed: $n (commit core/lib/$n)"; bad=1; }
    done
    echo "rebuilt == committed: $same of $(ls "$KEEP" | wc -l)"
    [ "$bad" = 0 ] || exit 1
    ;;

  *)
    echo "usage: $0 build|compare" >&2
    exit 2
    ;;
esac
