#include<iostream>

long Foo(long a, long b) {
	long c = a >= 101;
	return c;
}

int main() {
	std::wcout << Foo(199,2) << std::endl;	
	return 0;
}
