#!/bin/bash
#
# Tests for apt-get-retry.sh and apt_harden.sh against a fake apt-get, so the retry logic is
# proven without a network and without touching the machine's real apt. Run:
#
#     bash tools/cicd/test_apt_harden.sh
#
# Exit 0 = every case passed.

HERE="$(cd "$(dirname "$0")" && pwd)"

# the helpers resolve names with getent, which Git Bash on Windows does not have; every Linux runner does
command -v getent >/dev/null 2>&1 || { echo "getent is not available here; skipping (runs on Linux)"; exit 0; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
pass=0; fail=0

ok()  { pass=$((pass + 1)); echo "  ok    $1"; }
bad() { fail=$((fail + 1)); echo "  FAIL  $1${2:+ ($2)}"; }
check() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1" "expected '$3', got '$2'"; fi; }

# a fake apt-get: fails the first FAKE_FAILS calls with FAKE_MSG and exit code FAKE_RC, then succeeds
FAKE="$WORK/fake-apt-get"
cat > "$FAKE" <<'EOF'
#!/bin/bash
n=$(cat "$FAKE_COUNT" 2>/dev/null || echo 0)
n=$((n + 1))
echo "$n" > "$FAKE_COUNT"
echo "fake apt-get $*"
if [ "$n" -le "${FAKE_FAILS:-0}" ]; then
	echo "$FAKE_MSG"
	exit "${FAKE_RC:-100}"
fi
echo "done"
exit 0
EOF
chmod +x "$FAKE"

run() {  # run <fails> <rc> <message> <args...>  -> sets RC, CALLS, OUT
	local fails="$1" rc="$2" msg="$3"; shift 3
	export FAKE_COUNT="$WORK/count"; rm -f "$FAKE_COUNT"
	export FAKE_FAILS="$fails" FAKE_RC="$rc" FAKE_MSG="$msg"
	OUT="$(APT_REAL="$FAKE" APT_RETRY_TRIES=4 APT_RETRY_DELAY=0 bash "$HERE/apt-get-retry.sh" "$@" 2>&1)"
	RC=$?
	CALLS=$(cat "$FAKE_COUNT" 2>/dev/null || echo 0)
}

echo "apt-get-retry.sh"

run 0 0 "" install -y foo
check "a good install runs once and succeeds" "$RC/$CALLS" "0/1"

run 3 100 "E: Failed to fetch http://azure.archive.ubuntu.com/x  Temporary failure resolving 'azure.archive.ubuntu.com'" install -y foo
check "a name-lookup failure is retried until it succeeds" "$RC/$CALLS" "0/4"

run 2 100 "E: Unable to fetch some archives, maybe run apt-get update or try with --fix-missing?" install -y foo
check "'unable to fetch some archives' is retried" "$RC/$CALLS" "0/3"

run 9 100 "E: Failed to fetch http://x  Connection timed out" install -y foo
check "a persistent network failure gives up after the attempt limit, with its exit code" "$RC/$CALLS" "100/4"
echo "$OUT" | grep -q "giving up" && ok "and says that it gave up" || bad "and says that it gave up"

run 1 100 "E: Unable to locate package no-such-package" install -y no-such-package
check "a real error (no such package) is not retried" "$RC/$CALLS" "100/1"
echo "$OUT" | grep -q "not a network problem" && ok "and says why" || bad "and says why"

run 1 0 "W: Failed to fetch http://x/InRelease  Temporary failure resolving 'x'" update
check "'update' that exits 0 but failed to fetch an index is retried" "$RC/$CALLS" "0/2"

run 0 0 "" update
check "a clean update runs once" "$RC/$CALLS" "0/1"

run 5 100 "E: Failed to fetch http://x  Temporary failure resolving 'x'" remove -y foo
check "'remove' is never retried" "$RC/$CALLS" "100/1"

run 5 100 "boom" -s install foo
check "a simulated install (-s) still goes through the retry logic only for its verb" "$RC/$CALLS" "100/1"

echo "apt_harden.sh"

BIN="$WORK/bin"; APT="$WORK/apt"
mkdir -p "$APT/sources.list.d"
echo "http://azure.archive.ubuntu.com/ubuntu/ priority:1" > "$APT/apt-mirrors.txt"
printf 'Types: deb\nURIs: mirror+file:%s/apt-mirrors.txt\n' "$APT" > "$APT/sources.list.d/ubuntu.sources"

# the Azure mirror does not resolve, the archive does (localhost stands in for it)
OUT="$(APT_HARDEN_TEST=1 APT_DIR="$APT" APT_BIN_DIR="$BIN" APT_HARDEN_WAIT=0 APT_HARDEN_PRIMARY=localhost APT_HARDEN_AZURE=azure-mirror.invalid bash "$HERE/apt_harden.sh" 2>&1)"
RC=$?
check "exits 0" "$RC" "0"
[ -f "$APT/apt.conf.d/80-ci-retries" ] && grep -q 'Acquire::Retries "5"' "$APT/apt.conf.d/80-ci-retries" && ok "writes apt's retry and timeout settings" || bad "writes apt's retry and timeout settings"
[ -x "$BIN/apt-get" ] && ok "installs the retrying apt-get, executable" || bad "installs the retrying apt-get, executable"
cmp -s "$HERE/apt-get-retry.sh" "$BIN/apt-get" && ok "and it is the retry script" || bad "and it is the retry script"

# now with the Azure host actually present in the files, and failing to resolve
echo "http://azure-mirror.invalid/ubuntu/ priority:1" > "$APT/apt-mirrors.txt"
APT_HARDEN_TEST=1 APT_DIR="$APT" APT_BIN_DIR="$BIN" APT_HARDEN_WAIT=0 APT_HARDEN_PRIMARY=localhost APT_HARDEN_AZURE=azure-mirror.invalid bash "$HERE/apt_harden.sh" >/dev/null 2>&1
grep -q "archive.ubuntu.com" "$APT/apt-mirrors.txt" && ! grep -q "azure-mirror.invalid" "$APT/apt-mirrors.txt" && ok "an unresolvable Azure mirror is replaced by archive.ubuntu.com" || bad "an unresolvable Azure mirror is replaced by archive.ubuntu.com" "$(cat "$APT/apt-mirrors.txt")"

# a resolvable mirror is left alone
echo "http://localhost/ubuntu/ priority:1" > "$APT/apt-mirrors.txt"
APT_HARDEN_TEST=1 APT_DIR="$APT" APT_BIN_DIR="$BIN" APT_HARDEN_WAIT=0 APT_HARDEN_PRIMARY=localhost APT_HARDEN_AZURE=localhost bash "$HERE/apt_harden.sh" >/dev/null 2>&1
grep -q "http://localhost/ubuntu/" "$APT/apt-mirrors.txt" && ok "a mirror that resolves is left alone" || bad "a mirror that resolves is left alone"

# nothing resolves: it waits, gives up, rewrites nothing and still exits 0
echo "http://azure-mirror.invalid/ubuntu/ priority:1" > "$APT/apt-mirrors.txt"
OUT="$(APT_HARDEN_TEST=1 APT_DIR="$APT" APT_BIN_DIR="$BIN" APT_HARDEN_WAIT=1 APT_HARDEN_PRIMARY=also-down.invalid APT_HARDEN_AZURE=azure-mirror.invalid bash "$HERE/apt_harden.sh" 2>&1)"
RC=$?
check "with no name lookup at all it still exits 0" "$RC" "0"
grep -q "azure-mirror.invalid" "$APT/apt-mirrors.txt" && ok "and rewrites nothing" || bad "and rewrites nothing"

# not root and not a test: it does nothing and exits 0
if [ "$(id -u)" -ne 0 ]; then
	OUT="$(APT_DIR="$APT" APT_BIN_DIR="$WORK/none" bash "$HERE/apt_harden.sh" 2>&1)"
	check "without root it skips and exits 0" "$?" "0"
	[ ! -e "$WORK/none" ] && ok "and changes nothing" || bad "and changes nothing"
fi

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
