#!/bin/sh
# The Objeck library set, in dependency order. This is the ONE copy: update_version.sh
# runs it after building the compiler on Linux/macOS, and ci-build.yml runs it on the
# Windows runners with the MSBuild-built obc. It used to be duplicated in both places
# (and once in update_version_arm.sh), and the copies had to be diffed by hand.
#
# usage: build_libs.sh <path-to-obc>      (run from core/compiler)
#
# lang.obl is not here: it needs the _SYSTEM bootstrap compiler, which only the
# POSIX flow builds (update_version.sh) and Windows takes from the tree.
set -e
OBC=${1:?usage: build_libs.sh <path-to-obc>}

$OBC -src lib_src/gen_collect.obs -lib ../lib/lang -tar lib -opt s3 -dest ../lib/gen_collect.obl -strict
$OBC -src lib_src/concurrent.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/concurrent.obl
$OBC -src lib_src/json_stream.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/json_stream.obl
$OBC -src lib_src/cipher.obs -tar lib -opt s3 -dest ../lib/cipher.obl
$OBC -src lib_src/json.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/json.obl
$OBC -src lib_src/opencv.obs -lib cipher,json -tar lib -opt s3 -dest ../lib/opencv.obl
$OBC -src lib_src/onnx.obs -lib opencv,cipher,json -tar lib -opt s3 -dest ../lib/onnx.obl
$OBC -src lib_src/lame.obs -tar lib -opt s3 -dest ../lib/lame.obl
$OBC -src lib_src/diags.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/diags.obl
$OBC -src lib_src/xml.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/xml.obl
$OBC -src lib_src/regex.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/regex.obl
$OBC -src lib_src/csv.obs -tar lib -lib gen_collect -opt s3 -dest ../lib/csv.obl
$OBC -src lib_src/ml_core.obs,lib_src/ml_linear.obs,lib_src/ml_tree.obs,lib_src/ml_bayes.obs,lib_src/ml_neighbors.obs,lib_src/ml_cluster.obs,lib_src/ml_data.obs -lib gen_collect,csv -tar lib -opt s3 -dest ../lib/ml.obl
$OBC -src lib_src/ai_search.obs,lib_src/ai_game.obs,lib_src/ai_optimize.obs,lib_src/ai_rl.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/ai.obl
$OBC -src lib_src/nlp.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/nlp.obl
$OBC -src lib_src/net_common.obs,lib_src/net.obs,lib_src/net_secure.obs -tar lib -lib gen_collect,cipher -opt s3 -dest ../lib/net.obl
$OBC -src lib_src/net_h2.obs -tar lib -lib net,gen_collect,cipher -opt s3 -dest ../lib/net_h2.obl
$OBC -src lib_src/net_quic.obs -tar lib -lib net,gen_collect,cipher -opt s3 -dest ../lib/net_quic.obl
$OBC -src lib_src/net_server.obs -tar lib -lib net,json,gen_collect,cipher -opt s3 -dest ../lib/net_server.obl
$OBC -src lib_src/web_server.obs -lib gen_collect,net,net_server,json,cipher -tar lib -opt s3 -dest ../lib/web_server.obl
$OBC -src lib_src/json_rpc.obs -tar lib -lib json,net -opt s3 -dest ../lib/json_rpc.obl
$OBC -src lib_src/misc.obs -lib gen_collect,net,json -tar lib -opt s3 -dest ../lib/misc.obl
$OBC -src lib_src/rss.obs -tar lib -lib xml,gen_collect,net,cipher -opt s3 -dest ../lib/rss.obl
$OBC -src lib_src/query.obs -tar lib -lib net,regex,csv,xml,json,misc -opt s3 -dest ../lib/query.obl
$OBC -src lib_src/odbc.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/odbc.obl
$OBC -src lib_src/openai.obs -lib json,net,net_server,cipher,misc -tar lib -opt s3 -dest ../lib/openai.obl
$OBC -src lib_src/gemini.obs -lib misc,json,net,net_server,cipher -tar lib -opt s3 -dest ../lib/gemini.obl
$OBC -src lib_src/ollama.obs -lib net,json,cipher,misc -tar lib -opt s3 -dest ../lib/ollama.obl
$OBC -src lib_src/sdl2.obs -tar lib -dest ../lib/sdl2.obl
$OBC -src lib_src/sdl_game.obs -lib gen_collect,json,sdl2 -tar lib -dest ../lib/sdl_game.obl
$OBC -src lib_src/sdl_gl.obs -lib gen_collect,sdl2 -tar lib -dest ../lib/sdl_gl.obl
