#!/bin/bash

OBJECK_ROOT=../../objeck-lang

export PATH=$PATH:$OBJECK_ROOT/core/release/deploy/bin
export OBJECK_LIB_PATH=$OBJECK_ROOT/core/release/deploy/lib

rm -f *.obe
rm -f /tmp/objk-*

echo ---

obc -src $OBJECK_ROOT/core/compiler/lib_src/diags.obs -lib gen_collect -tar lib -opt s3 -dest $OBJECK_ROOT/core/lib/diags.obl
if [ $? -ne 0 ]; then
	echo "Build failed: diags.obl"
	exit 1
fi
cp $OBJECK_ROOT/core/lib/diags.obl $OBJECK_ROOT/core/release/deploy/lib/diags.obl

echo ---

obc -src frameworks.obs,proxy.obs,server.obs,format_code/scanner.obs,format_code/formatter.obs -lib diags,net,json,regex,cipher -dest objeck_lsp.obe
if [ $? -ne 0 ]; then
	echo "Build failed: objeck_lsp.obe"
	exit 1
fi
cp objeck_lsp.obe ../clients/vscode/server

echo ---
echo "Build successful"

if [ "$1" = "brun" ]; then
	echo Running...
	obr objeck_lsp.obe objk_apis.json pipe debug
#	obr objeck_lsp.obe objk_apis.json 6013 debug
fi
