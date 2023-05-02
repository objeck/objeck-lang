#include "../../../core/vm/lib_api.h"

extern "C" {
  //
  // initialize diagnostics environment
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void load_lib(VMContext& context)
  {
  }

  //
  // release diagnostics resources
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void unload_lib()
  {
  }

  //
  // Add two numbers
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void add(VMContext& context)
  {
    const long left = APITools_GetIntValue(context, 1);
    const long right = APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, left + right);
  }
}
