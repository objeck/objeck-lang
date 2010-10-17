#include <iostream>
#include <vector>
#include <string.h>

using namespace std;

void print(const int argc, const char** argv) {

}

int main() {
	char buffer[] = "10 ../hello/test 23 dog";

	vector<string> params;
	params.push_back("prgm1.obs");
	char* token = strtok(buffer, " ");
	while(token) {
		params.push_back(token);
		token = strtok(NULL, " ");
	}

	const int argc = params.size();
	const char* args[argc];
	for(int i = 0; i < argc; i++) {
		args[i] = params[i].c_str();
	}

	print(argc, args);

	return 0;
}
