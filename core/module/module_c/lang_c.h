#ifndef __CMODULE_H__
#define __CMODULE_H__

#include <stdbool.h>

typedef struct CObjL CObjL;
CObjL* CObjL_new(const char* lib_uses);
void CObjL_destroy(CObjL* _CObjL);
bool CObjL_compile(CObjL* _CObjL, const char* file, const char* source, const char* opt_level);
void CObjL_getErrors(CObjL* _CObjL, const char** errors);
#ifdef _MODULE_STDIO
const char* CObjL_execute(CObjL* _CObjL);
const char* CObjL_execute_with_args(CObjL* _CObjL, const char* cmd_args);
#else
void CObjL_execute(CObjL* _CObjL);
void CObjL_execute_with_args(CObjL* _CObjL, const char* cmd_args);
#endif

#endif
