ARGS=-O3 -Wall -D_MODULE -D_X64 -D_OBJECK_NATIVE_LIB_PATH -std=c++17 -mavx2 -Wno-unused-variable -Wno-unused-function -Wno-int-to-pointer-cast -Wno-unused-result

SRC=common.o dispatch.o interpreter.o loader.o vm.o posix_main.o 
OBJ_LIBS=jit_amd_lp64.a memory.a
MEM_PATH=arch
JIT_PATH=arch/jit/amd64
AR=ar
LIB=vm.a

$(LIB): $(SRC) $(OBJ_LIBS)
	$(AR) -cvq $(LIB) $(SRC)
	cp $(LIB) ../module

memory.a:
	cd $(MEM_PATH); $(MAKE) -f make/Makefile.amd64
	
jit_amd_lp64.a:
	cd $(JIT_PATH); $(MAKE) -f make/Makefile.amd64
	
%.o: %.cpp
	$(CXX) -m64 $(ARGS) -c $< 

clean:
	cd $(MEM_PATH); $(MAKE) clean -f make/Makefile.amd64
	cd $(JIT_PATH); $(MAKE) clean -f make/Makefile.amd64
	rm -f $(LIB) *.o *~
