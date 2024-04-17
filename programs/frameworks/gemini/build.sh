#!/bin/sh

export OBJECK_LIB_PATH=/Users/randyhollines/Documents/Code/objeck-lang/core/release/deploy/lib
export PATH=$PATH:/Users/randyhollines/Documents/Code/objeck-lang/core/release/deploy/bin

rm -f *.obe

if [ ! -z "$1" ]; then
	obc -src $1 -lib net,json,encrypt,misc,gemini -dest $1
	if [ ! -z "$2" ]; then
		obr $1 $2 $3 $4 $5
	fi
fi
