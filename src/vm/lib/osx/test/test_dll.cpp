#include <iostream>
#include <string>
#include "../../../../vm/lib_api.h"

using namespace std;

extern "C" {
  void load_lib() {}
  void unload_lib() {}

  void bar(VMContext& context) {
    long* array = (long*)APITools_GetIntValue(context, 0);
	 cout << APITools_GetFloatArrayElement(array, 0) << endl;
	 cout << APITools_GetFloatArrayElement(array, 1) << endl;
	 cout << APITools_GetFloatArrayElement(array, 2) << endl;
	 APITools_SetFloatArrayElement(array, 1, 33333.4);
  } 

  void foo(VMContext& context) {
    int size = APITools_GetArgumentCount(context);
    cout << size << endl;
    cout << APITools_GetIntValue(context, 1) << endl;
    cout << APITools_GetFloatValue(context, 2) << endl;
    APITools_SetFloatValue(context, 2, 13.5);
    APITools_SetIntValue(context, 0, 20);
    
    cout << "---0---" << endl;
    
//    APITools_PushInt(context, 13);
    APITools_PushFloat(context, 1112.11);
    APITools_CallMethod(context, NULL, "System.$Float:PrintLine:f,");
    
    cout << "---1---" << endl;
  }
}
