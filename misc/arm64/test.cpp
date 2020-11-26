#include <iostream>
#include <sys/mman.h>

#define PAGE_SIZE 4096

typedef long (*fun_ptr)(long a, long b);

int main() {
	//
	// START: Test for execute permissions
	//

	const uint32_t instrs[] = {
		0xd10043ff, // sub sp, sp, #16
		0xf90007e0, // str x0, [sp, #8]
		0xf90003e1, // str x1, [sp]
		0xd28001a0, // mov x0, #13
		0x910043ff, // add sp, sp, #16
		0xd65f03c0  // ret
	};

	uint32_t* buffer = (uint32_t*)mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, 0, 0);
	if(buffer == MAP_FAILED) {
		std::cerr << "unable to mmap!" << std::endl;
		return 1;
	}

	pthread_jit_write_protect_np(false);
	memcpy(buffer, instrs, sizeof(instrs));
	__clear_cache(buffer, buffer + sizeof(instrs));
	fun_ptr func = (fun_ptr)buffer;
	pthread_jit_write_protect_np(true);

	const long value = func(1, 2);
	wcout << value << endl;

	munmap(buffer, PAGE_SIZE);
	
	//
	// END: Test
	//

	return 0;
}