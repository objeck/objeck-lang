#include<iostream>

void Foo(long a, long b) {
	double c = 13000.5;
	double d = c / 6.66;
	bool i = c >= d;

	std::wcout << i << std::endl;
}

int main() {
	Foo(199,2);
	return 0;
}
