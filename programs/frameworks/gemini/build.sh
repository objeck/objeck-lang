#!/bin/sh

export OBJECK_LIB_PATH=/Users/randyhollines/Documents/Code/objeck-lang/core/release/deploy/lib
export PATH=$PATH:/Users/randyhollines/Documents/Code/objeck-lang/core/release/deploy/bin

rm -f *.obe

obc -src ../../../core/compiler/lib_src/gemini.obs -lib misc,json,net,encrypt -tar lib -opt s3 -dest ../../../core/release/deploy/lib/gemini.obl

if [ ! -z "$1" ]; then
	obc -src $1 -lib net,json,encrypt,misc,gemini -dest $1
	obr $1 $2 $3 $4 $5
fi
