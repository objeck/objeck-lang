#include "../lang.h"

// read source from file, see below
std::wstring ReadFile(const std::wstring filename);

int main(int argc, char* argv[]) {
#ifdef _DEBUG
  OpenLogger("debug.log");
#endif

  if(argc == 2) {
    // setup file name/source pair
    std::vector<std::pair<std::wstring, std::wstring>> file_source;
    const std::wstring filename = BytesToUnicode(argv[1]);
    file_source.push_back(std::make_pair(filename, ReadFile(filename)));

    // compile code
    ObjeckLang lang(L"lang.obl");
    if(lang.Compile(file_source, L"s2")) {
      // execute
      lang.Execute(L"");
    }
    // show errors
    else {
      auto errors = lang.GetErrors();
      for(auto& error : errors) {
        std::wcout << error << std::endl;
      }
    }
  }

#ifdef _DEBUG
  CloseLogger();
#endif

  return 0;
}

std::wstring ReadFile(const std::wstring filename)
{
  std::wstring buffer;

  std::wifstream file("");
  if(file.good()) {
    std::wstring line;
    while(getline(file, line)) {
      buffer += line;
    }
    file.close();
  }

  return buffer;
}