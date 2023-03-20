
#include "posix_pipes.h"

int main() 
{
	const std::string name = "/tmp/objk";

	if(!CreatePipe(name)) {
		std::cerr << "Unable to create pipe!" << std::endl;
		exit(1);
	}

	FILE* pipe;
	if(!OpenPipe(name, pipe)) {
		std::cerr << "Unable to open pipe!" << std::endl;
		exit(1);
	}

	std::string line = ReadLine(pipe);
	std::cout << line << std::endl;

	if(!RemovePipe(name, pipe)) {
		std::cerr << "Unable to close pipe!" << std::endl;
		exit(1);
	}
}