#!/bin/bash
# Black-box security checks for Web.HTTP.Server, against a local plain-HTTP target (127.0.0.1:8098).
# About 80 hostile or malformed requests -- header and request-line limits, malformed headers,
# traversal in every spelling, Range abuse, connection floods, trickled requests -- each followed
# by a check that the server still answers. Takes a few minutes (the trickle case waits out the
# 30 s request deadline). Run from a built tree:
#
#   DEP=<deploy tree> tools/security/run_web_server_checks.sh
set -e
DEP=${DEP:?set DEP to a built deploy tree}
HERE=$(cd "$(dirname "$0")" && pwd)
export OBJECK_LIB_PATH=$DEP/lib OBJECK_STACK_SIZE=2048 OBR=$DEP/bin/obr
ulimit -n 8192
W=$(mktemp -d); cd "$W"
$DEP/bin/obc -src "$HERE/web_server_target.obs" -lib net,net_server,json,cipher -dest target.obe
exec python3 -I "$HERE/web_server_harness.py" target.obe