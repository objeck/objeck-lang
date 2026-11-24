#!/bin/bash
# Stress the write barrier's dirty-list overflow path (POSIX, AMD64 or ARM64 Linux/macOS).
#
# Usage: dirty_list_overflow.sh <deploy-dir> [objeck-deps-prefix]
#
#   <deploy-dir>  a built tree with bin/obc, bin/obr and lib/ (core/release/deploy)
#   OBJECK_DEPS   the prefix core/vm/make/Makefile.amd64 links QUIC from; the argument,
#                 or the OBJECK_DEPS variable, or the makefile's own default
#
# The write barrier records old objects that gain young references in a list of
# DIRTY_LIST_MAX (65536) entries; past that a minor collection scans all of the old
# generation. Programs rarely dirty 65536 objects between collections, so that path
# runs almost never. This builds a second VM with -DOBJECK_DIRTY_LIST_MAX=64, where
# every collection overflows, runs dirty_list_overflow.obs on both it and the
# deployed VM (interpreted and compiled, small nursery), and requires the same
# checksum and PASS from each. The variant is built from a COPY of core/vm in a
# scratch directory, from scratch: the sub-makefiles do not rebuild for a header
# define, and one object built with the other limit would corrupt the list.
#
# Not part of run_regression.sh: the build takes minutes. Run it after changing
# memory.cpp's write barrier, dirty list or minor collector.
set -u

DEPLOY="${1:?usage: dirty_list_overflow.sh <deploy-dir> [objeck-deps-prefix]}"
DEPS="${2:-${OBJECK_DEPS:-}}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
DEPLOY="$(cd "$DEPLOY" && pwd)"
OBC="$DEPLOY/bin/obc"
NORMAL_OBR="$DEPLOY/bin/obr"
[ -x "$OBC" ] && [ -x "$NORMAL_OBR" ] || { echo "no bin/obc and bin/obr under $DEPLOY"; exit 2; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# --- build the variant VM from a copy of the sources -----------------------------
mkdir -p "$WORK/src/core" "$WORK/variant/bin"
cp -R "$ROOT/core/vm" "$WORK/src/core/vm"
cp -R "$ROOT/core/shared" "$WORK/src/core/shared"
MAKE_ARGS=()
[ -n "$DEPS" ] && MAKE_ARGS+=("OBJECK_DEPS=$DEPS")
case "$(uname -m)" in
  arm64|aarch64) MAKEFILE=make/Makefile.arm64 ;;
  *)             MAKEFILE=make/Makefile.amd64 ;;
esac
echo "building the variant VM (OBJECK_DIRTY_LIST_MAX=64) ..."
( cd "$WORK/src/core/vm" \
  && make -f "$MAKEFILE" clean >/dev/null 2>&1; \
  make -f "$MAKEFILE" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" \
       "EXTRA_FLAGS=-DOBJECK_DIRTY_LIST_MAX=64" "${MAKE_ARGS[@]}" >"$WORK/build.log" 2>&1 ) \
  || { echo "variant build failed:"; tail -n 20 "$WORK/build.log"; exit 2; }
cp "$WORK/src/core/vm/obr" "$WORK/variant/bin/obr"
ln -s "$DEPLOY/lib" "$WORK/variant/lib"
VARIANT_OBR="$WORK/variant/bin/obr"

# --- compile the workload and compare --------------------------------------------
"$OBC" -src "$HERE/dirty_list_overflow.obs" -dest "$WORK/dirty.obe" >/dev/null 2>&1 \
  || { echo "dirty_list_overflow.obs does not compile"; exit 2; }

status=0
for jit in --jit=off --jit=1; do
  normal="$("$NORMAL_OBR" "$jit" --nursery=256k "$WORK/dirty.obe" 2>&1)"; normal_rc=$?
  variant="$("$VARIANT_OBR" "$jit" --nursery=256k "$WORK/dirty.obe" 2>&1)"; variant_rc=$?
  if [ $normal_rc -ne 0 ] || ! grep -q '^PASS' <<<"$normal"; then
    echo "[FAIL] $jit: the normal VM fails the workload (rc=$normal_rc): $normal"; status=1; continue
  fi
  if [ $variant_rc -ne 0 ] || ! grep -q '^PASS' <<<"$variant"; then
    echo "[FAIL] $jit: the 64-entry-list VM fails the workload (rc=$variant_rc): $variant"; status=1; continue
  fi
  if [ "$normal" != "$variant" ]; then
    echo "[FAIL] $jit: the two VMs print different results"; echo "  normal : $normal"; echo "  variant: $variant"; status=1; continue
  fi
  echo "[PASS] $jit: both VMs agree ($(grep '^checksum' <<<"$variant"))"
done
exit $status
