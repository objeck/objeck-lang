#include <iostream>
#include <fstream>
#include <stdio.h>
#include <string.h>

#define BUFFER_MAX 1024

int main() {
	FILE* file = fopen("/tmp/objk", "w+b");
	if(file) {
		char buffer[BUFFER_MAX + 1];

		strcpy(buffer, "Hello World!\r\n");
		fwrite(buffer, 1, BUFFER_MAX, file);

		fread(buffer, 1, BUFFER_MAX, file);
		std::cout << buffer << std::endl;
	}
	fclose(file);
}
