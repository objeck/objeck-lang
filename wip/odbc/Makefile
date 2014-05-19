CC=g++
ARGS=-g -D_DEBUG -Wunused
# ARGS=-O3 -Wunused
SRC=odbc.o
EXE=odbc

$(EXE): $(SRC)
	$(CC) -o $(EXE) $(SRC) -lodbc

%.o: %.cpp
	$(CC) $(ARGS) -c $< 

clean:
	rm -f $(EXE) *.o *~
