#ifndef __CMODULE_H__
#define __CMODULE_H__

//#include <stddef.h>
#include <stdbool.h>
#include <wchar.h>

typedef struct CObjL CObjL;
CObjL* CObjL_new(const wchar_t* lib_uses);
void CObjL_destroy(CObjL* _CObjL);
bool CObjL_compile(CObjL* _CObjL, const wchar_t* file, const wchar_t* source, const wchar_t* opt_level);
void CObjL_getErrors(CObjL* _CObjL, const wchar_t** errors);
#ifdef _MODULE_STDIO
const wchar_t* CObjL_execute(CObjL* _CObjL);
const wchar_t* CObjL_execute_with_args(CObjL* _CObjL, const wchar_t* cmd_args);
#else
void CObjL_execute(CObjL* _CObjL);
void CObjL_execute_with_args(CObjL* _CObjL, const wchar_t* cmd_args);
#endif

#endif
