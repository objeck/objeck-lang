#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define BUFFER_MAX 4096

int main() {
	int pipe = open("/tmp/objk", O_APPEND, S_IRWXG);
std::cout << pipe << std::endl;
	if(pipe > -1) {
		char buffer[BUFFER_MAX + 1];

		strcpy(buffer, "Hello World dkdkfkdfk ak dfkdf...\r\n");
		write(pipe, buffer, BUFFER_MAX);

		read(pipe, buffer, BUFFER_MAX);
		std::cout << buffer << std::endl;
	}
	close(pipe);
}
