#!/usr/bin/env bash
#
# local_linux.sh [REF] [--jit1] [--flags] [--skip-regression]
#
# Builds and tests the COMMITTED tree (REF, default HEAD) the way ci-build.yml's
# linux-x64 leg does, in a clean copy, so a break shows up before the push rather
# than 30 minutes into the cloud run. Run it inside WSL2 Ubuntu 24.04 (the image
# the runner is pinned to) or on any Linux box with the packages ci-build installs.
#
#   * a clean copy from `git archive`: untracked files and local edits cannot hide
#     a missing file, and line endings are the committed ones, as on the runner
#   * deploy_posix.sh x64, the same script CI runs
#   * check_docs_deps.py --artifacts: the built tree against runtime_deps.json,
#     the check that failed master CI when ubuntu-latest moved to 26.04 (#1479)
#   * the regression suite, and with --jit1 again with OBJECK_JIT_THRESHOLD=1
#   * with --flags, run_vm_flag_tests.py
#
# Uncommitted changes are NOT tested: commit first (a throwaway commit is fine).
# The work tree is kept under $OBJECK_CI_WORK (default ~/objeck-ci/<sha>) so a failure
# can be inspected; older ones are pruned to the newest three.
set -uo pipefail

REF=HEAD
JIT1=0
FLAGS=0
REGRESSION=1
for arg in "$@"; do
  case "$arg" in
    --jit1)            JIT1=1 ;;
    --flags)           FLAGS=1 ;;
    --skip-regression) REGRESSION=0 ;;
    -*)                echo "unknown option: $arg"; exit 2 ;;
    *)                 REF="$arg" ;;
  esac
done

case "$(uname -s)" in
  Linux) ;;
  *) echo "local_linux.sh builds the Linux leg: run it inside WSL2 Ubuntu or on Linux"; exit 2 ;;
esac

repo=$(git rev-parse --show-toplevel) || exit 2
sha=$(git -c safe.directory='*' rev-parse --short "$REF") || exit 2
base="${OBJECK_CI_WORK:-$HOME/objeck-ci}"
work="$base/$sha"

echo "== local linux-x64 leg: $REF ($sha)"
if [ -n "$(git -c safe.directory='*' status --porcelain --untracked-files=no 2>/dev/null)" ]; then
  echo "   note: the working tree has uncommitted changes; only $sha is tested"
fi

rm -rf "$work"
mkdir -p "$work"
git -c safe.directory='*' archive "$REF" | tar -x -C "$work" || exit 2
# keep the three newest trees
ls -1dt "$base"/*/ 2>/dev/null | tail -n +4 | xargs -r rm -rf

FAIL=0
step() { printf '\n== %s\n' "$*"; }
bad()  { echo "   FAIL: $*"; FAIL=1; }

step "build (deploy_posix.sh x64)"
start=$(date +%s)
( cd "$work/core/release" && sh ./deploy_posix.sh x64 ) > "$work/build.log" 2>&1
rc=$?
echo "   exit $rc after $(( $(date +%s) - start )) s, log: $work/build.log"
if [ "$rc" -ne 0 ] || [ ! -x "$work/core/release/deploy/bin/obr" ]; then
  tail -25 "$work/build.log"
  bad "the build failed"
  exit 1
fi

step "built tree vs runtime_deps.json"
( cd "$work" && python3 tools/cicd/check_docs_deps.py --artifacts core/release/deploy --platform linux-x64 ) \
  || bad "the built tree links libraries runtime_deps.json does not list (or the reverse)"

if [ "$REGRESSION" -eq 1 ]; then
  step "regression"
  ( cd "$work/programs/regression" && chmod +x run_regression.sh && ./run_regression.sh x64 ) > "$work/regression.log" 2>&1
  tail -4 "$work/regression.log"
  grep -q ", 0 failed" "$work/regression.log" || bad "regression had failures: $work/regression.log"

  if [ "$JIT1" -eq 1 ]; then
    step "regression, OBJECK_JIT_THRESHOLD=1"
    ( cd "$work/programs/regression" && OBJECK_JIT_THRESHOLD=1 ./run_regression.sh x64 ) > "$work/regression_jit1.log" 2>&1
    tail -4 "$work/regression_jit1.log"
    grep -q ", 0 failed" "$work/regression_jit1.log" || bad "JIT-threshold-1 regression had failures: $work/regression_jit1.log"
  fi
fi

if [ "$FLAGS" -eq 1 ]; then
  step "run_vm_flag_tests.py"
  ( cd "$work/programs/regression" && python3 run_vm_flag_tests.py "$work/core/release/deploy/bin" ) > "$work/flags.log" 2>&1
  grep -c '\[PASS\]' "$work/flags.log" | sed 's/^/   passed: /'
  grep '\[FAIL\]' "$work/flags.log" | head -10
  grep -q '\[FAIL\]' "$work/flags.log" && bad "flag tests had failures: $work/flags.log"
fi

echo
if [ "$FAIL" -eq 0 ]; then
  echo "LOCAL LINUX LEG PASSES for $sha"
else
  echo "LOCAL LINUX LEG FAILED for $sha (tree kept: $work)"
fi
exit "$FAIL"
