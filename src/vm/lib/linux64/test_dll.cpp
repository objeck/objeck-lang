#include <iostream>
#include <string>
#include "../../../shared/dll_tools.h"

using namespace std;

extern "C" {
	void foo(long* data) {
		int size = DLLTools_GetArraySize(data);
		cout << size << endl;
		cout << DLLTools_GetIntValue(data, 1) << endl;
		cout << DLLTools_GetFloatValue(data, 2) << endl;
		DLLTools_SetFloatValue(data, 2, 13.5);
		DLLTools_SetIntValue(data, 0, 20);
	}
}
