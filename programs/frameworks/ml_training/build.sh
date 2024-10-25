#!/usr/bin/env bash

if [ "$(uname)" == "Darwin" ]; then
	export OBJECK_LIB_PATH=/Users/randyhollines/Documents/Code/objeck-lang/core/release/deploy/lib
	export PATH=$PATH:/Users/randyhollines/Documents/Code/objeck-lang/core/release/deploy/bin
elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
	export OBJECK_LIB_PATH=/home/randy/Documents/Code/objeck-lang/core/release/deploy/lib
	export PATH=$PATH:/home/randy/Documents/Code/objeck-lang/core/release/deploy/bin
fi

rm -f *.obe

# obc -src ../../../core/compiler/lib_src/openai.obs -lib misc,json,net,cipher -tar lib -opt s3 -dest ../../../core/release/deploy/lib/opeani.obl

if [ ! -z "$1" ]; then
	obc -src $1 -lib net,json,misc,openai -dest $1
	obr $1 $2 $3 $4 $5
fi
