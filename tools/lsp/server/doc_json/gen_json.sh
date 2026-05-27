#!/bin/bash
set -e

cd "$(dirname "$0")"

VERSION=${1:-0.0.0}

OBJECK_ROOT=../../../objeck-lang
LIB_SRC=$OBJECK_ROOT/core/compiler/lib_src

export PATH=$PATH:$OBJECK_ROOT/core/release/deploy/bin
export OBJECK_LIB_PATH=$OBJECK_ROOT/core/release/deploy/lib

rm -f *.obe

obc -src doc_json.obs,doc_parser.obs -lib gen_collect,xml,json,cipher -dest doc_json.obe

obr doc_json.obe templates "$VERSION" \
	$LIB_SRC/lang.obs \
	$LIB_SRC/regex.obs \
	$LIB_SRC/json_stream.obs \
	$LIB_SRC/json.obs \
	$LIB_SRC/xml.obs \
	$LIB_SRC/cipher.obs \
	$LIB_SRC/odbc.obs \
	$LIB_SRC/csv.obs \
	$LIB_SRC/query.obs \
	$LIB_SRC/sdl2.obs \
	$LIB_SRC/sdl_game.obs \
	$LIB_SRC/gen_collect.obs \
	$LIB_SRC/net_common.obs \
	$LIB_SRC/net.obs \
	$LIB_SRC/net_secure.obs \
	$LIB_SRC/net_server.obs \
	$LIB_SRC/rss.obs \
	$LIB_SRC/misc.obs \
	$LIB_SRC/diags.obs \
	$LIB_SRC/ml.obs \
	$LIB_SRC/nlp.obs \
	$LIB_SRC/openai.obs \
	$LIB_SRC/gemini.obs \
	$LIB_SRC/ollama.obs \
	$LIB_SRC/opencv.obs \
	$LIB_SRC/onnx.obs \
	$LIB_SRC/json_rpc.obs \
	$LIB_SRC/lame.obs

mv out.json ../objk_apis.json
