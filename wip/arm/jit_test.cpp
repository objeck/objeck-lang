#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <iostream>

#define PAGE_SIZE 4096
#define INSTR uint32_t

using namespace std;

typedef int32_t (*jit_fun_ptr)();

bool MakeCode(INSTR* code) {
	int code_index = 0;
	
	// push {fp}
	code[code_index++] = 0xe52db004;
	
	// add fp, sp, #0
	code[code_index++] = 0xe28db000;
	
	// ....
	
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
		jit_fun();
	}
	else {
		cerr << "Unable to make code!" << endl;
		return 1;
	}
	
	return 0;
}
