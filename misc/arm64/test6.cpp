#include<iostream>

void Foo(wchar_t* foo, wchar_t c) {
	foo[1] = c;
	wchar_t v = foo[1];
	std::wcout << v << std::endl;
}

int main() {
	wchar_t foo[3] = {0};
	Foo(foo, 'x');
	return 0;
}
