#include <iostream>

using namespace std;

typedef enum _Instr {
	lit,
	add,
	sub,
	end
} Instr;
	
int main() {
	int ip = 0;
	Instr instrs[] = {lit, lit, add, lit, sub, end};

	while(true) {
		switch(instrs[ip++]) {
			case lit: {
				cout << "lit" << endl;
			}
				break;

			case add: {
				cout << "add" << endl;
			}
				break;
			
			case sub: {
				cout << "sub" << endl;
			}
				break;

			case end: {
				cout << "end" << endl;
				return 0;
			}
				break;
		}
	}

	return 0;
}
