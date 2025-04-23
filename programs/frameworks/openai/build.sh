#!/usr/bin/env bash

if [ "$(uname)" == "Darwin" ]; then
	export OBJECK_LIB_PATH=/Users/randyhollines/Documents/Code/objeck-lang/core/release/deploy/lib
	export PATH=$PATH:/Users/randyhollines/Documents/Code/objeck-lang/core/release/deploy/bin
elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
	export OBJECK_LIB_PATH=/home/randy/Documents/Code/objeck-lang/core/release/deploy/lib
	export PATH=$PATH:/home/randy/Documents/Code/objeck-lang/core/release/deploy/bin
fi

rm -f *.obe

obc -src ../../../core/compiler/lib_src/net_common.obs,../../../core/compiler/lib_src/net.obs,../../../core/compiler/lib_src/net_secure.obs -lib cipher -tar lib -opt s3 -dest ../../../core/release/deploy/lib/net.obl

obc -src ../../../core/compiler/lib_src/net_server.obs -lib json,net,cipher -tar lib -opt s3 -dest ../../../core/release/deploy/lib/net_server.obl

obc -src ../../../core/compiler/lib_src/openai.obs -lib misc,json,net,net_server,cipher -tar lib -opt s3 -dest ../../../core/release/deploy/lib/opeani.obl

if [ ! -z "$1" ]; then
	obc -src $1 -lib net,json,misc,openai -dest $1
	obr $1 $2 $3 $4 $5
fi
