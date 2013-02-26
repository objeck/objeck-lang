#!/bin/bash 
i=91
# i=104
# i=5
# i=87
until [  $i -lt 1 ]; do
	echo -------- prgm$i -----------

	./obc -src test_src/prgm$i.obs -lib xml.obl,collect.obl -opt s3 -dest a.obe
#	./obc -src test_src/prgm$i.obs -lib xml.obl,struct.obl -opt s0 -dest a.obe

	cd ../vm
	if [ $i = 41 ]; then
		./obr ../compiler/a.obe 7
	elif [ $i = 37 ]; then
		./obr ../compiler/a.obe 13
	elif [ $i = 60 ]; then
		./obr ../compiler/a.obe http://www.du.edu
	else
		./obr ../compiler/a.obe
	fi

	cd ../compiler
	let i-=1
done
