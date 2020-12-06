#include<iostream>

long Foo(long a, long b) {
	int c = 0;

//	while(a > 101) {
 do {
		c = 13;
		a--;
	}
  while(a > 101);

	return c;
}

int main() {
	std::wcout << Foo(199,2) << std::endl;	
	return 0;
}
