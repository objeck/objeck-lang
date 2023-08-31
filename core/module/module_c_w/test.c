#include "lang_c_w.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
	CObjL* lang = CObjL_new(L"lang.obl");
	
	wchar_t file[] = L"hello_0.obs";
	FILE *source_file;
	wchar_t *source;
	long numbytes;

	source_file = _wfopen(file, L"r, ccs=UTF-8");
	if(source_file == NULL)
		return 1;

	fseek(source_file, 0L, SEEK_END);
	numbytes = ftell(source_file);
	fseek(source_file, 0L, SEEK_SET);

	source = (wchar_t*)calloc(numbytes, sizeof(wchar_t));
	if(source == NULL)
		return 1;

	fread(source, sizeof(wchar_t), numbytes, source_file);
	fclose(source_file);
	
	CObjL_compile(lang, file, source, L"s2");
	CObjL_execute(lang);
	CObjL_destroy(lang);
	return 0;
}
