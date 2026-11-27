#!/usr/bin/env bash
#
# Runs INSIDE a distribution's base image, started by
# tools/packaging/test_portable_linux.sh --natives: /pkg is the unpacked release
# tree (read-only), /fixture holds this script and three regression programs.
#
# smoke.sh proves the tools need nothing. This proves the other half of the
# promise: on this distribution, the tarball's own install_deps.sh finds a
# package for every system library the optional bindings use, and with those
# installed every native library resolves completely and works.

set -uo pipefail
fails=0
ok()  { echo "  ok: $*"; }
bad() { echo "  FAIL: $*"; fails=$((fails + 1)); }
step() { echo; echo "--- $*"; }

. /etc/os-release 2>/dev/null
echo "${PRETTY_NAME:-unknown system}, $(uname -m), $(ldd --version 2>&1 | head -n 1)"

cp -R /pkg /opt/objeck || { echo "FAIL: cannot copy the tree"; exit 1; }
OBJECK=/opt/objeck
export PATH="$OBJECK/bin:$PATH" OBJECK_LIB_PATH="$OBJECK/lib" DEBIAN_FRONTEND=noninteractive
unset LD_LIBRARY_PATH
mkdir -p /tmp/natives && cd /tmp/natives || exit 1

step "1. install_deps.sh installs what the optional bindings use"
bash "$OBJECK/install_deps.sh" --yes > install.log 2>&1; rc=$?
tail -n 12 install.log | sed 's/^/  /'
[ $rc -eq 0 ] && ok "install_deps.sh --yes exited 0" || bad "install_deps.sh --yes exited $rc"
bash "$OBJECK/install_deps.sh" --check > check.log 2>&1; rc=$?
[ $rc -eq 0 ] && ok "install_deps.sh --check: nothing missing" || { cat check.log | sed 's/^/  /'; bad "install_deps.sh --check exited $rc"; }

step "2. every native library resolves, down to the last symbol"
for f in "$OBJECK"/lib/native/libobjk_*.so; do
  out=$(ldd -r "$f" 2>&1 | grep -E "not found|undefined symbol")
  if [ -n "$out" ]; then
    echo "$out" | sed 's/^/    /' | head -n 12
    bad "$(basename "$f") does not resolve on ${PRETTY_NAME:-this system}"
  else
    ok "$(basename "$f")"
  fi
done

step "3. the bindings work"
run() {   # <program> <libs>
  local name="$1" libs="$2" out rc
  cp "/fixture/$name.obs" . || { bad "$name.obs is not in the fixture"; return; }
  if ! obc -src "$name.obs" -lib "cipher,collect,xml,json,$libs" -dest "$name.obe" > "$name.log" 2>&1; then
    tail -n 5 "$name.log"; bad "$name.obs did not compile"; return
  fi
  out=$(timeout 300 obr "$name.obe" 2>&1 </dev/null); rc=$?
  # a test that cannot find its native library prints SKIP and exits 0
  if [ $rc -eq 0 ] && ! echo "$out" | grep -qE "FAIL|SKIP"; then
    ok "$name: $(echo "$out" | tail -n 1)"
  else
    echo "$out" | tail -n 8 | sed 's/^/    /'; bad "$name exited $rc"
  fi
}
run core_opencv opencv
run lame_encode_test lame
run onnx_runtime_test models,onnx,opencv

echo
if [ "$fails" -ne 0 ]; then
  echo "$fails check(s) failed on ${PRETTY_NAME:-this system}"
  exit 1
fi
echo "all native-library checks passed on ${PRETTY_NAME:-this system}"
