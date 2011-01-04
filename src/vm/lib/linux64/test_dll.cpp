#include <iostream>
#include <string>
#include "../../../shared/dll_tools.h"

using namespace std;

extern "C" {
	void foo(long* data, long* op_stack, long *stack_pos, 
            const long ip, StackProgram* program) {
		int size = DLLTools_GetArraySize(data);
		cout << size << endl;
		cout << DLLTools_GetIntValue(data, 1) << endl;
		cout << DLLTools_GetFloatValue(data, 2) << endl;
		DLLTools_SetFloatValue(data, 2, 13.5);
		DLLTools_SetIntValue(data, 0, 20);
	}
}
