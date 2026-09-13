# HTTP/2 and HTTP/3 libraries (nghttp2, ngtcp2 + nghttp3, AWS-LC) are linked
# statically, so obr needs no system copies. tools/deps/build_quic_deps.sh builds
# them; OBJECK_DEPS is its install prefix.
OBJECK_DEPS ?= $(HOME)/objeck-deps/linux-$(shell uname -m)
QUIC_LIBS=$(OBJECK_DEPS)/lib/libnghttp2.a $(OBJECK_DEPS)/lib/libngtcp2_crypto_boringssl.a $(OBJECK_DEPS)/lib/libngtcp2.a $(OBJECK_DEPS)/lib/libnghttp3.a $(OBJECK_DEPS)/lib/libssl.a $(OBJECK_DEPS)/lib/libcrypto.a

ARGS=-O3 -flto=auto -Wall -pthread -D_X64 -D_OBJECK_NATIVE_LIB_PATH -DOBJECK_HAS_NGHTTP2 -DOBJECK_HAS_NGTCP2 -I$(OBJECK_DEPS)/include -std=c++20 -mavx2 -Wno-unused-variable -Wno-unused-function -Wno-int-to-pointer-cast -Wno-unused-result -MMD -MP

SRC=common.o dispatch.o interpreter.o loader.o vm.o posix_main.o 
OBJ_LIBS=jit_amd_lp64.a memory.a
MEM_PATH=arch
JIT_PATH=arch/jit/amd64
EXE= obr

$(EXE): $(SRC) $(OBJ_LIBS)
	$(CXX) -m64 -flto=auto -o $(EXE) $(SRC) $(OBJ_LIBS) -lmbedtls -lmbedx509 -lmbedcrypto $(QUIC_LIBS) -ldl -lz -pthread

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
	rm -f $(EXE).exe $(EXE) *.exe *.o *.d *~
