
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

	std::cout << ReadLine(pipe) << std::endl;
	WriteLine("Second...\r\n", pipe);

	if(!ClosePipe(pipe)) {
		std::cerr << "Unable to close pipe!" << std::endl;
		exit(1);
	}
}
