ARGS=-O3 -Wall -D_MODULE -std=c++17 -Wno-unused-function -Wno-sequence-point
SRC=types.o tree.o scanner.o parser.o linker.o context.o intermediate.o optimization.o emit.o compiler.o 
OBJ_LIBS=logger.a
LOGGER_PATH=../shared
AR=ar
LIB=compiler.a

$(LIB): $(SRC) $(OBJ_LIBS)
	$(AR) -cvq $(LIB) $(SRC)
	cp $(LIB) ../module

%.o: %.cpp
	$(CXX) -m64 $(ARGS) -c $< 

logger.a:
	cd $(LOGGER_PATH); $(MAKE) -f make/Makefile.amd64

clean:
	cd $(LOGGER_PATH); $(MAKE) -f make/Makefile.amd64 clean
	rm -f $(LIB) *.o *~
