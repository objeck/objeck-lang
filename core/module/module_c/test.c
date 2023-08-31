#include "lang_c.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
	CObjL* lang = CObjL_new("lang.obl");
	
	char file[] = "hello_0.obs";
	FILE *source_file;
	char *source;
	long numbytes;

	source_file = fopen(file, "r");
	if(source_file == NULL)
		return 1;

	fseek(source_file, 0L, SEEK_END);
	numbytes = ftell(source_file);
	fseek(source_file, 0L, SEEK_SET);

	source = (char*)calloc(numbytes, sizeof(char));
	if(source == NULL)
		return 1;

	fread(source, sizeof(char), numbytes, source_file);
	fclose(source_file);
	
	CObjL_compile(lang, file, source, "s2");
	CObjL_execute(lang);
	CObjL_destroy(lang);
	return 0;
}
