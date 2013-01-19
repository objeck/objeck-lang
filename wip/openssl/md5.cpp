#include <iostream>
#include <string.h>
#include <openssl/md5.h>

using namespace std;

int main() {
  char d[] = "Hello World Again!";
  
  char md[MD5_DIGEST_LENGTH];
  unsigned char* p = MD5((unsigned char*)&d, strlen(d), (unsigned char *)&md);

  std::cout << d << std::endl;
  for(int i = 0; i < MD5_DIGEST_LENGTH; i++) {
    printf("%02x",md[i]);
  }
  cout << endl;

  return 0;
}
