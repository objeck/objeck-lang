#include <iostream>

using namespace std;

typedef void* Instr;

int main() {
	int ip = 0;
	Instr instrs[] = {&&lit, &&lit, &&add, &&lit, &&sub, &&end};

	goto *instrs[ip++];

	lit: {
		cout << "lit" << endl;
		goto *instrs[ip++];

	}

	add: {
		cout << "add" << endl;
		goto *instrs[ip++];
	}
	
	sub: {
		cout << "sub" << endl;
		goto *instrs[ip++];
	}

	end: {
		cout << "end" << endl;
		return 0;
	}

	return 0;
}
