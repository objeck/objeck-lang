#include <iostream>

long Foo(long a, long b) {
	return 13;
}

/*
long Foo(long cls_id, long mthd_id, size_t cls_mem, size_t inst,
         size_t op_stack, long stack_pos, size_t call_stack,
         long call_stack_pos, size_t jit_mem, long offset, long ints) 
{
		return 13;	
}
*/

int main() {
//	long result = Foo(111, 110, 109, 108, 107, 106, 105,104, 103, 102, 101);
	long result = Foo(7, 11);
	std::cout << result << std::endl;

	return 0;
}
