#!/bin/sh

export OBJECK_ROOT=/Users/randyhollines/Documents/Code/objeck-lang
export OBJECK_LIB_PATH=$OBJECK_ROOT/core/release/deploy/lib
export PATH=$PATH:$OBJECK_ROOT/core/release/deploy/bin

obc -src $OBJECK_ROOT/core/compiler/lib_src/openai.obs -lib json,net,misc -tar lib -dest $OBJECK_ROOT/core/release/deploy/lib/openai.obl

rm -f *.obe
obc -src openai_tune -lib openai,csv,net,json,misc
obr openai_tune $1 $2
