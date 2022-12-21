CC=g++
ARGS=-g -D_DEBUG -Wall -fPIC -Wno-unused-function
# ARGS=-O3 -Wall -fPIC -Wno-unused-function
SRC=fcgi.o
EXE=fcgi.so

all: $(EXE)

$(EXE): $(SRC)
	$(CC) -shared -Wl,-soname,$1.so.1  -o $(EXE) $(SRC) -lodbc

%.o: %.cpp
	$(CC) $(ARGS) -c $< -lfcgi -luuid

clean:
	rm -f $(EXE) *.so *.o *~
