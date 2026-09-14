#!/bin/bash
#
# verify_platform.sh <VERSION> [--skip-build]
#
# Release-candidate verification for ONE POSIX machine: Linux x64, Linux ARM64
# (including WSL2) or macOS arm64. Every machine runs this same script against
# the same commit, so a release is verified by one repeatable procedure instead
# of a different hand-written test list per machine per release. The result
# file it writes is what tools/cicd/pre_release.sh reads before tagging.
#
# Run from a CLEAN checkout of the commit to be released, in a native shell:
#   0. prerequisites: openssl, python3, expect; git, cmake, make for the build
#   1. build the deploy tree exactly as release-build.yml does
#      (tools/deps/build_quic_deps.sh, tools/deps/build_macos_deps.sh on macOS, then deploy_posix.sh / deploy_macos_arm64.sh)
#   2. regression suite, default JIT
#   3. regression suite, every method compiled (OBJECK_JIT_THRESHOLD=1)
#   4. VM flag tests, debugger tests, DAP tests
#
# Writes rc-results/<VERSION>/<platform>.txt (key=value, verdict=PASS|FAIL) and
# rc-results/<VERSION>/<platform>.log, restores any tracked files the deploy
# rewrote, and exits 0 only when every step passed. Anything skipped is a FAIL;
# the one exception is build=SKIPPED from --skip-build, which tests an existing tree.
#
# Windows machines run tools/cicd/verify_platform.cmd, which writes the same format.

set -uo pipefail

usage() { echo "usage: verify_platform.sh <VERSION> [--skip-build]"; exit 2; }
VERSION="${1:-}"
case "$VERSION" in ""|-*) usage ;; esac
SKIP_BUILD=0
case "${2:-}" in
	"") ;;
	--skip-build) SKIP_BUILD=1 ;;
	*) echo "unknown argument: $2"; usage ;;
esac
[ $# -le 2 ] || { echo "unknown argument: $3"; usage; }

ROOT=$(git rev-parse --show-toplevel 2>/dev/null) || { echo "not inside a git checkout"; exit 2; }
cd "$ROOT"
COMMIT=$(git rev-parse HEAD)

case "$(uname -s)-$(uname -m)" in
	Linux-x86_64)  PLATFORM=linux-x64;   ARCH=x64;   DEPLOY=(./deploy_posix.sh x64) ;;
	Linux-aarch64) PLATFORM=linux-arm64; ARCH=arm64; DEPLOY=(./deploy_posix.sh arm64) ;;
	Darwin-arm64)  PLATFORM=macos-arm64; ARCH=arm64; DEPLOY=(./deploy_macos_arm64.sh) ;;
	Darwin-x86_64)
		# A shell under Rosetta on an arm64 Mac reports x86_64; what it would test
		# is not what an arm64 user runs.
		if [ "$(sysctl -n sysctl.proc_translated 2>/dev/null)" = 1 ]; then
			echo "unsupported platform: this shell runs under Rosetta; start a native one (arch -arm64 /bin/bash)"
		else
			echo "unsupported platform: Darwin-x86_64"
		fi
		exit 2 ;;
	*) echo "unsupported platform: $(uname -s)-$(uname -m)"; exit 2 ;;
esac
HOST="$(hostname)"
grep -qi microsoft /proc/version 2>/dev/null && HOST="$HOST (WSL2)"

OUT="$ROOT/rc-results/$VERSION"
RES="$OUT/$PLATFORM.txt"
LOG="$OUT/$PLATFORM.log"
mkdir -p "$OUT"
: > "$LOG"

VERDICT=PASS
declare -a LINES=()
record() {   # key value -- anything but PASS fails the verdict, except build=SKIPPED
	LINES+=("$1=$2")
	case "$2" in
		PASS*) ;;
		SKIPPED) [ "$1" = build ] || VERDICT=FAIL ;;
		*) VERDICT=FAIL ;;
	esac
	printf '  %-16s %s\n' "$1" "$2"
}
write_result() {
	{
		echo "platform=$PLATFORM"
		echo "host=$HOST"
		echo "version=$VERSION"
		echo "commit=$COMMIT"
		echo "date=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
		printf '%s\n' "${LINES[@]}"
		echo "verdict=$VERDICT"
	} > "$RES"
}

echo "== verify_platform $PLATFORM, v$VERSION @ ${COMMIT:0:10} on $HOST"

# ---- preconditions: a clean tree at the version being released ----------------
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
	record tree "FAIL (uncommitted changes to tracked files; verify a clean checkout)"
	write_result; exit 1
fi
record tree PASS
if grep -q "VERSION_STRING L\"$VERSION\"" core/shared/version.h; then
	record version PASS
else
	record version "FAIL (core/shared/version.h is $(sed -n 's/.*VERSION_STRING L"\([0-9.]*\)".*/\1/p' core/shared/version.h))"
	write_result; exit 1
fi

# ---- prerequisites: stop now, not an hour into the build ------------------------
# Each of these otherwise surfaces late: the TLS regression tests fail when they
# cannot run openssl for their certificate, run_debugger_tests.sh skips without
# expect, and the dependency builds stop without cmake.
MISSING=""
need() {   # tool why
	command -v "$1" >/dev/null 2>&1 && return 0
	echo "  missing prerequisite: $1 -- $2"
	MISSING="${MISSING:+$MISSING, }$1"
}
need openssl "the TLS regression tests create their certificate with it"
need python3 "run_vm_flag_tests.py and run_dap_tests.py"
need expect "run_debugger_tests.sh skips the debugger tests without it"
if [ $SKIP_BUILD -eq 0 ]; then
	for tool in git cmake make; do need "$tool" "tools/deps/build_quic_deps.sh"; done
	if [ "$PLATFORM" = macos-arm64 ]; then
		for tool in curl shasum install_name_tool codesign otool cmp /usr/bin/clang; do
			need "$tool" "tools/deps/build_macos_deps.sh"
		done
	fi
fi
if [ -n "$MISSING" ]; then
	record prereqs "FAIL (missing prerequisite: $MISSING)"
	write_result; exit 1
fi
record prereqs PASS

# Runs one step, appending its output to the log; prints the last lines on failure.
# A step that says it skipped its tests is a FAIL here even when it exits 0:
# run_debugger_tests.sh exits 0 with "WARNING: 'expect' not found, skipping
# debugger tests", and a release verified that way has verified nothing.
run_step() {   # key label command...
	local key="$1" label="$2"; shift 2
	local out; out=$(mktemp)
	echo "---- $label" >> "$LOG"
	local start=$SECONDS
	"$@" > "$out" 2>&1
	local rc=$?
	local secs=$((SECONDS - start))
	cat "$out" >> "$LOG"
	local skipped; skipped=$(grep -m1 -iE '\bskipping\b|not found, skip' "$out")
	if [ $rc -ne 0 ]; then
		record "$key" "FAIL (exit $rc, ${secs}s)"
		tail -25 "$out" | sed 's/^/      /'
	elif [ -n "$skipped" ]; then
		record "$key" "FAIL (did not run: $skipped)"
		rc=1
	else
		record "$key" "PASS (${secs}s)"
	fi
	rm -f "$out"
	return $rc
}

# The regression runners print a "N passed, N skipped, N failed" style summary;
# keep the count line next to the verdict so the result file is self-describing.
regression() {   # key label [env assignments...]
	local key="$1" label="$2"; shift 2
	local tmp; tmp=$(mktemp)
	echo "---- $label" >> "$LOG"
	local start=$SECONDS
	( cd programs/regression && env "$@" ./run_regression.sh "$ARCH" ) > "$tmp" 2>&1
	local rc=$?
	cat "$tmp" >> "$LOG"
	local counts
	counts=$(grep -E -i '^(passed|failed|skipped|total)[: ]' "$tmp" | tr -s ' ' | tr '\n' ' ' | sed 's/ $//')
	[ -n "$counts" ] || counts=$(grep -E -i '[0-9]+ passed' "$tmp" | tail -1)
	if [ $rc -eq 0 ]; then
		record "$key" "PASS (${counts:-counts not found}, $((SECONDS - start))s)"
	else
		record "$key" "FAIL (exit $rc; ${counts:-counts not found})"
		sed -n '/Failed tests:/,$p' "$tmp" | head -30 | sed 's/^/      /'
	fi
	rm -f "$tmp"
}

# ---- build ------------------------------------------------------------------------
if [ $SKIP_BUILD -eq 1 ]; then
	record build SKIPPED
else
	[ "$PLATFORM" = macos-arm64 ] && export MACOSX_DEPLOYMENT_TARGET=13.3
	if [ -f tools/deps/build_quic_deps.sh ]; then
		run_step quic_deps "static QUIC dependencies" bash tools/deps/build_quic_deps.sh || { write_result; exit 1; }
	fi
	if [ "$PLATFORM" = macos-arm64 ]; then
		run_step macos_deps "bundled macOS libraries" bash tools/deps/build_macos_deps.sh || { write_result; exit 1; }
	fi
	run_step build "deploy (${DEPLOY[*]})" bash -c "cd core/release && ${DEPLOY[*]}"
	# A deploy that "succeeds" without the toolchain is how v2026.5.0 shipped with no obr.
	BIN=core/release/deploy-$ARCH/bin
	[ -d "$BIN" ] || BIN=core/release/deploy/bin
	for tool in obc obr obd obi obb obu; do
		[ -x "$BIN/$tool" ] || { record "tool_$tool" "FAIL (missing from $BIN)"; }
	done
	# The deploy rewrites tracked files (version stamps, api.zip); the tree was clean
	# at the start, so put them back and list what changed.
	CHANGED=$(git status --porcelain --untracked-files=no | awk '{print $2}' | tr '\n' ' ')
	[ -n "$CHANGED" ] && { echo "restoring tracked files rewritten by the deploy: $CHANGED" >> "$LOG"; git checkout -- . ; }
	if [ "$VERDICT" = FAIL ]; then write_result; exit 1; fi
fi

BIN=core/release/deploy-$ARCH/bin
[ -d "$BIN" ] || BIN=core/release/deploy/bin
BIN_ABS="$ROOT/$BIN"

# ---- tests --------------------------------------------------------------------------
regression regression "regression (default JIT)"
regression regression_jit1 "regression (OBJECK_JIT_THRESHOLD=1)" OBJECK_JIT_THRESHOLD=1
run_step vm_flags "VM flag tests" bash -c "cd programs/regression && python3 run_vm_flag_tests.py '$BIN_ABS'"
run_step debugger "debugger tests" bash -c "cd programs/regression && ./run_debugger_tests.sh $ARCH"
run_step dap "DAP tests" bash -c "cd programs/regression && python3 run_dap_tests.py '$BIN_ABS'"

write_result
echo "== verdict: $VERDICT  ($RES)"
[ "$VERDICT" = PASS ]
