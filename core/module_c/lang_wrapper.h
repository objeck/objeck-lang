#ifndef __CMODULE_H__
#define __CMODULE_H__

#include "../module/lang.h"
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
	DllExport CObjL* CObjL_new(const char* source, const char* lib_uses, const char* cmd_args);
	DllExport void CObjL_destroy(CObjL* _CObjL);
	DllExport bool CObjL_compile(CObjL* _CObjL, const char* file, const char* opt);
	DllExport void CObjL_getErrors(CObjL* _CObjL, const char** errors);
#ifdef _MODULE_STDIO
	DllExport const char* CObjL_execute(CObjL* _CObjL);
#else
	DllExport void CObjL_execute(CObjL* _CObjL);
#endif
#ifdef __cplusplus
}
#endif

#endif
