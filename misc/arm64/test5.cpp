#include<iostream>

void Foo(long a, long b) {
	do {
		a += 3;
	}
	while(a >= 10);

	std::wcout << a << std::endl;
}

int main() {
	Foo(10,13);
	return 0;
}
