ARGS=-O3 -Wall -D_MODULE -std=c++20 -mavx2 -Wno-unused-function -Wno-sequence-point -MMD -MP
SRC=types.o tree.o scanner.o parser.o linker.o context.o intermediate.o optimization.o emit.o compiler.o 
OBJ_LIBS=sys.a
LOGGER_PATH=../shared
AR=ar
LIB=compiler.a

$(LIB): $(SRC) $(OBJ_LIBS)
	$(AR) -cvq $(LIB) $(SRC)
	cp $(LIB) ../module

% : %.mod
%.o : %.mod

%.o: %.cpp
	$(CXX) -m64 $(ARGS) -c $< 

sys.a:
	cd $(LOGGER_PATH); $(MAKE) -f make/Makefile.amd64

# Auto-generated header dependencies (see -MMD -MP in ARGS). Without these make
# never learned that the objects include ../shared/version.h and friends, so a
# header edit silently relinked stale .o files -- 'make clean' was mandatory.
-include $(wildcard *.d)

clean:
	cd $(LOGGER_PATH); $(MAKE) -f make/Makefile.amd64 clean
	rm -f $(LIB) *.o *.d *~
