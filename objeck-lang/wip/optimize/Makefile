CC=g++
ARGS=-g -D_DEBUG -Wunused
# ARGS=-O3 -Wunused
SRC=optimizer.o test.o
EXE=optimize

$(EXE): $(SRC)
	$(CC) -m64 -o $(EXE) $(SRC)

%.o: %.cpp
	$(CC) -m64 $(ARGS) -c $< 

clean:
	rm -f $(EXE) *.o *~
