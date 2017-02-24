#include <iostream>

using namespace std;

int r3_mod(int r0, int r1) {
	if(r1 == 0) {
		return r1;
	}
	
	int r3 = 0;
	r0 = r0 - r1;
	while(r0 > -1) {
		r3 = r3 + 1;
		r0 = r0 - r1;
	}	
}

int main() {
	r3_mod(50, 3);
}