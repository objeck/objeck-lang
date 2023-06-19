CC=g++
ARGS=-O3 -Wall -std=c++17 -D_MODULE -D_ARM64 -Wno-unused-function -Wno-maybe-uninitialized
SRC=types.o tree.o scanner.o parser.o linker.o context.o intermediate.o optimization.o emit.o compiler.o 
LOGGER_PATH=../shared
OBJ_LIBS=logger.a
AR=ar
LIB=compiler.a

$(LIB): $(SRC) $(OBJ_LIBS)
	$(AR) -cvq $(LIB) $(SRC)
	cp $(LIB) ../module

%.o: %.cpp
	$(CC) $(ARGS) -c $< 

logger.a:
	cd $(LOGGER_PATH); make -f make/Makefile.arm64

clean:
	cd $(LOGGER_PATH); make -f make/Makefile.arm64 clean
	rm -f $(LIB) *.o *~
