ARGS=-O3 -Wall -std=c++17 -D_MODULE -D_ARM64 -Wno-unused-function -Wno-sequence-point
SRC=types.o tree.o scanner.o parser.o linker.o context.o intermediate.o optimization.o emit.o compiler.o 
LOGGER_PATH=../shared
OBJ_LIBS=sys.a
AR=ar
LIB=compiler.a

$(LIB): $(SRC) $(OBJ_LIBS)
	$(AR) -cvq $(LIB) $(SRC)
	cp $(LIB) ../module

%.o: %.cpp
	$(CXX) $(ARGS) -c $< 

sys.a:
	cd $(LOGGER_PATH); $(MAKE) -f make/Makefile.arm64

clean:
	cd $(LOGGER_PATH); $(MAKE) -f make/Makefile.arm64 clean
	rm -f $(LIB) *.o *~
