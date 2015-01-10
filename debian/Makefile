all: build install

build:
	cd compiler; cp Makefile.64 Makefile; make
	cd vm; cp Makefile.64 Makefile; cp os/posix/Makefile.64 os/posix/Makefile; cp jit/amd64/Makefile.64 jit/amd64/Makefile; make
	cd vm; cp Makefile.obd64 Makefile
	cd vm/debugger; cp Makefile.64 Makefile; make
	cd utilities; cp Makefile.64 Makefile; make

install:
	mkdir -p ../bin
	cd utilities; cp obu ../../bin
	cd compiler; cp obc ../../bin
	cd vm; cp obr ../../bin
	cd vm/debugger; cp obd ../../../bin

clean:
	cd utilities; make clean
	cd compiler; make clean
	cd vm; make clean
	cd vm/debugger; make clean
