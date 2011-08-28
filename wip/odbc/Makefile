CC=g++
ARGS=-g -D_DEBUG -Wunused
# ARGS=-O3 -Wunused
SRC=odbc.o
EXE=odbc

$(EXE): $(SRC)
	$(CC) -m64 -lodbc -o $(EXE) $(SRC)

%.o: %.cpp
	$(CC) -m64 $(ARGS) -c $< 

clean:
	rm -f $(EXE) *.o *~
