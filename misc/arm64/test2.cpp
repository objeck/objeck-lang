#include <iostream>

long Foo(long cls_id, long mthd_id, size_t cls_mem, size_t inst,
         size_t op_stack, long stack_pos, size_t call_stack,
         long call_stack_pos, size_t jit_mem, long offset, long ints)
{
	long x = 101;
	long y = x - inst;
	return y;
}

int main() {
	const long foo = Foo(11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1);
	std::wcout << foo << std::endl;
	
	return 0;
}
