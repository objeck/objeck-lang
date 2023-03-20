#include <iostream>
#include <string>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define BUFFER_MAX 4096

size_t WriteLine(const std::string &line, FILE* pipe) {
	return fwrite(line.c_str(), 1, line.size() + 1, pipe);
}

int main() {
	char pipe_name[] = "/tmp/objk";

	FILE* pipe = fopen(pipe_name, "r+b");
	if(!pipe) {
		std::wcerr << "Unable to open pipe!" << std::endl;
		exit(1);
	}

	const std::string line = "Hi Ya!\r\n";
	WriteLine(line, pipe);
	
	if(fclose(pipe)) {
		std::wcerr << "Unable to close pipe!" << std::endl;
		exit(1);
	}
}
