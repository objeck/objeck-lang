#include <iostream>

using namespace std;

enum Register {
	r0,
	r1,
	r2,
	r3,
	r4,
	r5,
	r6,
	r7
};

// src => r0; dest => r1
void div(Register src, Register dest) {
	cout << "src=r" << src << ", dest=r" << dest << endl << endl;

	cout << "r0 -> tmp0" << endl;
	cout << "r1 -> tmp1" << endl;

	if(src == r0 && dest == r1) {
		cout << "call <div>" << endl;
		cout << "r0 -> dest" << endl;
	}
	else {
		// swap
		if(src == r1 && dest == r0) {
			cout << "tmp1 -> r0" << endl;
			cout << "tmp0 -> r1" << endl;
			// call and save result
			cout << "call <div>" << endl;
			cout << "r0 -> dest" << endl;
		}

		if(src == r1) {
			cout << "tmp1 -> r0" << endl;
			if(dest != r1) {
				cout << "dest -> r1" << endl;
			}
			// call and save result
			cout << "call <div>" << endl;
			cout << "r0 -> dest" << endl;
		}
		
		if(dest == r0) {
			if(src != r0) {
				cout << "src -> r0" << endl;
			}
			cout << "tmp0 -> r1" << endl;
			// call and save result
			cout << "call <div>" << endl;
			cout << "r0 -> dest" << endl;
		}

	}

	if(dest != r0) {
		cout << "tmp0 -> r0" << endl;
	}
	
	if(dest != r1) {
		cout << "tmp1 -> r1" << endl;
	}
}

int main() {
	div(r1, r0);
	cout << "---" << endl;
	div(r0, r1);
	cout << "---" << endl;
	div(r2, r3);
	cout << "---" << endl;
	div(r0, r3);
	cout << "---" << endl;
	div(r3, r1);
}

