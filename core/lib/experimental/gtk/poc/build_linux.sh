#/bin/sh

#
# packages: apt-get install libgtk-3-dev
#

if [ ! -z "$1" ] && [ "$1" = "clib" ]; then
	rm -rf *.o *.so
	g++ -O3 -Wall -std=c++11 -fPIC -c gtk3_test.cpp `pkg-config --cflags gtk+-3.0 --libs gtk+-3.0` -Wno-unused-function 
	g++ -O3 -shared -Wl,-soname,libobjk_gtk3_test.so.1 -o libobjk_gtk3_test.so *.o 
	cp libobjk_gtk3_test.so ../../../../release/deploy/lib/native
fi

rm -f *.obe *.obl

export PATH=$PATH:../../../../release/deploy/bin
export OBJECK_LIB_PATH=../../../../release/deploy/lib

obc -src gtk3_test.obs -tar lib -dest ../../../../release/deploy/lib/gtk3_test.obl
obc -src app_test.obs -lib gtk3_test

if [ ! -z "$2" ] && [ "$2" = "brun" ]; then
	obr app_test
fi