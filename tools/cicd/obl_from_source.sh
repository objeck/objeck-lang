#!/usr/bin/env bash
# obl_from_source.sh build|compare
#
# The experiment behind .github/workflows/obl-from-source.yml and
# docs/DESIGN_OBL_FROM_SOURCE.md: can a checkout with NO committed core/lib/*.obl build
# them all from source?
#
#   build    save the committed set to $OBL_KEEP (default /tmp/committed_obl), delete every
#            core/lib/*.obl, then run what ci-build.yml's linux-x64 leg runs:
#            core/compiler/update_version.sh (sys_obc -> lang.obl -> obc -> every library in
#            build_libs.sh order) and core/release/deploy_posix.sh x64.
#   compare  byte-compare the rebuilt core/lib/*.obl with the saved set.
#
# Run from the repository root. build is destructive to core/lib/*.obl: use a throwaway checkout.
set -euo pipefail

KEEP="${OBL_KEEP:-/tmp/committed_obl}"
cmd="${1:-}"

case "$cmd" in
  build)
    ls core/lib/*.obl >/dev/null
    rm -rf "$KEEP"; mkdir -p "$KEEP"
    cp core/lib/*.obl "$KEEP"/
    echo "saved $(ls "$KEEP" | wc -l) committed libraries to $KEEP"

    rm -f core/lib/*.obl
    if ls core/lib/*.obl >/dev/null 2>&1; then echo "ERROR: .obl files survived the delete"; exit 1; fi
    echo "core/lib has no .obl: building from source"

    ( cd core/compiler && ./update_version.sh )

    built=$(ls core/lib/*.obl | wc -l)
    want=$(( $(ls "$KEEP" | wc -l) ))
    echo "built $built libraries (the committed set had $want)"
    [ "$built" -ge "$want" ] || { echo "ERROR: fewer libraries built than were committed"; exit 1; }

    # Every library in the committed set must exist in the rebuilt one, by name.
    missing=0
    for f in "$KEEP"/*.obl; do
      [ -f "core/lib/$(basename "$f")" ] || { echo "ERROR: not rebuilt: $(basename "$f")"; missing=1; }
    done
    [ "$missing" = 0 ]

    ( cd core/release && ./deploy_posix.sh x64 )
    ;;

  compare)
    same=0; differ=""
    for f in "$KEEP"/*.obl; do
      n=$(basename "$f")
      if cmp -s "$f" "core/lib/$n"; then same=$((same+1)); else differ="$differ $n"; fi
    done
    echo "rebuilt == committed: $same of $(ls "$KEEP" | wc -l)"
    if [ -n "$differ" ]; then echo "rebuilt differs from committed:$differ"; else echo "every rebuilt library is byte-identical to the committed one"; fi
    ;;

  *)
    echo "usage: $0 build|compare" >&2
    exit 2
    ;;
esac
