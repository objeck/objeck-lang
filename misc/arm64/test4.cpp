#include <iostream>

long Foo(	size_t a, int b, int c, int d, int e,
					size_t f, int g, int h, int i, int j) 
{
	long x = 101;
	long y = x - d;
	return y;
}

int main() {
	const long foo = Foo(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
	std::wcout << foo << std::endl;
	
	return 0;
}
