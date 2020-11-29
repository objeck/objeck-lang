#include<iostream>

long Foo(long a, long b) {
	long c = a >> 5;
	return c;
}

int main() {
	std::wcout << Foo(2,3) << std::endl;	
	return 0;
}
