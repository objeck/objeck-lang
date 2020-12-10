#include<iostream>

void Foo(double a, long b) {
	double c = sin(45.5);
	double d = 13.5;
	double e = c + d;
	a = d;
	b = e;
	std::wcout << b << std::endl;
}

int main() {
	Foo(199,2);
	return 0;
}
