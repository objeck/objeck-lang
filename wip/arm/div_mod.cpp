#include <iostream>

using namespace std;

int div_mod(int num, int deno) {
	if(deno == 0) {
		return deno;
	}
	
	int div = 0;
	num = num - deno;
	while(num > -1) {
		div = div + 1;
		num = num - deno;
	}
	
	cout << "div=" << div << ", remainder=" << (deno + num) << endl;
}

int main() {
	div_mod(50, 3);
}