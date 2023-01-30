#!/bin/sh

make -f make/Makefile.sys.amd64 clean
make -f make/Makefile.sys.amd64
./obc -src lib_src/lang.obs -tar lib -opt s2 -dest ../lib/lang.obl -strict

make -f make/Makefile.amd64 clean
make -f make/Makefile.amd64

./obc -src lib_src/gen_collect.obs -lib ../lib/lang -tar lib -opt s3 -dest ../lib/gen_collect.obl -strict
./obc -src lib_src/diags.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/diags.obl
./obc -src lib_src/misc.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/misc.obl
./obc -src lib_src/xml.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/xml.obl
./obc -src lib_src/net_common.obs,lib_src/net.obs,lib_src/net_secure.obs -tar lib -lib gen_collect -opt s3 -dest ../lib/net.obl
./obc -src lib_src/rss.obs -tar lib -lib xml,gen_collect,net -opt s3 -dest ../lib/rss.obl
./obc -src lib_src/json.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/json.obl
./obc -src lib_src/encrypt.obs -tar lib -opt s3 -dest ../lib/encrypt.obl
./obc -src lib_src/odbc.obs -lib collect -tar lib -opt s3 -dest ../lib/odbc.obl
./obc -src lib_src/regex.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/regex.obl
./obc -src lib_src/csv.obs -tar lib -lib gen_collect -opt s3 -dest ../lib/csv.obl
./obc -src lib_src/query.obs -tar lib -lib csv,xml,misc,regex,gen_collect -opt s3 -dest ../lib/query.obl
./obc -src lib_src/sdl2.obs -tar lib -dest ../lib/sdl2.obl
./obc -src lib_src/sdl_game.obs -lib gen_collect,json,sdl2 -tar lib -dest ../lib/sdl_game.obl
