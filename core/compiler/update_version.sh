#!/bin/sh
make -f make/Makefile.SYS.32 clean
make -f make/Makefile.SYS.32
./obc -src lib_src/lang.obs -tar lib -opt s2 -dest ../lib/lang.obl 
make -f make/Makefile.32 clean
make -f make/Makefile.32
./obc -src lib_src/collect.obs -tar lib -opt s3 -dest ../lib/collect.obl
./obc -src lib_src/xml.obs -lib collect.obl -tar lib -opt s3 -dest ../lib/xml.obl
./obc -src lib_src/json.obs -lib collect.obl -tar lib -opt s3 -dest ../lib/json.obl
./obc -src lib_src/encrypt.obs -lib collect.obl -tar lib -opt s3 -dest ../lib/encrypt.obl
./obc -src lib_src/odbc.obs -lib collect.obl -tar lib -opt s3 -dest ../lib/odbc.obl
./obc -src lib_src/regex.obs -lib collect.obl -tar lib -opt s3 -dest ../lib/regex.obl
./obc -src lib_src/fcgi.obs -lib collect.obl,json.obl -tar lib -opt s3 -dest ../lib/fcgi.obl
./obc -src lib_src/csv.obs -tar lib -lib collect.obl -opt s3 -dest ../lib/csv.obl
./obc -src lib_src/query.obs -tar lib -lib regex.obl,collect.obl -opt s3 -dest ../lib/query.obl
./obc -src lib_src/sdl.obs -lib collect.obl -tar lib -dest ../lib/sdl.obl
./obc -src lib_src/game.obs -lib collect.obl,sdl.obl -tar lib -dest ../lib/game.obl