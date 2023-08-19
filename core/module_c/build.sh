#/bin/sh
rm -rf *.o
rm -rf *.dll

g++ -O3 -std=c++17 -Wall -Wno-unused-function -c lang_wrapper.cpp
g++ -O3 -shared -o objeck.dll *.o ../module/module.a ../compiler/compiler.a ../compiler/logger.a ../vm/vm.a ../vm/memory.a ../vm/arch/jit/amd64/jit_common.a ../vm/arch/win32/win32.o ../vm/arch/jit/amd64/jit_amd_lp64.o -lssl -lcrypto -lz -pthread -lwsock32 -luserenv -lws2_32 -lz
