#include <iostream>

int foo(long a, long b) {
	a = 90000;
	b = 30000;

	return a + b;
}

int main() {
	foo(1,2);	
	return 0;
}
