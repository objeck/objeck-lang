using namespace std;

// linux: sudo apt-get install uuid-dev; g++ test.cpp -luuid
// windows: 

#include <iostream>
#include <string.h>
#include <string>

#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "Rpcrt4.lib")
#else
#include <uuid/uuid.h>
#endif

string CreateUUID() {  
  uuid_t uuid;
  string uuid_str;

#ifdef _WIN32
  RPC_CSTR buffer = NULL;
  CoCreateGuid(&uuid);
  UuidToString(&uuid, &buffer);
  for(int i = 0; i < 36; i++) {
    if(buffer[i] != '-') {
      uuid_str += buffer[i];
    }
  }
  RpcStringFree(&buffer);
#else
  char buffer[36];
  bzero(buffer, 36);
  uuid_generate(uuid);
  uuid_unparse(uuid, buffer);
  for(int i = 0; i < 36; i++) {
    if(buffer[i] != '-') {
      uuid_str += buffer[i];
    }
  }
#endif

  return uuid_str;
}

int main() {
  cout << CreateUUID() << endl;
}
