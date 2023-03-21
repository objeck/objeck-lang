#ifndef __PIPES_H__
#define __PIPES_H__

#include <iostream>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

bool CreatePipe(const std::string &name) {
	if(mkfifo(name.c_str(), S_IRWXU)) {
		return false;
	}

	return true;
}

bool OpenPipe(const std::string &name, FILE* &pipe) {
	pipe = fopen(name.c_str(), "w+b");
	if(!pipe) {
		return false;
	}

	return true;
}

bool RemovePipe(const std::string &name, FILE* pipe) {
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
	std::string output;

	int value;
	do {
		value = fgetc(pipe);	
		if(value != '\0') {
			output += (char)value;
		}
	}
	while(value != '\0');
	
	return output;
}

bool WriteLine(const std::string &line, FILE* pipe) {
	const size_t len = line.size() + 1;
	return fwrite(line.c_str(), 1, len, pipe) == len;
}

#endif
