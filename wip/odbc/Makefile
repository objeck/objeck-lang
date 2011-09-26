CC=g++
ARGS=-g -D_DEBUG -Wunused
# ARGS=-O3 -Wunused
SRC=odbc.o
EXE=odbc

$(EXE): $(SRC)
	$(CC) -m32 -o $(EXE) $(SRC)

%.o: %.cpp
	$(CC) -m32 $(ARGS) -c $< 

clean:
	rm -f $(EXE) *.o *~
