#include<iostream>

long Foo(long a, long b) {
	long c = 13;
	return a * c;
}

int main() {
	std::wcout << Foo(2,3) << std::endl;	
	return 0;
}
