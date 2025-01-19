#!/bin/sh

rm *.obe

OBJK_BASE=/Users/randyhollines/Documents/Code/objeck-lang
export PATH=$PATH:$OBJK_BASE/core/release/deploy/bin
export OBJECK_LIB_PATH=$OBJK_BASE/core/release/deploy/lib

obc -src ../../core/compiler/lib_src/net.obs,../../core/compiler/lib_src/net_common.obs,../../core/compiler/lib_src/net_secure.obs -tar lib -lib json,cipher -dest ../../core/release/deploy/lib/net.obl

if [ ! -z "$1" ]; then
	obc -src $1 -lib net,json,cipher
	obr $1
fi
