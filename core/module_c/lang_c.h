#ifndef __CMODULE_H__
#define __CMODULE_H__

#include <stdbool.h>

typedef struct CObjL CObjL;
CObjL* CObjL_new(const char* source, const char* lib_uses, const char* cmd_args);
void CObjL_destroy(CObjL* _CObjL);
bool CObjL_compile(CObjL* _CObjL, const char* file, const char* opt);
void CObjL_getErrors(CObjL* _CObjL, const char** errors);
#ifdef _MODULE_STDIO
const char* CObjL_execute(CObjL* _CObjL);
#else
void CObjL_execute(CObjL* _CObjL);
#endif

#endif
