#include<iostream>

long Foo(long a, long b) {
	int c = 0;
	if(a < 101) {
		c = 13;
	}
	else {
		c = 7;
	}
	return c;
}

int main() {
	std::wcout << Foo(199,2) << std::endl;	
	return 0;
}
