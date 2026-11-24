#!/usr/bin/env bash
# obl_from_source.sh build [--in-place] | compare
#
# Used by .github/workflows/libs.yml (docs/DESIGN_OBL_FROM_SOURCE.md): build every
# core/lib/*.obl from source, then check the result against the set git carries.
#
#   build    run core/compiler/update_version.sh (sys_obc -> lang.obl -> obc -> every library
#            in build_libs.sh) for THIS machine's architecture (x86_64 -> amd64, aarch64/arm64
#            -> arm64). The library list is read from build_libs.sh, nothing else.
#            Prerequisites are checked FIRST, before anything is touched.
#
#            Default is NON-DESTRUCTIVE: core/compiler, core/shared and core/lib's .ini are
#            copied to $OBL_WORK (default a new temp dir) and built there; the checkout's
#            core/lib/*.obl are never written or deleted.
#            --in-place (what CI's libs job uses, on a throwaway checkout): delete
#            core/lib/*.obl, build in the tree, so nothing can quietly reach for a committed
#            .obl. The committed set is saved to $OBL_KEEP first for `compare`.
#   compare  byte-compare the rebuilt libraries with the committed ones and exit 1 on any
#            difference, any committed library the build did not produce, or any built
#            library that is not committed. With nothing committed it is a no-op.
#
# Run from the repository root.
set -euo pipefail

KEEP="${OBL_KEEP:-/tmp/committed_obl}"
STATE="${OBL_STATE:-/tmp/obl_from_source.state}"   # remembers where `build` put its output
cmd="${1:-}"; shift || true
inplace=0
for a in "$@"; do case "$a" in --in-place) inplace=1 ;; *) echo "unknown option $a" >&2; exit 2 ;; esac; done

arch_arg() {
  case "$(uname -m)" in
    x86_64|amd64) echo amd64 ;;
    aarch64|arm64) echo arm64 ;;
    *) echo "ERROR: unsupported machine $(uname -m)" >&2; return 1 ;;
  esac
}

preflight() {
  [ -f core/compiler/update_version.sh ] || { echo "ERROR: run from the repository root"; exit 1; }
  for t in make sh; do command -v "$t" >/dev/null || { echo "ERROR: $t not found"; exit 1; }; done
  command -v "${CXX:-c++}" >/dev/null || command -v g++ >/dev/null || command -v clang++ >/dev/null \
    || { echo "ERROR: no C++ compiler"; exit 1; }
  # zlib is the only library obc links
  printf '#include <zlib.h>\nint main(){return 0;}\n' | "${CXX:-c++}" -x c++ - -lz -o /dev/null 2>/dev/null \
    || { echo "ERROR: zlib development files are missing (apt install zlib1g-dev / brew install zlib)"; exit 1; }
  arch_arg >/dev/null || exit 1
}

case "$cmd" in
  build)
    preflight
    ARCH=$(arch_arg)
    lib_count() { ls "$1"/*.obl 2>/dev/null | wc -l; }
    want=$(( $(grep -c '^\$OBC ' core/compiler/build_libs.sh) + 1 ))   # lang.obl plus one per line
    if [ "$inplace" = 1 ]; then
      rm -rf "$KEEP"; mkdir -p "$KEEP"
      if ls core/lib/*.obl >/dev/null 2>&1; then cp core/lib/*.obl "$KEEP"/; fi
      echo "saved $(ls "$KEEP" | wc -l) committed libraries to $KEEP"
      rm -f core/lib/*.obl
      echo "core/lib has no .obl: building from source ($ARCH)"
      ( cd core/compiler && ./update_version.sh "$ARCH" )
      OUT=core/lib
    else
      WORK="${OBL_WORK:-$(mktemp -d)}"
      mkdir -p "$WORK/core/lib" "$WORK/core/vm"
      cp -R core/compiler core/shared "$WORK/core/"
      cp core/lib/*.ini "$WORK/core/lib/" 2>/dev/null || true
      # a prior in-tree build's products must not leak into the copy
      rm -f "$WORK"/core/lib/*.obl
      echo "building in $WORK (the checkout's core/lib is not touched) for $ARCH"
      ( cd "$WORK/core/compiler" && ./update_version.sh "$ARCH" )
      OUT="$WORK/core/lib"
      rm -rf "$KEEP"; mkdir -p "$KEEP"
      if ls core/lib/*.obl >/dev/null 2>&1; then cp core/lib/*.obl "$KEEP"/; fi
    fi
    echo "$OUT" > "$STATE"
    echo "built $(lib_count "$OUT") libraries in $OUT"
    [ "$(lib_count "$OUT")" -ge "$want" ] || { echo "ERROR: build_libs.sh lists $((want-1)) libraries (+ lang), fewer were built"; exit 1; }
    ;;

  compare)
    OUT=$(cat "$STATE" 2>/dev/null || echo core/lib)
    if [ -z "$(ls "$KEEP" 2>/dev/null)" ]; then echo "no committed libraries to compare with"; exit 0; fi
    bad=0; same=0
    for f in "$KEEP"/*.obl; do
      n=$(basename "$f")
      if [ ! -f "$OUT/$n" ]; then echo "ERROR: committed but not built: $n"; bad=1
      elif cmp -s "$f" "$OUT/$n"; then same=$((same+1))
      else echo "ERROR: built $n differs from the committed copy ($(wc -c < "$f") vs $(wc -c < "$OUT/$n") bytes): rebuild it (core/compiler/update_version.sh) and commit"; bad=1; fi
    done
    for f in "$OUT"/*.obl; do
      n=$(basename "$f")
      [ -f "$KEEP/$n" ] || { echo "ERROR: built but not committed: $n (commit core/lib/$n)"; bad=1; }
    done
    echo "rebuilt == committed: $same of $(ls "$KEEP" | wc -l)"
    [ "$bad" = 0 ] || exit 1
    ;;

  *)
    echo "usage: $0 build [--in-place] | compare" >&2
    exit 2
    ;;
esac
