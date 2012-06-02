CC=g++
ARGS=-g -D_DEBUG -Wall
# ARGS=-O3 -Wall
SRC=fcgi_main.o
EXE=fcgi

$(EXE): $(SRC)
	$(CC) -m32 -o $(EXE) $(SRC) -lfcgi -ldl

%.o: %.cpp
	$(CC) -m32 $(ARGS) -c $< 

clean:
	rm -f $(EXE) *.o *~
