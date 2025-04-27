ARGS=-O3 -Wall -Wno-unused-function -D_X64 -std=c++20 -mavx2 -Wno-sequence-point

SRC=types.o tree.o scanner.o parser.o linker.o context.o intermediate.o optimization.o emit.o compiler.o posix_main.o
OBJ_LIBS=sys.a objeck.res
LOGGER_PATH=../shared
EXE=obc

$(EXE): $(SRC) $(OBJ_LIBS) 
	$(CXX) -m64 -o $(EXE) $(SRC) $(OBJ_LIBS) -lz

%.o: %.cpp
	$(CXX) -m64 $(ARGS) -c $< 

sys.a:
	cd $(LOGGER_PATH); $(MAKE) -f make/Makefile.msys2-ucrt.amd64

objeck.res:
	windres vs/objeck.rc -O coff -o objeck.res -D_MSYS2_CLANG

clean:
	cd $(LOGGER_PATH); $(MAKE) -f make/Makefile.msys2-ucrt.amd64 clean
	rm -f $(EXE) $(EXE).exe *.o *.res *~
