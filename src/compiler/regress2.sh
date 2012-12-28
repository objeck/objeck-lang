#!/bin/bash 

FILES="rc/*.obs"
for f in $FILES 
	do
		./obc -src $f -lib collect.obl,regex.obl,xml.obl,json.obl -dest a.obe | grep "rc/"
	done
