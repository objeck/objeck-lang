#!/bin/bash 
i=53
until [  $i -lt 1 ]; do
	echo -------- prgm$i -----------

#	./obc -src test_src/prgm$i.obs -lib lang.obl,struct.obl -dest a.obe &> /dev/null
#	./obc -src test_src/prgm$i.obs -lib lang.obl,struct.obl -opt s3 -dest a.obe &> /dev/null
	./obc -src test_src/prgm$i.obs -lib lang.obl,struct.obl -opt s0 -dest a.obe

	cd ../vm
	if [ $i = 41 ]; then
		./obr ../compiler/a.obe 7
	elif [ $i = 37 ]; then
		./obr ../compiler/a.obe 13
	else
		./obr ../compiler/a.obe
	fi

	cd ../compiler
	let i-=1

done
