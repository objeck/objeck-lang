#!/bin/sh

OBJECK_ROOT=C:\Users\objec\Documents\Code\objeck-lang

rm -f *.obe

if [ ! -z "$1" ]; then
	obc -src $1 -lib net,json,encrypt -dest $1
	if [ ! -z "$2" ]; then
		obr $1 $2 $3 $4 $5
	fi
fi
