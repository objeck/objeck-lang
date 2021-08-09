#/bin/sh
rm -rf *.o
rm -rf *.so
# g++ -O3 -Wno-unused-function -D_DIAG_LIB -std=c++11 -Wall -fPIC -c *$1.cpp ../../compiler/scanner.cpp ../../compiler/parser.cpp ../../compiler/context.cpp ../../compiler/tree.cpp ../../compiler/types.cpp
# g++ -O3 -shared -Wl,-soname,$1.so.1 -o $1.so *.o -lssl -lcrypto
g++ -g -Wno-unused-function -D_DIAG_LIB -std=c++11 -Wall -fPIC -c *$1.cpp ../../compiler/scanner.cpp ../../compiler/parser.cpp ../../compiler/context.cpp ../../compiler/tree.cpp ../../compiler/types.cpp
g++ -g -shared -Wl,-soname,$1.so.1 -o $1.so *.o -lssl -lcrypto -ldl -lz -pthread
