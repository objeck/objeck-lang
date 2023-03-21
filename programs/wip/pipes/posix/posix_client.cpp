#include "posix_pipes.h"

int main() {
	const char name[] = "/tmp/objk";

	FILE* pipe;
	if(!OpenPipe(name, pipe)) {
		std::wcerr << "Unable to open pipe!" << std::endl;
		exit(1);
	}

	WriteLine("Hi Ya!\r\n", pipe);
	std::cout << ReadLine(pipe) << std::endl;
	
	ClosePipe(pipe);
}
