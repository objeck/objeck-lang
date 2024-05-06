#!/bin/sh

export OBJECK_LIB_PATH=/Users/randyhollines/Documents/Code/objeck-lang/core/release/deploy/lib
export PATH=$PATH:/Users/randyhollines/Documents/Code/objeck-lang/core/release/deploy/bin

rm -f *.obe
obc -src openai_chat -lib openai,csv,net,json,misc
obr openai_chat $1
