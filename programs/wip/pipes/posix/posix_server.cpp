
#include "pipes.h"

int main() {
	const std::string name = "/tmp/objk";

	if(!CreatePipe(name)) {
		std::wcerr << "Unable to create pipe!" << std::endl;
		exit(1);
	}

	FILE* pipe;
	if(!OpenPipe(name, pipe)) {
		std::wcerr << "Unable to open pipe!" << std::endl;
		exit(1);
	}

	std::string line = ReadLine(pipe);
	std::wcout << line.c_str() << std::endl;

	if(!RemovePipe(name, pipe)) {
		std::wcerr << "Unable to close pipe!" << std::endl;
		exit(1);
	}
}