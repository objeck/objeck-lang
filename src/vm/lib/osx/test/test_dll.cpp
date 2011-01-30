#include <iostream>
#include <string>
#include "../../../../utilities/dll_tools.h"

using namespace std;

extern "C" {
  void load_lib() {}
  void unload_lib() {}
  
  void foo(long* data_array, long* op_stack, long *stack_pos, 
	   DLLTools_MethodCall_Ptr callback) {
    int size = DLLTools_GetArraySize(data_array);
    cout << size << endl;
    cout << DLLTools_GetIntValue(data_array, 1) << endl;
    cout << DLLTools_GetFloatValue(data_array, 2) << endl;
    DLLTools_SetFloatValue(data_array, 2, 13.5);
    DLLTools_SetIntValue(data_array, 0, 20);
    
    cout << "---0---" << endl;
    
//    DLLTools_PushInt(op_stack, stack_pos, 13);
    DLLTools_PushFloat(op_stack, stack_pos, 1112.11);
    DLLTools_CallMethod(callback, op_stack, stack_pos, NULL, "System.$Float:PrintLine:f,");
    
    cout << "---1---" << endl;
  }
}
