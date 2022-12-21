CC=g++
# ARGS=-g -D_DEBUG -Wall -fPIC -Wno-unused-function
ARGS=-O3 -Wall -fPIC -Wno-unused-function
SRC=openssl.o
EXE=openssl.so

all: $(EXE)

$(EXE): $(SRC)
	$(CC) -shared -Wl,-soname,$1.so.1  -o $(EXE) $(SRC) -lssl -lcrypto

%.o: %.cpp
	$(CC) $(ARGS) -c $<

clean:
	rm -f $(EXE) *.so *.o *~
