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

#define UUID_LEN 36

string CreateUUID() {  
  uuid_t uuid;
  string uuid_str;

#ifdef _WIN32
  RPC_CSTR buffer = NULL;
  CoCreateGuid(&uuid);
  UuidToString(&uuid, &buffer);
  for(int i = 0; i < UUID_LEN; i++) {
    if(buffer[i] != '-') {
      uuid_str += buffer[i];
    }
  }
  RpcStringFree(&buffer);
#else
  char buffer[UUID_LEN];
  bzero(buffer, UUID_LEN);
  uuid_generate(uuid);
  uuid_unparse(uuid, buffer);
  for(int i = 0; i < UUID_LEN; i++) {
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
