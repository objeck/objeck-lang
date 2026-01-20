ARGS=-O3 -Wall -std=c++17 -D_ARM64 -D_OBJECK_NATIVE_LIB_PATH -Wno-unused-variable -Wno-unused-function -Wno-int-to-pointer-cast

SRC=common.o dispatch.o interpreter.o loader.o vm.o posix_main.o 
OBJ_LIBS=jit_arm_a64.a memory.a
MEM_PATH=arch
JIT_PATH=arch/jit/arm64
AR=ar
LIB=vm.a

$(LIB): $(SRC) $(OBJ_LIBS)
	$(AR) -cvq $(LIB) $(SRC)
	cp $(LIB) ../module

memory.a:
	cd $(MEM_PATH); $(MAKE) -f make/Makefile.arm64
	
jit_arm_a64.a:
	cd $(JIT_PATH); $(MAKE) -f make/Makefile.arm64
	
%.o: %.cpp
	$(CXX) $(ARGS) -c $< 

clean:
	cd $(MEM_PATH); $(MAKE) clean -f make/Makefile.arm64
	cd $(JIT_PATH); $(MAKE) clean -f make/Makefile.arm64
	rm -f $(LIB) *.o *~
