clang++ $1.cpp
objdump -d a.out > $1.dbg
rm a.out
