#ifndef __CMODULE_H__
#define __CMODULE_H__

#include "../lang.h"
#include <string>

using namespace std;

#ifdef _WIN32
#define DllExport   __declspec( dllexport )
#else
#define DllExport
#endif

#ifdef __cplusplus
extern "C" {
#endif
	typedef struct CObjL CObjL;
	DllExport CObjL* CObjL_new(const char* lib_uses);
	DllExport void CObjL_destroy(CObjL* _CObjL);
	DllExport bool CObjL_compile(CObjL* _CObjL, const char* file, const char* source, const char* opt_level);
	DllExport void CObjL_getErrors(CObjL* _CObjL, const char** errors);
#ifdef _MODULE_STDIO
	DllExport const char* CObjL_execute(CObjL* _CObjL);
	DllExport const char* CObjL_execute_with_args(CObjL* _CObjL, const char* cmd_args);
#else
	DllExport void CObjL_execute(CObjL* _CObjL);
	DllExport void CObjL_execute_with_args(CObjL* _CObjL, const char* cmd_args);
#endif
#ifdef __cplusplus
}
#endif

#endif
