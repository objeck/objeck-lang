#include "pipes.h"

int main() {
	const char name[] = "/tmp/objk";

	FILE* pipe;
	if(!OpenPipe(name, pipe)) {
		std::wcerr << "Unable to open pipe!" << std::endl;
		exit(1);
	}

	const std::string line = "Hi Ya!\r\n";
	WriteLine(line, pipe);
	
	ClosePipe(pipe);
}
