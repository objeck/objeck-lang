#ifndef __PIPES_H__
#define __PIPES_H__

#include <iostream>
#include <string>
#include <windows.h>

#define BUFFER_MAX 4096

bool CreatePipe(const std::string& name, HANDLE &pipe) {
	pipe = CreateNamedPipe(name.c_str(), 
		PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | 
		PIPE_READMODE_BYTE | 
		PIPE_WAIT, 
		// TODO: 1.5 MB source files
		PIPE_UNLIMITED_INSTANCES, 1024 * 16, 1024 * 16, 0, nullptr);
    if(pipe == INVALID_HANDLE_VALUE) {
    	return false;
    }

    return true;
}

bool ClosePipe(HANDLE pipe) {
	if(CloseHandle(pipe)) {
		false;
	}

	return true;
}

bool OpenClientPipe(const std::string& name, HANDLE &pipe) {
	pipe = CreateFile(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
 	if(pipe == INVALID_HANDLE_VALUE) {
    	return false;
 	}

	return true;
}

bool OpenServerPipe(HANDLE pipe) {
	if(ConnectNamedPipe(pipe, nullptr)) {
		return true;
	}

	return false;
}

std::string ReadLine(HANDLE pipe) {
	std::string line;

	DWORD read;
	bool done = false;
	char buffer[BUFFER_MAX];
	do {
		if(ReadFile(pipe, buffer, BUFFER_MAX - 1, &read, nullptr)) {
			buffer[read] = '\0';
			line.append(buffer);
			done = true;
		}
		else if(GetLastError() == ERROR_MORE_DATA) {
			buffer[read] = '\0';
			line.append(buffer);
		}
		else {
			return "";
		}
	}
	while(!done);

	return line;
}

bool WriteLine(const std::string &line, HANDLE pipe) {
	DWORD written;
	const size_t len = line.size() + 1;
	if(WriteFile(pipe, line.c_str(), len, &written, nullptr)) {
		return written == len;
	}

	return false;
}

#endif