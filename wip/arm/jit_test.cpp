/*********************************************** 
 * R. Hollines
 * Simple JIT code for ARM-A32 (Raspberry Pi v2)
 ***********************************************/

#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <iostream>

#define PAGE_SIZE 4096
#define INSTR uint32_t

using namespace std;

typedef int32_t (*jit_fun_ptr)();

enum Reg {
  R0,
  R1,
  R2,
  R3,
};

uint32_t add_reg_reg(Reg dest, Reg left, Reg right) {
  uint32_t op_code = 0xe0800000;
  
  uint32_t op_left = left << 16;
  op_code |= op_left;
  
  uint32_t op_dest = dest << 12;
  op_code |= op_dest;
	
  op_code |= right;
	
  return op_code;
}

uint32_t mov_imm(Reg reg, int32_t value) {
  uint32_t op_code = 0xe3a00000;

  uint32_t op_dest = reg << 12;
  op_code |= op_dest;
  op_code |= value;

  return op_code;
}

bool MakeCode(INSTR* code) {
  int code_index = 0;
	
  // push {fp}
  code[code_index++] = 0xe52db004;
  
  // add fp, sp, #0
  code[code_index++] = 0xe28db000;
	
  // add 2 values
  code[code_index++] = mov_imm(R0, 18);
  code[code_index++] = mov_imm(R1, 2);
  code[code_index++] = add_reg_reg(R0, R0, R1);
	
  // add sp, fp, #0
  code[code_index++] = 0xe28bd000;
	
  // ldmfd sp!, {fp}
  code[code_index++] = 0xe8bd0800;
	 
  // bx lr
  code[code_index++] = 0xe12fff1e;
	
  // mark for execute
  int flag = mprotect(code, code_index, PROT_EXEC);
  if(flag) {
    return false;
  }
	
  return true;
}

int main() {
  INSTR* code;
  if(posix_memalign((void**)&code, PAGE_SIZE, PAGE_SIZE)) {
    cerr << "Unable allocate JIT memory!" << endl;
    return 1;
  }
	
  if(MakeCode(code)) {
    jit_fun_ptr jit_fun = (jit_fun_ptr)code;
    int value = jit_fun();
    cout << value << endl;
  }
  else {
    cerr << "Unable to make code!" << endl;
    return 1;
  }
	
  return 0;
}
