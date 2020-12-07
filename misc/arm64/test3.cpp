#include<iostream>

long Foo(long a, long b) {
	int c = 0;

 do {
	 	bool c = a == -5;
		a -= 1;
		std::wcout << c << std::endl;
	}
  while(a > 101);

	return c;
}

int main() {
	std::wcout << Foo(199,2) << std::endl;	
	return 0;
}
