#!/usr/bin/env bash
#
# run_tsan.sh TREE [--raw] [--only REGEX]
#
# Runs the regression tests that start threads under a ThreadSanitizer build of
# the VM and fails if a test fails or a report remains. Linux x64 (GCC).
#
# TREE is a tree that deploy_posix.sh has built, with core/release/deploy in it:
# the one tools/cicd/local_linux.sh leaves in ~/objeck-ci/<sha> is what this was
# written against. Nothing in TREE is changed. Everything is written under
# $OBJECK_TSAN_WORK (default TREE/tsan):
#
#   core/           a copy of core/vm and core/shared, the rest of core linked;
#                   obr built there with -fsanitize=thread -O1 and no LTO
#   core/release/deploy   a copy of TREE's deploy tree with that obr; obc and
#                   the libraries are the normal ones
#   programs/       a copy of TREE/programs; the tests run from here
#   logs/<test>.<pid>     the reports, one file per VM process, named by test
#   logs/by_test.tsv      test <tab> report summary, one line per distinct pair
#   logs/summary.txt      what ran, what failed, the report classes
#
# Which tests: every programs/regression/*.obs that mentions Thread or
# System.Concurrency, less the names in tools/tsan/racy_tests.txt (programs
# that race on their own data on purpose; see that file). --only REGEX narrows
# the set by test name.
#
# Reports listed in tools/tsan/tsan.supp are suppressed; each entry there says
# why. --raw runs without the suppressions and with the racy tests, to see
# everything the tool can say.
#
# A report does not change a test's exit code (exitcode=0), so a failed test
# here is a test that fails, and the reports are counted separately.
#
# OBJECK_TSAN_JOBS (default 6) build jobs, OBJECK_TSAN_SHARDS (default 4) test
# processes, TEST_TIMEOUT (default 900 s a test: the instrumented VM is 5 to 15
# times slower). The machine should not be busy: a starved thread test times out.
#
set -uo pipefail

TREE=""
RAW=0
ONLY=""
while [ $# -gt 0 ]; do
  case "$1" in
    --raw)  RAW=1 ;;
    --only) shift; ONLY="${1:-}" ;;
    -*)     echo "unknown option: $1"; exit 2 ;;
    *)      TREE="$1" ;;
  esac
  shift
done
if [ -z "$TREE" ] || [ ! -x "$TREE/core/release/deploy/bin/obc" ]; then
  echo "usage: run_tsan.sh TREE [--raw] [--only REGEX]   (TREE/core/release/deploy must be built)"
  exit 2
fi
TREE=$(cd "$TREE" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
WORK="${OBJECK_TSAN_WORK:-$TREE/tsan}"
JOBS="${OBJECK_TSAN_JOBS:-6}"
SHARDS="${OBJECK_TSAN_SHARDS:-4}"
SUPP="$HERE/tsan.supp"
RACY="$HERE/racy_tests.txt"

rm -rf "$WORK"
mkdir -p "$WORK/core/release" "$WORK/logs" || exit 2
LOGS="$WORK/logs"

# --- the instrumented VM ---
cp -r "$TREE/core/vm" "$TREE/core/shared" "$WORK/core/" || exit 2
cp -r "$TREE/core/release/deploy" "$WORK/core/release/deploy" || exit 2
for d in "$TREE"/core/*; do
  [ -e "$WORK/core/$(basename "$d")" ] || ln -s "$d" "$WORK/core/$(basename "$d")"
done
# -O1 keeps the stacks readable; LTO and the sanitizer runtime do not mix well
find "$WORK/core/vm" \( -name 'Makefile*' -o -name '*.mk' \) -print0 | xargs -0 sed -i 's/-O3 -flto=auto/-O1/; s/ -flto=auto//'
(
  cd "$WORK/core/vm" && cp make/Makefile.amd64 Makefile && make clean > /dev/null 2>&1
  make -j"$JOBS" CXX="g++ -fsanitize=thread -g -fno-omit-frame-pointer"
) > "$WORK/build.log" 2>&1
if [ ! -x "$WORK/core/vm/obr" ]; then
  echo "the ThreadSanitizer build of obr failed: see $WORK/build.log"
  exit 1
fi
BIN="$WORK/core/release/deploy/bin"
cp "$WORK/core/vm/obr" "$BIN/obr.real" || exit 2

# run_regression.sh calls bin/obr; this names each process's log after its test
cat > "$BIN/obr" <<'WRAP'
#!/usr/bin/env bash
n=obr
for a in "$@"; do case "$a" in *.obe) n=$(basename "${a%.obe}") ;; esac; done
export TSAN_OPTIONS="log_path=$OBJECK_TSAN_LOGS/$n:halt_on_error=0:report_signal_unsafe=0:history_size=4:second_deadlock_stack=1:exitcode=0${OBJECK_TSAN_SUPP:+:suppressions=$OBJECK_TSAN_SUPP}"
exec "$(dirname "$0")/obr.real" "$@"
WRAP
chmod +x "$BIN/obr"

# --- the tests ---
cp -r "$TREE/programs" "$WORK/programs" || exit 2
cd "$WORK/programs/regression" || exit 2
skipped=0
for f in *.obs; do
  n="${f%.obs}"
  if ! grep -Eq 'Thread\b|System\.Concurrency' "$f"; then rm -f "$f"; continue; fi
  if [ -n "$ONLY" ] && ! echo "$n" | grep -Eq "$ONLY"; then rm -f "$f"; continue; fi
  if [ "$RAW" = 0 ] && sed 's/[[:space:]]*#.*//; /^$/d' "$RACY" | grep -qx "$n"; then
    rm -f "$f"; skipped=$((skipped + 1))
  fi
done
count=$(ls *.obs 2>/dev/null | wc -l)
echo "== ThreadSanitizer: $count tests, $skipped listed in racy_tests.txt left out, $SHARDS at a time$([ "$RAW" = 1 ] && echo ', raw (no suppressions)')"

export TEST_TIMEOUT="${TEST_TIMEOUT:-900}" OBJECK_TSAN_LOGS="$LOGS"
if [ "$RAW" = 0 ]; then export OBJECK_TSAN_SUPP="$SUPP"; else unset OBJECK_TSAN_SUPP; fi
pids=()
for s in $(seq 0 $((SHARDS - 1))); do
  ( REGRESSION_SHARD="$s/$SHARDS" ./run_regression.sh x64 > "$LOGS/regression_shard$s.log" 2>&1 ) &
  pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done

# --- the result ---
for f in "$LOGS"/*.[0-9]*; do
  [ -f "$f" ] || continue
  t=$(basename "$f"); t="${t%.*}"
  grep -h '^SUMMARY' "$f" | sed "s#$WORK/##; s#\.\./posix/\.\./\.\./##; s#^SUMMARY: ThreadSanitizer: ##" | sort -u | sed "s#^#$t\t#"
done | sort -u > "$LOGS/by_test.tsv"
failed=$(awk '/^Running: /{n=$2} /\[FAIL\]/{print n}' "$LOGS"/regression_shard*.log | sort -u)
{
  echo "tests: $count (left out as racy: $skipped)"
  awk '/Results:/ { p += $2; s += $4; f += $6 } END { printf "passed %d, skipped %d, failed %d\n", p, s, f }' "$LOGS"/regression_shard*.log
  [ -n "$failed" ] && echo "failed: $(echo $failed)"
  echo "reports, by class (number of tests):"
  cut -f2 "$LOGS/by_test.tsv" | sort | uniq -c | sort -rn
} > "$LOGS/summary.txt"
cat "$LOGS/summary.txt"

reports=$(wc -l < "$LOGS/by_test.tsv")
if [ -n "$failed" ] || [ "$reports" -gt 0 ]; then
  echo "== FAILED: see $LOGS (by_test.tsv names the test for each report)"
  exit 1
fi
echo "== ThreadSanitizer: clean"
