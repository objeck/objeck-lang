#include <iostream>

using namespace std;

#define REG_MEX 8
#define MEM_MEX 2

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
	int registers[REG_MEX];
	int memory[MEM_MEX];
	
public:
	RegSim() {
		for(int i = 0; i < REG_MEX; ++i) {
			registers[i] = -1;
		}
		
		for(int i = 0; i < MEM_MEX; ++i) {
			memory[i] = -1;
		}
	}
	
	~RegSim() {
	}

	void stor_reg_mem(Reg src, Mem offset) {
		cout << "stor\tr" << src << ", #" << offset << "[fp]" << endl;
		memory[offset] = registers[src];
	}
	
	void load_reg_mem(Reg src, Mem offset) {
		cout << "load\tr" << src << ", #" << offset << "[fp]" << endl;
		registers[src] = memory[offset];
	}
	
	void mov_reg_reg(Reg dest, Reg src) {
		cout << "mov\tr" << dest << ", r" << src << endl;
		registers[dest] = registers[src];
	}
	
	void mov_imm_reg(Reg dest, int imm) {
		cout << "mov\tr" << dest << ", #" << imm << endl;
		registers[dest] = imm;
	}
	
	void add_reg_reg(Reg dest, Reg src) {
		registers[dest] = registers[dest] + registers[src];
	}

	void sub_reg_reg(Reg dest, Reg src) {
		cout << "add\tr" << dest << ", r" << dest  << ", r" << src << endl;
		registers[dest] = registers[dest] - registers[src];
	}
	
	void div_reg_reg(Reg dest, Reg src) {
		if(src != r0) {
			stor_reg_mem(src, TMP_0);
			if(src == r1 && dest == r0) {
				mov_reg_reg(r1, dest);
				load_reg_mem(r0, TMP_0);
			}
			else {
				mov_reg_reg(r0, src);
			}
		}
		
		if(dest != r1 && dest != r0) {
			mov_reg_reg(r1, dest);
		}
		
		div_reg_reg();
		
		if(dest != r0) {
			mov_reg_reg(dest, r0);
		}
		load_reg_mem(src, TMP_0);
	}
	
	void div_reg_reg() {
		cout << "div\tr0, r0, r1 (r" << r0 << " = "  << registers[r0] 
			<< "/" << registers[r1] << ")" << endl;
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
	sim.mov_imm_reg(r1, 4);
	sim.mov_imm_reg(r3, 20);
	sim.div_reg_reg(r1, r3);
	sim.ShowReg(r1);
}

