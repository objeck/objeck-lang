!/bin/sh
make -f make/Makefile.SYS.64 clean
make -f make/Makefile.SYS.64
./obc -src lib_src/lang.obs -tar lib -opt s2 -dest ../lib/lang.obl 
make -f make/Makefile.64 clean
make -f make/Makefile.64
./obc -src lib_src/gen_collect.obs -tar lib -opt s3 -dest ../lib/gen_collect.obl
./obc -src lib_src/misc.obs -lib gen_collect.obl -tar lib -opt s3 -dest ../lib/misc.obl
./obc -src lib_src/xml.obs -lib gen_collect.obl -tar lib -opt s3 -dest ../lib/xml.obl
./obc -src lib_src/net.obs -tar lib -lib gen_collect.obl -opt s3 -dest ../lib/net.obl
./obc -src lib_src/rss.obs -tar lib -lib xml.obl,gen_collect.obl,net.obl -opt s3 -dest ../lib/rss.obl
./obc -src lib_src/json.obs -lib gen_collect.obl -tar lib -opt s3 -dest ../lib/json.obl
./obc -src lib_src/encrypt.obs -tar lib -opt s3 -dest ../lib/encrypt.obl
./obc -src lib_src/odbc.obs -lib collect.obl -tar lib -opt s3 -dest ../lib/odbc.obl
./obc -src lib_src/regex.obs -lib gen_collect.obl -tar lib -opt s3 -dest ../lib/regex.obl
./obc -src lib_src/fcgi.obs -lib gen_collect,net,misc -tar lib -opt s3 -dest ../lib/fcgi.obl
./obc -src lib_src/fcgi_web.obs -lib fcgi.obl,gen_collect,net,misc -tar lib -opt s3 -dest ../lib/fcgi_web.obl
./obc -src lib_src/csv.obs -tar lib -lib gen_collect.obl -opt s3 -dest ../lib/csv.obl
./obc -src lib_src/query.obs -tar lib -lib csv.obl,xml.obl,misc.obl,regex.obl,gen_collect.obl -opt s3 -dest ../lib/query.obl
./obc -src lib_src/sdl2.obs -tar lib -dest ../lib/sdl2.obl
./obc -src lib_src/sdl_game.obs -lib gen_collect.obl,json.obl,sdl2.obl -tar lib -dest ../lib/sdl_game.obl
