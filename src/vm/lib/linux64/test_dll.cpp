#include <iostream>
#include <string>
#include "../../../shared/dll_tools.h"

using namespace std;

extern "C" {
	void foo(long* data) {
		int size = DLLTools_GetArraySize(data);
		cout << size << endl;
		cout << DLLTools_GetIntValue(data, 1) << endl;
		DLLTools_SetIntValue(data, 0, 20);
	}
}
