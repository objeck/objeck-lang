#include<iostream>

long Foo(long a, long b) {
	long c = a % 4;
	return c;
}

int main() {
	std::wcout << Foo(7,3) << std::endl;	
	return 0;
}
