#include<iostream>

void Foo(long a, long b) {
	double c = 13000.5;
	double d = c / 6.66;
	long i = c * d;

/*
	int c = 13;
	double d = a * b;
*/
	std::wcout << i << std::endl;
}

int main() {
	Foo(199,2);
	return 0;
}
