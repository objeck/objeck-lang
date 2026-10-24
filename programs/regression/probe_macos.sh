#!/bin/bash
# Throwaway diagnostic for collect_hash_open timing out on macOS arm64 only.
cd "$(dirname "$0")"
for d in ../../core/release/deploy-arm64 ../../core/release/deploy; do
  if [ -d "$d/bin" ]; then DEPLOY=$(cd "$d" && pwd); break; fi
done
echo "deploy: $DEPLOY"
export OBJECK_LIB_PATH=$DEPLOY/lib
OBC=$DEPLOY/bin/obc
OBR=$DEPLOY/bin/obr
"$OBC" -src probe_hash_growth.obs -lib collect -opt s3 -dest probe_hash_growth.obe | tail -1

run() {
  label="$1"; shift
  echo "===== $label"
  "$@" &
  pid=$!
  for s in $(seq 1 90); do
    if ! kill -0 $pid 2>/dev/null; then break; fi
    sleep 1
  done
  if kill -0 $pid 2>/dev/null; then
    echo "----- still running after 90 s; sampling pid $pid"
    sample $pid 3 -file /tmp/sample_$pid.txt >/dev/null 2>&1
    grep -E "Call graph|Thread_|^ +[0-9]+ " /tmp/sample_$pid.txt | head -80
    kill -9 $pid
  fi
  wait $pid 2>/dev/null
  echo "exit: $?"
}

run "boxed 1.2M, JIT default" "$OBR" probe_hash_growth.obe 1200000 boxed
run "prebuilt 1.2M, JIT default" "$OBR" probe_hash_growth.obe 1200000 prebuilt
run "boxed 1.2M, JIT off" env OBJECK_JIT_DISABLE=1 "$OBR" probe_hash_growth.obe 1200000 boxed
run "boxed 300k, JIT default" "$OBR" probe_hash_growth.obe 300000 boxed
exit 1
