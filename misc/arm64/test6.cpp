#include<iostream>

void Foo(wchar_t* foo) {
	foo[1] = 'a';
	wchar_t v = foo[1];
	std::wcout << v << std::endl;
}

int main() {
	wchar_t foo[3] = {0};
	Foo(foo);
	return 0;
}
