#!/usr/bin/env bash
#
# test_deb.sh <objeck-ubuntu24.04-ARCH_VERSION.deb>
#
# Install a .deb on a clean Ubuntu 24.04 the way a user would, and prove it works:
# a container with nothing but the base image, so a dependency the package forgot
# cannot be supplied by the machine running the test. Needs docker.
#
#   1. --no-install-recommends: the core toolchain compiles and runs a program,
#      with OBJECK_LIB_PATH unset and from a directory that is not the install.
#   2. obu refuses 'update' (apt owns the files) and still answers 'version'.
#   3. with Recommends: every optional native module loads -- each one's
#      libraries resolve, which is the whole job of the Recommends line.
#   4. apt remove leaves nothing under /usr/lib/objeck or /usr/bin.

set -euo pipefail
DEB="${1:?usage: test_deb.sh <objeck-ubuntu24.04-ARCH_VERSION.deb>}"
[ -f "$DEB" ] || { echo "test_deb.sh: no such file: $DEB" >&2; exit 1; }
# pwd -W: from Git Bash, Docker Desktop needs C:/... rather than /c/...
DIR="$(cd "$(dirname "$DEB")" && { pwd -W 2>/dev/null || pwd; })"; NAME="$(basename "$DEB")"
export MSYS_NO_PATHCONV=1

docker run --rm -v "$DIR:/pkg:ro" -e NAME="$NAME" ubuntu:24.04 bash -euo pipefail -c '
export DEBIAN_FRONTEND=noninteractive
# A mirror caught mid-sync fails a download with "Hash Sum mismatch"; that is the
# archive, not the package, so it gets a fresh index and another try.
apt_install() {
  for try in 1 2 3; do
    apt-get install -y -qq "$@" >/dev/null && return 0
    echo "apt-get install failed (try $try); refreshing the index"; sleep 10; apt-get update -qq
  done
  return 1
}
apt-get update -qq

echo "=== 1. install without Recommends"
apt_install --no-install-recommends "/pkg/$NAME"
unset OBJECK_LIB_PATH
mkdir -p /tmp/t && cd /tmp/t
cat > hello.obs <<EOF
class Hello {
  function : Main(args : String[]) ~ Nil {
    names := ["deb", "apt"];
    each(i : names) {
      ("Hello from " + names[i])->PrintLine();
    };
  }
}
EOF
obc -src hello.obs -dest hello.obe >/dev/null
out="$(obr hello.obe | tr "\n" " ")"
echo "$out"
[ "$out" = "Hello from deb Hello from apt " ] || { echo "FAIL: wrong output"; exit 1; }
echo "ok: compiled and ran outside the install, OBJECK_LIB_PATH unset"

echo "=== 2. obu under apt"
if obu update >/tmp/obu.txt 2>&1; then echo "FAIL: obu update was allowed"; exit 1; fi
grep -q "installed by apt" /tmp/obu.txt || { cat /tmp/obu.txt; echo "FAIL: obu update refused without saying why"; exit 1; }
obu version >/dev/null || { echo "FAIL: obu version"; exit 1; }
echo "ok: obu update refused, obu version works"

echo "=== 3. Recommends make every native module loadable"
# Removed first: installing the same version again is a no-op for apt, and would
# not bring in the Recommends this step is about.
apt-get remove -y -qq objeck >/dev/null
apt_install "/pkg/$NAME"
missing=0
for so in /usr/lib/objeck/lib/native/libobjk_*.so; do
  if LD_LIBRARY_PATH=/usr/lib/objeck/lib/native ldd "$so" | grep -q "not found"; then
    echo "FAIL: $(basename "$so") cannot resolve:"; ldd "$so" | grep "not found"; missing=1
  fi
done
[ "$missing" = 0 ] || exit 1
echo "ok: all $(ls /usr/lib/objeck/lib/native/libobjk_*.so | wc -l) native modules resolve"

echo "=== 4. remove"
apt-get remove -y -qq objeck >/dev/null
left="$(ls -A /usr/lib/objeck 2>/dev/null; ls /usr/bin/obc /usr/bin/obr /usr/bin/obu 2>/dev/null || true)"
[ -z "$left" ] || { echo "FAIL: left behind: $left"; exit 1; }
echo "ok: removed cleanly"
echo "PASS $NAME"
'
