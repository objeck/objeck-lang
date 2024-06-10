#!/usr/bin/env bash

if [ "$(uname)" == "Darwin" ]; then
	export OBJECK_LIB_PATH=/Users/randyhollines/Documents/Code/objeck-lang/core/release/deploy/lib
	export PATH=$PATH:/Users/randyhollines/Documents/Code/objeck-lang/core/release/deploy/bin
elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
	export OBJECK_LIB_PATH=/home/randy/Documents/Code/objeck-lang/core/release/deploy/lib
	export PATH=$PATH:/home/randy/Documents/Code/objeck-lang/core/release/deploy/bin
fi

obc -src ../../../core/compiler/lib_src/ollama.obs -lib net,json,encrypt,misc -tar lib -dest ../../../core/release/deploy/lib/ollama.obl
obc -src ../../../core/compiler/lib_src/net_common.obs,../../../core/compiler/lib_src/net.obs,../../../core/compiler/lib_src/net_secure.obs -tar lib -lib json,gen_collect -opt s3 -dest ../../../core/release/deploy/lib/net.obl

rm -f *.obe

obc -src ../../../core/compiler/lib_src/openai.obs -lib misc,json,net,encrypt -tar lib -opt s3 -dest ../../../core/release/deploy/lib/opeani.obl

if [ ! -z "$1" ]; then
	obc -src $1 -lib ollama,json,net,encrypt,misc

	if [ ! -z "$2" ]; then
		obr $1 $2 $3 $4 $5
	fi
fi
