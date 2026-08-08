#!/bin/sh
# Offline test for the obu update/rollback flow (POSIX). Builds obu with the
# test hooks enabled and exercises the whole pipeline against fake releases
# under a temp dir -- no network. Exits non-zero on the first failed check so
# it can gate CI.
#
# The hooks (OBU_INSTALL_ROOT / OBU_RELEASE_JSON_FILE / OBU_ASSET_DIR) exist
# ONLY in a build compiled with -DOBU_TEST_HOOKS; the shipped binary ignores
# them, so this harness cannot weaken a real install.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
OBU="$WORK/obu"

CXX=${CXX:-c++}
"$CXX" -std=c++17 -O2 -Wall -Wextra -DOBU_TEST_HOOKS -o "$OBU" "$HERE/obu.cpp"

fail() { echo "FAIL: $1"; exit 1; }

mkfake() { # <root> <version>
  mkdir -p "$1/bin"
  printf '#!/bin/sh\necho obr\n' > "$1/bin/obr"; chmod +x "$1/bin/obr"
  printf '#!/bin/sh\necho "Objeck %s"; exit 0\n' "$2" > "$1/bin/obc"; chmod +x "$1/bin/obc"
  echo "$2" > "$1/VERSION"
}

mkrel() { # <reldir>
  mkdir -p "$1/stage/bin"
  printf '#!/bin/sh\necho obr\n' > "$1/stage/bin/obr"; chmod +x "$1/stage/bin/obr"
  printf '#!/bin/sh\necho "Objeck 9999.1.0"; exit 0\n' > "$1/stage/bin/obc"; chmod +x "$1/stage/bin/obc"
  echo NEW > "$1/stage/VERSION"
  tar -czf "$1/objeck-linux-x64_9999.1.0.tgz" -C "$1/stage" .
  ( cd "$1" && sha256sum objeck-linux-x64_9999.1.0.tgz > SHA256SUMS )
  printf '{ "tag_name":"v9999.1.0", "assets":[ {"name":"objeck-linux-x64_9999.1.0.tgz","browser_download_url":"https://github.com/o/o/a.tgz"}, {"name":"SHA256SUMS","browser_download_url":"https://github.com/o/o/SHA256SUMS"} ] }\n' > "$1/release.json"
}

# This harness names the linux-x64 asset; skip on platforms obu builds a
# different asset name for (it would correctly report "no asset").
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) ;;
  *) echo "SKIP: test fixtures are linux-x64 only"; exit 0 ;;
esac

# --- happy path: verify, swap, obc post-check, then rollback ---
R="$WORK/t1/root"; mkfake "$R" OLD; mkrel "$WORK/t1/rel"
OBU_INSTALL_ROOT="$R" OBU_RELEASE_JSON_FILE="$WORK/t1/rel/release.json" OBU_ASSET_DIR="$WORK/t1/rel" "$OBU" update --quiet || fail "update should succeed"
[ "$(cat "$R/VERSION")" = NEW ] || fail "update did not install the new tree"
OBU_INSTALL_ROOT="$R" "$OBU" rollback --quiet || fail "rollback should succeed"
[ "$(cat "$R/VERSION")" = OLD ] || fail "rollback did not restore the old tree"
[ -d "$R/.previous" ] && fail "rollback left .previous behind"
echo "ok: update + rollback"

# --- tampered checksum: reject, tree untouched, no residue ---
R="$WORK/t2/root"; mkfake "$R" ORIG; mkrel "$WORK/t2/rel"
echo "0000000000000000000000000000000000000000000000000000000000000000  objeck-linux-x64_9999.1.0.tgz" > "$WORK/t2/rel/SHA256SUMS"
if OBU_INSTALL_ROOT="$R" OBU_RELEASE_JSON_FILE="$WORK/t2/rel/release.json" OBU_ASSET_DIR="$WORK/t2/rel" "$OBU" update --quiet; then fail "tampered asset must be rejected"; fi
[ "$(cat "$R/VERSION")" = ORIG ] || fail "tampered update changed the tree"
[ -d "$R/.previous" ] && fail "tampered update left .previous"
[ -d "$R/.obu-work" ] && fail "tampered update left staging"
echo "ok: tampered asset rejected, tree intact"

# --- rollback with no real previous: refuse, install intact ---
R="$WORK/t3/root"; mkfake "$R" SOLO
if OBU_INSTALL_ROOT="$R" "$OBU" rollback --quiet; then fail "rollback with no previous must fail"; fi
[ -f "$R/bin/obc" ] || fail "rollback erased the install"
echo "ok: empty rollback refused"

# --- command injection via a crafted asset name must not execute ---
R="$WORK/t4/root"; mkfake "$R" V; mkdir -p "$WORK/t4/rel/stage/bin" "$WORK/t4/cwd"
EVIL='objeck-linux-x64_$(touch${IFS}PWNED).tgz'
printf '#!/bin/sh\necho x\n' > "$WORK/t4/rel/stage/bin/obr"
printf '#!/bin/sh\necho v;exit 0\n' > "$WORK/t4/rel/stage/bin/obc"; chmod +x "$WORK/t4/rel/stage/bin/"*
tar -czf "$WORK/t4/rel/$EVIL" -C "$WORK/t4/rel/stage" .
( cd "$WORK/t4/rel" && sha256sum "$EVIL" > SHA256SUMS )
printf '{ "tag_name":"v9999.1.0", "assets":[ {"name":"%s","browser_download_url":"https://github.com/o/o/a.tgz"}, {"name":"SHA256SUMS","browser_download_url":"https://github.com/o/o/s"} ] }\n' "$EVIL" > "$WORK/t4/rel/release.json"
( cd "$WORK/t4/cwd"
  OBU_INSTALL_ROOT="$R" OBU_RELEASE_JSON_FILE="$WORK/t4/rel/release.json" OBU_ASSET_DIR="$WORK/t4/rel" "$OBU" update --quiet || true )
[ -f "$WORK/t4/cwd/PWNED" ] && fail "COMMAND INJECTION: crafted asset name executed"
echo "ok: injection blocked"

echo "ALL obu update tests passed"
