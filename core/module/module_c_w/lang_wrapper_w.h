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
	DllExport CObjL* CObjL_new(const wchar_t* lib_uses);
	DllExport void CObjL_destroy(CObjL* _CObjL);
	DllExport bool CObjL_compile(CObjL* _CObjL, const wchar_t* file, const wchar_t* source, const wchar_t* opt_level);
	DllExport void CObjL_getErrors(CObjL* _CObjL, const wchar_t** errors);
#ifdef _MODULE_STDIO
	DllExport const wchar_t* CObjL_execute(CObjL* _CObjL);
	DllExport const wchar_t* CObjL_execute_with_args(CObjL* _CObjL, const wchar_t* cmd_args);
#else
	DllExport void CObjL_execute(CObjL* _CObjL);
	DllExport void CObjL_execute_with_args(CObjL* _CObjL, const wchar_t* cmd_args);
#endif
#ifdef __cplusplus
}
#endif

#endif
