using namespace std;

// linux: sudo apt-get install uuid-dev; g++ test.cpp -luuid
// windows: 

#include <iostream>
#include <string.h>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <uuid/uuid.h>
#endif

int main() {
	uuid_t t;
	uuid_generate(t);

	char ch[36];
	memset(ch, 0, 36);
	uuid_unparse(t, ch);
	string uuid(ch);
  
  cout << uuid.size() << ": " << uuid << endl;
}
