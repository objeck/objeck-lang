#include <iostream>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define BUFFER_MAX 4096

std::string ReadString(FILE* pipe) {
	char* buffer = new char[BUFFER_MAX];
	
	// line from pipe
	size_t buffer_len = BUFFER_MAX;
	int read = (int)getline(&buffer, &buffer_len, pipe);
	if(read < 0) {
		delete[] buffer;
		buffer = nullptr;

		return "";
	}

	if(read < BUFFER_MAX) {
		buffer[read] = '\0';
	}
	else {
		delete[] buffer;
		buffer = nullptr;

		return "";
	}

	// copy and clean up
	std::string output(buffer);

	delete[] buffer;
	buffer = nullptr;

	return output;
}

int main() {
	char pipe_name[] = "/tmp/objk";

	if(mkfifo(pipe_name, S_IRWXU)) {
		std::wcerr << "Unable to create pipe!" << std::endl;
		exit(1);
	}

	FILE* pipe = fopen(pipe_name, "r+b");
	if(!pipe) {
		std::wcerr << "Unable to open pipe!" << std::endl;
		exit(1);
	}

	std::string line = ReadString(pipe);
	std::wcout << line.c_str() << std::endl;

	if(fclose(pipe)) {
		std::wcerr << "Unable to close pipe!" << std::endl;
		exit(1);
	}

	if(unlink(pipe_name)) {
		std::wcerr << "Unable to unlink pipe!" << std::endl;
		exit(1);
	}
}