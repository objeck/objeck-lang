ARGS=-O3 -Wall -D_MODULE  -D_MSYS2 -D_X64 -D_OBJECK_NATIVE_LIB_PATH -std=c++17 -mavx2 -Wno-uninitialized -Wno-unused-function -Wno-unused-variable -Wno-int-to-pointer-cast -Wno-unknown-pragmas -Wno-unused-but-set-variable -Wno-dangling-pointer 

SRC=common.o interpreter.o loader.o vm.o posix_main.o 
OBJ_LIBS=win32.a jit_amd_lp64.a memory.a
MEM_PATH=arch
JIT_PATH=arch/jit/amd64
WIN32_PATH=arch/win32
AR=ar
LIB=vm.a

$(LIB): $(SRC) $(OBJ_LIBS)
	$(AR) -cvq $(LIB) $(SRC)
	cp $(LIB) ../module

memory.a:
	cd $(MEM_PATH); $(MAKE) -f make/Makefile.msys2-ucrt.amd64
	
jit_amd_lp64.a:
	cd $(JIT_PATH); $(MAKE) -f make/Makefile.msys2-ucrt.amd64

win32.a:
	cd $(WIN32_PATH); $(MAKE) -f make/Makefile.msys2-ucrt.amd64
	
%.o: %.cpp
	$(CXX) -m64 $(ARGS) -c $< 

clean:
	cd $(MEM_PATH); $(MAKE) clean -f make/Makefile.msys2-ucrt.amd64
	cd $(JIT_PATH); $(MAKE) clean -f make/Makefile.msys2-ucrt.amd64
	cd $(WIN32_PATH); $(MAKE) clean -f make/Makefile.msys2-ucrt.amd64
	rm -f $(LIB) *.o *~
