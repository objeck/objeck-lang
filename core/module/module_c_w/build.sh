#/bin/sh
rm -rf lang_wrapper_w.o
rm -rf objeck_w.dll

g++ -O3 -std=c++17 -Wall -Wno-unused-function -c lang_wrapper_w.cpp
g++ -O3 -shared -o objeck_w.dll lang_wrapper_w.o ../../module/module.a ../../compiler/compiler.a ../../compiler/logger.a ../../vm/vm.a ../../vm/memory.a ../../vm/arch/jit/amd64/jit_common.a ../../vm/arch/win32/win32.o ../../vm/arch/jit/amd64/jit_amd_lp64.o -lssl -lcrypto -lz -pthread -lwsock32 -luserenv -lws2_32
