#!/bin/sh

make -f make/Makefile.sys.arm64 clean
make -f make/Makefile.sys.arm64
./sys_obc -src lib_src/lang.obs -tar lib -opt s2 -dest ../lib/lang.obl -strict

make -f make/Makefile.arm64 clean
make -f make/Makefile.arm64

./obc -src lib_src/gen_collect.obs -lib ../lib/lang -tar lib -opt s3 -dest ../lib/gen_collect.obl -strict
./obc -src lib_src/json_stream.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/json_stream.obl
./obc -src lib_src/cipher.obs -tar lib -opt s3 -dest ../lib/cipher.obl
./obc -src lib_src/onnx.obs -tar lib -opt s3 -dest ../lib/ml.obl
./obc -src lib_src/lame.obs -tar lib -opt s3 -dest ../lib/lame.obl
./obc -src lib_src/diags.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/diags.obl
./obc -src lib_src/xml.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/xml.obl
./obc -src lib_src/json.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/json.obl
./obc -src lib_src/regex.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/regex.obl
./obc -src lib_src/csv.obs -tar lib -lib gen_collect -opt s3 -dest ../lib/csv.obl
./obc -src lib_src/ml.obs -lib gen_collect,csv -tar lib -opt s3 -dest ../lib/ml.obl
./obc -src lib_src/net_common.obs,lib_src/net.obs,lib_src/net_secure.obs -tar lib -lib gen_collect,cipher -opt s3 -dest ../lib/net.obl
./obc -src lib_src/net_server.obs -tar lib -lib net,json,gen_collect,cipher -opt s3 -dest ../lib/net_server.obl
./obc -src lib_src/json_rpc.obs -tar lib -lib json,net -opt s3 -dest ../lib/json_rpc.obl
./obc -src lib_src/misc.obs -lib gen_collect,net,json -tar lib -opt s3 -dest ../lib/misc.obl
./obc -src lib_src/rss.obs -tar lib -lib xml,gen_collect,net,cipher -opt s3 -dest ../lib/rss.obl
./obc -src lib_src/query.obs -tar lib -lib net,regex,csv,xml,json,misc -opt s3 -dest ../lib/query.obl
./obc -src lib_src/odbc.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/odbc.obl
./obc -src lib_src/openai.obs -lib json,net,net_server,cipher,misc -tar lib -opt s3 -dest ../lib/openai.obl
./obc -src lib_src/gemini.obs -lib misc,json,net,net_server,cipher -tar lib -opt s3 -dest ../lib/gemini.obl
./obc -src lib_src/ollama.obs -lib net,json,cipher,misc -tar lib -opt s3 -dest ../lib/ollama.obl
./obc -src lib_src/sdl2.obs -tar lib -dest ../lib/sdl2.obl
./obc -src lib_src/sdl_game.obs -lib gen_collect,json,sdl2 -tar lib -dest ../lib/sdl_game.obl
