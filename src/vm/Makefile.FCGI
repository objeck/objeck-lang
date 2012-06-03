##############
# For OSX only
##############
# ARGS=-pthread -mstackrealign -O3
# ARGS=-pthread -D_TIMING -mstackrealign -O3
# ARGS=-pthread -mstackrealign -g -D_DEBUG -Wunused
# ARGS=-g -mstackrealign -Wunused -D_DEBUG -pthread -D_DEBUGGER

##############
# For Linux
##############
# ARGS=-pthread -O3 -Wall
# ARGS=-g -Wall -pthread
ARGS=-g -D_DEBUG -D_JIT_SERIAL -Wall -pthread
# ARGS=-g -D_DEBUG -Wunused -pthread
# ARGS=-O3 -pthread

CC=g++
SRC=common.o interpreter.o loader.o fcgi_main.o
OBJ_LIBS=jit_intel_lp32.a memory.a
MEM_PATH=os/posix
JIT_PATH=jit/ia32
EXE=obr_fcgi

$(EXE): $(SRC) $(OBJ_LIBS) $(FCGI)
	$(CC) -m32 -o $(EXE) $(SRC) $(OBJ_LIBS) -lfcgi -ldl -pthread

memory.a:
	cd $(MEM_PATH); make	
	
jit_intel_lp32.a:
	cd $(JIT_PATH); make	
	
%.o: %.cpp
	$(CC) -m32 $(ARGS) -c $< 

clean:
	cd $(MEM_PATH); make clean
	cd $(JIT_PATH); make clean
	rm -f $(EXE).exe $(EXE) *.exe *.o *~

