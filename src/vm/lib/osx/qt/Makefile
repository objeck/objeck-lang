QT_INCS=-I/usr/local/Qt4.7/mkspecs/macx-g++ -I. -I/Library/Frameworks/QtCore.framework/Versions/4/Headers -I/usr/include/QtCore -I/Library/Frameworks/QtGui.framework/Versions/4/Headers -I/usr/include/QtGui -I/usr/include -I. -I. -F/Library/Frameworks
QT_LIBS=-F/Library/Frameworks -L/Library/Frameworks -framework QtGui -framework QtCore -Wl,-headerpad_max_install_names,-undefined,dynamic_lookup,-compatibility_version,1.0,-current_version,1.0
# ARGS=-O3 -shared -fPIC $(QT_INCS) -Wno-deprecated -D_X64 -pthread -D_X64
ARGS=-g -D_DEBUG -shared -fPIC $(QT_INCS) -Wno-deprecated -D_X64 -pthread -D_X64

CC=g++
SRC=test.o
EXE=test.1.dylib 

$(EXE): $(SRC)
	$(CC) -m64 -pthread -dynamiclib $(QT_LIBS) -o $(EXE) $(SRC)

%.o: %.cpp
	$(CC) -m64 $(ARGS) -c $< 

clean:
	rm -f $(EXE).exe $(EXE) *.dylib *.o *~
