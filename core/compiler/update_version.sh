#!/bin/sh
make -f make/Makefile.SYS.32 clean
make -f make/Makefile.SYS.32
./obc -src lib_src/lang.obs -tar lib -opt s2 -dest ../lib/lang.obl 
make -f make/Makefile.32 clean
make -f make/Makefile.32
./obc -src lib_src/collect.obs -tar lib -opt s3 -dest ../lib/collect.obl
./obc -src lib_src/gen_collect.obs -tar lib -opt s3 -dest ../lib/gen_collect.obl
./obc -src lib_src/xml.obs -lib gen_collect.obl -tar lib -opt s3 -dest ../lib/xml.obl
./obc -src lib_src/net_misc.obs -tar lib -lib xml.obl,gen_collect.obl -opt s3 -dest ../lib/net_misc.obl
./obc -src lib_src/json.obs -lib gen_collect.obl -tar lib -opt s3 -dest ../lib/json.obl
./obc -src lib_src/encrypt.obs -tar lib -opt s3 -dest ../lib/encrypt.obl
./obc -src lib_src/odbc.obs -lib collect.obl -tar lib -opt s3 -dest ../lib/odbc.obl
./obc -src lib_src/regex.obs -lib gen_collect.obl -tar lib -opt s3 -dest ../lib/regex.obl
./obc -src lib_src/fcgi.obs -lib net_misc.obl,collect.obl,json.obl -tar lib -opt s3 -dest ../lib/fcgi.obl
./obc -src lib_src/csv.obs -tar lib -lib gen_collect.obl -opt s3 -dest ../lib/csv.obl
./obc -src lib_src/query.obs -tar lib -lib xml.obl,regex.obl,net_misc.obl,gen_collect.obl -opt s3 -dest ../lib/query.obl
./obc -src lib_src/sdl2.obs -tar lib -dest ../lib/sdl2.obl
./obc -src lib_src/sdl_game.obs -lib collect.obl,sdl2.obl -tar lib -dest ../lib/sdl_game.obl
