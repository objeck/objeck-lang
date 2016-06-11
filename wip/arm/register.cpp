#include <iostream>

using namespace std;

enum Reg {
	r0,
	r1,
	r2,
	r3,
	r4,
	r5,
	r6,
	r7
};

enum Mem {
	TMP_0,
	TMP_1
};

class RegSim {
	int registers[8];
	int memory[2];
	
public:
	void stor_reg_mem(Reg src, Mem offset) {
		memory[offset] = registers[src];
	}
	
	void load_reg_mem(Reg src, Mem offset) {
		registers[src] = memory[offset];
	}
	
	void move_reg_reg(Reg dest, Reg src) {
		registers[dest] = registers[src];
	}
	
	void move_imm_reg(Reg dest, int imm) {
		registers[dest] = imm;
	}
	
	void add_reg_reg(Reg dest, Reg left, Reg right) {
		registers[dest] = registers[left] + registers[right];
	}
	
	void sub_reg_reg(Reg dest, Reg left, Reg right) {
		registers[dest] = registers[left] - registers[right];
	}
	
	void div_reg_reg(Reg dest, Reg left, Reg right) {
		registers[r0] = registers[r0] / registers[r1];
	}
	
	void ShowReg(Reg reg) {
		cout << "r" << reg << "=" << registers[reg] << endl;
	}
	
	void ShowMem(Mem mem) {
		cout << "m" << mem << "=" << memory[mem] << endl;
	}
};

int main() {
	RegSim sim;
	sim.move_imm_reg(r0, 13);
	sim.move_imm_reg(r1, 7);
	sim.add_reg_reg(r0, r0, r1);
	
	sim.ShowReg(r0);
	sim.ShowReg(r1);
}

