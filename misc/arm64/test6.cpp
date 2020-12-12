#include<iostream>

void Foo(char* foo, char c) {
	foo[1] = 13;
	char v = foo[1];
	std::wcout << v << std::endl;
}

int main() {
	char foo[3] = {0};
	Foo(foo, 10);
	return 0;
}
