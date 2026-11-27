#!/usr/bin/env bash
#
# cnn_repeat.sh [--copies N] [--rounds N] [--cpus LIST]
#
# Checks that seeded System.ML.Cnn training gives the same bits in EVERY PROCESS: it runs
# programs/regression/ml_cnn_train.obs (which prints every loss with all its digits) many times
# and requires every output to be identical to the first.
#
#   1. once pinned to one CPU of each KIND of core. Kinds are told apart by their cache sizes
#      (/sys/devices/system/cpu/cpuN/cache), which is what went wrong: Eigen sized the blocks of a
#      matrix product from the caches of whichever core ran the first product, so on a CPU with
#      performance and efficiency cores a run that started on the other kind printed other losses
#      (0.138337 against 0.138356). libobjk_ml now sets the sizes itself (FixEigenCacheSizes in
#      core/lib/matrix/matrix.cpp). A machine with one kind of core has nothing to pin; the step
#      says so and passes.
#   2. --rounds times (default 4), --copies copies at once (default 8) confined to the CPUs of
#      --cpus (default: one of each kind of core, made up to three with the first the process may
#      use) at the lowest priority, so that the layer threads of every copy are descheduled
#      constantly and a copy may start on either kind. Every copy runs 4 layer threads and, for a
#      few seconds, 8 busy ones: the load average rises by far more than the three CPUs it uses.
#
# The regression suite cannot do this: within one process the block sizes never change, so the
# test's own in-process repeat always agreed. Run it on a machine with two kinds of core after a
# change to the layers, the thread pool or the Eigen configuration.
#
# Needs Linux (taskset, sysfs), and obc and obr on PATH with OBJECK_LIB_PATH set. Run it from
# anywhere inside the repository. Exit status 0 when every run agreed, 1 otherwise, 2 for a setup
# problem.
set -uo pipefail

COPIES=8
ROUNDS=4
CPUS=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --copies) COPIES="${2:?--copies needs a number}"; shift 2 ;;
    --rounds) ROUNDS="${2:?--rounds needs a number}"; shift 2 ;;
    --cpus)   CPUS="${2:?--cpus needs a list, e.g. 0-2}"; shift 2 ;;
    *) echo "usage: cnn_repeat.sh [--copies N] [--rounds N] [--cpus LIST]"; exit 2 ;;
  esac
done

[ "$(uname -s)" = "Linux" ] || { echo "cnn_repeat.sh needs Linux (taskset and sysfs)"; exit 2; }
for tool in obc obr taskset; do
  command -v "$tool" >/dev/null 2>&1 || { echo "cnn_repeat.sh: $tool is not on PATH"; exit 2; }
done

root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# the UTF-8 byte order mark some sources carry stops the compiler on Linux
sed '1s/^\xEF\xBB\xBF//' "$root/programs/regression/ml_cnn_train.obs" > "$work/ml_cnn_train.obs"
libs="$(sed -n 's/.*# EXTRA_LIBS:[[:space:]]*//p' "$work/ml_cnn_train.obs" | head -n1)"
if ! obc -src "$work/ml_cnn_train.obs" -lib "$libs,json,cipher,xml" -dest "$work/t.obe" > "$work/compile.log" 2>&1; then
  cat "$work/compile.log"
  echo "cnn_repeat.sh: the test did not compile"
  exit 2
fi

# the CPUs this process may use, one per line
allowed="$(taskset -cp $$ | sed 's/.*: //' | tr ',' '\n' | while IFS=- read -r lo hi; do seq "$lo" "${hi:-$lo}"; done)"

fail=0
runs=0

# compare NAME: the output of run NAME against the reference
compare() {
  runs=$((runs + 1))
  if ! cmp -s "$work/reference.out" "$work/$1.out"; then
    fail=$((fail + 1))
    echo "  DIFFERENT: $1"
    diff "$work/reference.out" "$work/$1.out" | head -n 6 | sed 's/^/    /'
  fi
}

echo "== reference run"
obr "$work/t.obe" > "$work/reference.out" 2>&1
if ! grep -q '^ml_cnn_train: ok' "$work/reference.out"; then
  cat "$work/reference.out"
  echo "cnn_repeat.sh: the test itself fails"
  exit 1
fi

echo "== one run pinned to each kind of core"
kinds="$(for cpu in $allowed; do
  echo "$(cat /sys/devices/system/cpu/cpu"$cpu"/cache/index*/size 2>/dev/null | paste -sd/) $cpu"
done | sort -k1,1 -u)"
[ -n "$CPUS" ] || CPUS="$( (echo "$kinds" | cut -d' ' -f2; echo "$allowed") | awk '!seen[$0]++' | head -n3 | paste -sd,)"
if [ "$(echo "$kinds" | wc -l)" -lt 2 ]; then
  echo "  one kind of core here (caches $(echo "$kinds" | cut -d' ' -f1)): nothing to pin"
else
  while read -r caches cpu; do
    echo "  cpu $cpu (caches $caches)"
    taskset -c "$cpu" obr "$work/t.obe" > "$work/pinned$cpu.out" 2>&1
    compare "pinned$cpu"
  done <<< "$kinds"
fi

echo "== $ROUNDS round(s) of $COPIES copies at once on CPUs $CPUS, lowest priority"
for round in $(seq 1 "$ROUNDS"); do
  for copy in $(seq 1 "$COPIES"); do
    taskset -c "$CPUS" nice -n 19 obr "$work/t.obe" > "$work/load${round}_$copy.out" 2>&1 &
  done
  wait
  for copy in $(seq 1 "$COPIES"); do
    compare "load${round}_$copy"
  done
  echo "  round $round done"
done

if [ "$fail" -gt 0 ]; then
  echo "FAIL: $fail of $runs runs differ from the reference"
  exit 1
fi
echo "PASS: $runs runs, every output identical to the reference"
