#include <iostream>
#include <string>
#include "../../../vm/callback.h"

using namespace std;

extern "C" {
  __declspec(dllexport) void load_lib() {}

  __declspec(dllexport) void unload_lib() {}

  __declspec(dllexport) void foo(long* data_array, long* op_stack, long *stack_pos, Callbacks& callbacks) {
	long size = DLLTools_GetArraySize(data_array);
    cout << size << endl;
	cout << DLLTools_GetIntValue(data_array, 1) << endl;
	cout << DLLTools_GetFloatValue(data_array, 2) << endl;
	DLLTools_SetFloatValue(data_array, 2, 13.5);
	DLLTools_SetIntValue(data_array, 0, 20);

	cout << "---0---" << endl;

	DLLTools_PushFloat(op_stack, stack_pos, 3.14);
	DLLTools_CallMethod(callbacks.method_call, op_stack, stack_pos, NULL, "System.$Float:PrintLine:f,");

	cout << "---1---" << endl;
  }
}