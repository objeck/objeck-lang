#include<iostream>

long Foo(long a, long b) {
	return a - b;
}

int main() {
	std::wcout << Foo(2,3) << std::endl;	
	return 0;
}
