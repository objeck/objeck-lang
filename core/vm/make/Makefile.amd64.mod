# HTTP/2 and HTTP/3 (nghttp2, ngtcp2 + nghttp3, AWS-LC) are compiled into the
# module the same way they are into obr: tools/deps/build_quic_deps.sh builds
# them, and OBJECK_DEPS is its install prefix. Without these the module fell
# back to the #else stubs, and obi and the embedding API had neither (#897).
OBJECK_DEPS ?= $(HOME)/objeck-deps/linux-$(shell uname -m)
ARGS=-O3 -Wall -D_MODULE -D_X64 -D_OBJECK_NATIVE_LIB_PATH -DOBJECK_HAS_NGHTTP2 -DOBJECK_HAS_NGTCP2 -I$(OBJECK_DEPS)/include -std=c++20 -mavx2 -Wno-unused-variable -Wno-unused-function -Wno-int-to-pointer-cast -Wno-unused-result -MMD -MP

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
	
% : %.mod
%.o : %.mod

%.o: %.cpp
	$(CXX) -m64 $(ARGS) -c $< 

# Auto-generated header dependencies (see -MMD -MP in ARGS). Without these make
# never learned that the objects include ../shared/version.h and friends, so a
# header edit silently relinked stale .o files -- 'make clean' was mandatory.
-include $(wildcard *.d)

clean:
	cd $(MEM_PATH); $(MAKE) clean -f make/Makefile.amd64
	cd $(JIT_PATH); $(MAKE) clean -f make/Makefile.amd64
	rm -f $(LIB) *.o *.d *~
