#include "../../core/shared/sys.h"
#include <string>
#include <codecvt>

void SetEnv();

int main(int argc, char const* argv[])
{
  SetEnv();
  std::string test("\xe2\x82\xac");

  wchar_t out;
  BytesToCharacter(test, out);
  std::wcout << out << std::endl;

  return 0;
}

void SetEnv() {
#ifdef _MSYS2_CLANG
  std::locale utf(std::locale(), new std::codecvt_utf8<wchar_t>);
  std::wcout.imbue(utf);
  std::wcin.imbue(utf);
#else
  if (_setmode(_fileno(stdin), _O_U16TEXT) < 0) {
    exit(1);
  }

  if (_setmode(_fileno(stdout), _O_U16TEXT) < 0) {
    exit(1);
  }
#endif
}