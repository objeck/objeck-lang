#include "native_launcher.h"

int main(int argc, char* argv[])
{
  const char* path = ".\\runtime\\bin\\obr.exe";
  const char* args[] = { path, ".\\app\\app.obe" , nullptr };
}