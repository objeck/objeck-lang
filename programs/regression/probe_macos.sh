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

echo "===== boxed 1.2M with GC stats, sampled during the 700k-750k pause"
OBJECK_GC_STATS=1 "$OBR" probe_hash_growth.obe 1200000 boxed > /tmp/probe_out.txt 2>&1 &
pid=$!
for s in $(seq 1 600); do
  if grep -q "700000 inserted" /tmp/probe_out.txt 2>/dev/null; then break; fi
  sleep 0.1
done
sleep 1
sample $pid 5 -file /tmp/sample.txt >/dev/null 2>&1
wait $pid
cat /tmp/probe_out.txt
echo "----- sample (heaviest frames)"
sed -n '/Call graph:/,/Total number in stack/p' /tmp/sample.txt | head -120
echo "----- sorted by top of stack"
sed -n '/Sort by top of stack/,$p' /tmp/sample.txt | head -40

echo "===== prebuilt 1.2M with GC stats"
OBJECK_GC_STATS=1 "$OBR" probe_hash_growth.obe 1200000 prebuilt 2>&1 | tail -6
exit 1
