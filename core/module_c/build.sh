#/bin/sh
rm -rf *.o
rm -rf *.dll

g++ -O3 -Wno-int-to-pointer-cast -Wall -std=c++17 -fPIC -c *$1.cpp -Wno-unused-function -Wno-address
g++ -O3 -shared -o objeck.dll *.o ../module/module.a ../compiler/compiler.a ../compiler/logger.a ../vm/vm.a ../vm/memory.a ../vm/arch/jit/amd64/jit_common.a ../vm/arch/win32/win32.o ../vm/arch/jit/amd64/jit_amd_lp64.o -lssl -lcrypto -lz -pthread -lwsock32 -luserenv -lws2_32 -lz