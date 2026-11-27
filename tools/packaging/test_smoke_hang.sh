#!/usr/bin/env bash
#
# test_smoke_hang.sh [image]
#
# smoke.sh must reach its verdict when a packaged tool hangs. The tree here is fake: every
# tool in bin/ is a script, obc one that sleeps for ten minutes. smoke.sh has to report the
# failure of each, and stop with exit 1 inside a minute and a half, not run `obc --version` a
# second time with no timeout. Needs docker (DOCKER="sudo docker" where it needs root) and
# an image with ldd and timeout (default ubuntu:24.04).

set -euo pipefail
IMAGE="${1:-ubuntu:24.04}"
read -r -a DOCKER <<< "${DOCKER:-docker}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/pkg/bin" "$WORK/fixture"
for t in obc obr obd obi obu obb; do printf '#!/bin/sh\necho fake %s\n' "$t" > "$WORK/pkg/bin/$t"; done
printf '#!/bin/sh\nsleep 600\n' > "$WORK/pkg/bin/obc"
chmod -R a+rx "$WORK"
cp "$REPO/tools/packaging/portable-linux/smoke.sh" "$WORK/fixture/"

start=$(date +%s)
set +e
out=$("${DOCKER[@]}" run --rm --network none -v "$WORK/pkg:/pkg:ro" -v "$WORK/fixture:/fixture:ro" "$IMAGE" sh /fixture/smoke.sh 2>&1)
rc=$?
set -e
took=$(( $(date +%s) - start ))
echo "$out" | tail -n 8
fails=0
check() { if "$@"; then echo "  ok: $DESC"; else echo "  FAIL: $DESC"; fails=$((fails + 1)); fi; }
DESC="smoke.sh exits 1 (exit $rc)"; check [ "$rc" -eq 1 ]
DESC="it reached its verdict in ${took}s, under 90"; check [ "$took" -lt 90 ]
DESC="it names obc as the tool that did not answer"; check grep -q "did not start and answer as it should: .*obc" <<< "$out"
[ $fails -eq 0 ] || exit 1
echo "PASS: smoke.sh gives a verdict when obc hangs"
