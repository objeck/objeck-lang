#include<iostream>

void Foo(long a, long b) {
	double c = 13000.5;
	double d = c / 6.66;
	std::wcout << d << std::endl;
}

int main() {
	Foo(199,2);
	return 0;
}
