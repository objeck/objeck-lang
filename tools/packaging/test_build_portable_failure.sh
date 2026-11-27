#!/usr/bin/env bash
#
# test_build_portable_failure.sh [--deps <dir>] [--out <scratch dir>]
#
# A build of build_portable_linux.sh whose native-library check is made to fail
# must leave --out as it found it:
#
#   * the last good archive (objeck-linux-<arch>_0.0.0.tgz) is still there, byte for byte;
#   * an archive of another architecture, owned by another builder, is untouched and
#     keeps its owner, though the container is told to chown to a different user;
#   * no staging file is left behind.
#
# The container half of the script is replaced, through a Docker shim, by a copy whose
# unresolved-symbol check cannot pass; nothing else is changed, so it is the real
# staging, trap and publish code that runs. Needs docker and a real build (the
# libraries in --deps are kept between runs, so only the first is slow). It chowns the
# checkout it builds in to a stranger, so it works in a throwaway copy and removes it
# with the container's root. The mirror of this (a good build replaces the archive)
# is build_portable_linux.sh itself.
#
# DOCKER="sudo docker" where docker needs root. Exit 0 only if every check holds.

set -euo pipefail
die() { echo "test_build_portable_failure.sh: $*" >&2; exit 1; }

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
DEPS="$HOME/objeck-deps/portable-linux-$(uname -m)"
OUT=""
while [ $# -gt 0 ]; do
  case "$1" in
    --deps) shift; DEPS="${1:?--deps needs a directory}" ;;
    --out) shift; OUT="${1:?--out needs a directory}" ;;
    *) die "unknown argument $1" ;;
  esac
  shift
done
read -r -a DOCKER <<< "${DOCKER:-docker}"
case "$(uname -m)" in x86_64) ARCH=x64; OTHER=arm64 ;; aarch64) ARCH=arm64; OTHER=x64 ;; *) die "unsupported machine" ;; esac

WORK="$(mktemp -d)"
cleanup() {
  "${DOCKER[@]}" run --rm -v "$WORK:/w" almalinux:8 rm -rf /w/src >/dev/null 2>&1 || true
  rm -rf "$WORK" 2>/dev/null || true
}
trap cleanup EXIT
mkdir -p "$WORK/src" "$DEPS"
( cd "$REPO" && git ls-files -z | xargs -0 tar cf - ) | tar xf - -C "$WORK/src"
[ -n "$OUT" ] || OUT="$WORK/out"
mkdir -p "$OUT"

# the build script with an unresolved-symbol check that always finds one
sed 's/\[ \$unresolved -eq 0 \] || die/[ $unresolved -eq 99 ] || die/' \
  "$WORK/src/tools/packaging/build_portable_linux.sh" > "$WORK/broken.sh"
cmp -s "$WORK/broken.sh" "$WORK/src/tools/packaging/build_portable_linux.sh" && die "the sed found nothing to break"

# `run` gets the broken script mounted over the real one and a host user that is not us
# (the script's own -e HOST_UID=... is rewritten in place, since a later -e would lose to it)
cat > "$WORK/shim.sh" <<'SHIM'
#!/bin/bash
read -r -a real <<< "$REAL_DOCKER"
if [ "$1" = run ]; then
  shift
  args=()
  for a in "$@"; do
    case "$a" in HOST_UID=*) a=HOST_UID=54321 ;; HOST_GID=*) a=HOST_GID=54321 ;; esac
    args+=("$a")
  done
  exec "${real[@]}" run -v "$BROKEN:/src/tools/packaging/build_portable_linux.sh:ro" "${args[@]}"
fi
exec "${real[@]}" "$@"
SHIM
chmod +x "$WORK/shim.sh"
export REAL_DOCKER="${DOCKER[*]}" BROKEN="$WORK/broken.sh"

echo good > "$OUT/objeck-linux-${ARCH}_0.0.0.tgz"
echo neighbour > "$OUT/objeck-linux-${OTHER}_0.0.0.tgz"
echo keep > "$OUT/objeck-backups.txt"
good_sum="$(sha256sum "$OUT/objeck-linux-${ARCH}_0.0.0.tgz")"
owner_before="$(stat -c %u:%g "$OUT"/objeck-linux-*_0.0.0.tgz | tr '\n' ' ')"

set +e
DOCKER="$WORK/shim.sh" bash "$WORK/src/tools/packaging/build_portable_linux.sh" --deps "$DEPS" --out "$OUT" --no-gate > "$WORK/build.log" 2>&1
rc=$?
set -e
tail -n 5 "$WORK/build.log"

fails=0
check() { if "$@"; then echo "  ok: $DESC"; else echo "  FAIL: $DESC"; fails=$((fails + 1)); fi; }
DESC="the build failed (exit $rc)"; check [ "$rc" -ne 0 ]
DESC="the failure is the unresolved-symbol check"; check grep -q "a native library has unresolved symbols" "$WORK/build.log"
DESC="the last good archive is unchanged"; check [ "$(sha256sum "$OUT/objeck-linux-${ARCH}_0.0.0.tgz")" = "$good_sum" ]
DESC="no archive was owned by another uid"; check [ "$(stat -c %u:%g "$OUT"/objeck-linux-*_0.0.0.tgz | tr '\n' ' ')" = "$owner_before" ]
DESC="the other builder's archive is untouched"; check [ "$(cat "$OUT/objeck-linux-${OTHER}_0.0.0.tgz")" = neighbour ]
DESC="no staging file is left"; check [ -z "$(ls -A "$OUT" | grep '^\.staging-' || true)" ]
DESC="the other files are untouched"; check [ "$(cat "$OUT/objeck-backups.txt")" = keep ]
[ $fails -eq 0 ] || { echo "$fails check(s) failed"; exit 1; }
echo "PASS: a failed build left $OUT as it found it"
