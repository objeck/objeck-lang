#ifndef __PIPES_H__
#define __PIPES_H__

#include <iostream>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define BUFFER_MAX 4096

bool CreatePipe(const std::string name) {
	if(mkfifo(name.c_str(), S_IRWXU)) {
		return false;
	}

	return true;
}

bool OpenPipe(const std::string name, FILE* &pipe) {
	pipe = fopen(name.c_str(), "r+b");
	if(!pipe) {
		return false;
	}

	return true;
}

bool RemovePipe(const std::string name, FILE* pipe) {
	if(fclose(pipe)) {
		false;
	}

	if(unlink(name.c_str())) {
		return false;
	}

	return true;
}

bool ClosePipe(FILE* pipe) {
	if(fclose(pipe)) {
		false;
	}

	return true;
}

std::string ReadLine(FILE* pipe) {
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

bool WriteLine(const std::string &line, FILE* pipe) {
	const size_t len = line.size() + 1;
	return fwrite(line.c_str(), 1, len, pipe) == len;
}

#endif