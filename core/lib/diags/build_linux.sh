#/bin/sh
rm -rf *.o
rm -rf *.so

g++ -O3 -D_DIAG_LIB -std=c++11 -Wall -Wno-unused-function -fPIC -c *$1.cpp ../../compiler/scanner.cpp ../../compiler/parser.cpp ../../compiler/context.cpp ../../compiler/tree.cpp ../../compiler/types.cpp
g++ -O3 -shared -Wl,-soname,$1.so.1 -o $1.so *.o -lssl -lcrypto -ldl -lz -pthread

# g++ -f -D_DIAG_LIB -std=c++11 -Wall -Wno-unused-function -fPIC -c *$1.cpp ../../compiler/scanner.cpp ../../compiler/parser.cpp ../../compiler/context.cpp ../../compiler/tree.cpp ../../compiler/types.cpp
# g++ -g -shared -Wl,-soname,$1.so.1 -o $1.so *.o -lssl -lcrypto -ldl -lz -pthread