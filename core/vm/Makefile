ARGS=-O3 -Wall -std=c++20 -mavx2 -pthread -D_MSYS2 -D_X64 -D_WIN32 -D_OBJECK_NATIVE_LIB_PATH -Wno-uninitialized -Wno-unknown-pragmas -Wno-unused-function -Wno-unused-variable -Wno-int-to-pointer-cast -Wno-unused-but-set-variable -Wno-dangling-pointer

SRC=common.o dispatch.o interpreter.o loader.o vm.o win_main.o
OBJ_LIBS=win32.a jit_amd_lp64.a memory.a objeck.res
MEM_PATH=arch
JIT_PATH=arch/jit/amd64
WIN32_PATH=arch/win32
EXE=obr

$(EXE): $(SRC) $(OBJ_LIBS)
	$(CXX) -m64 -o $(EXE) $(SRC) $(OBJ_LIBS) -lssl -lcrypto -lz -pthread -lwsock32 -luserenv -lws2_32 -lbcrypt

memory.a:
	cd $(MEM_PATH); $(MAKE) -f make/Makefile.msys2-ucrt.amd64
	
jit_amd_lp64.a:
	cd $(JIT_PATH); $(MAKE) -f make/Makefile.msys2-ucrt.amd64

win32.a:
	cd $(WIN32_PATH); $(MAKE) -f make/Makefile.msys2-ucrt.amd64	

objeck.res:
	windres vs/objeck.rc -O coff -o objeck.res -D_MSYS2_CLANG

%.o: %.cpp
	$(CXX) -m64 $(ARGS) -c $< 

clean:
	cd $(MEM_PATH); $(MAKE) -f make/Makefile.msys2-ucrt.amd64 clean
	cd $(JIT_PATH); $(MAKE) -f make/Makefile.msys2-ucrt.amd64 clean
	cd $(WIN32_PATH); $(MAKE) -f make/Makefile.msys2-ucrt.amd64 clean
	rm -f $(EXE).exe $(EXE) *.exe *.dll *.o *~ *.res
