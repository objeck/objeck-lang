#include<iostream>

long Foo(long a, long b) {
	int c = 0;

	while(a < 101) {
		c = 13;
		a--;
	}

	return c;
}

int main() {
	std::wcout << Foo(199,2) << std::endl;	
	return 0;
}
