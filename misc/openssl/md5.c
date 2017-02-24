#include <iostream>
#include <string.h>
#include <openssl/md5.h>

int main() {
  // unsigned char[] d = "Hello World!";

  unsigned char* d;

  unsigned char md[16];
  unsigned char *MD5(d, strlen(d), &md);

  cout << md << endl;

  return 0;
}
