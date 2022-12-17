CC=g++
# ARGS=-g -D_DEBUG -Wall -fPIC -Wno-unused-function
ARGS=-O3 -Wall -fPIC -Wno-unused-function
SRC=odbc.o
EXE=odbc.so

all: $(EXE)

$(EXE): $(SRC)
	$(CC) -shared -Wl,-soname,$1.so.1  -o $(EXE) $(SRC) -lodbc

%.o: %.cpp
	$(CC) $(ARGS) -c $< -lodbc 

clean:
	rm -f $(EXE) *.so *.o *~
