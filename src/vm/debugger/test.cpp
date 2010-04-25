#include "parser.h"
#include <string>
#include <fstream>

void Test(const string &line) {
  Parser parser("file.cpp");
  Command* command = parser.Parse(line);
}

int main(int argc, char** args) {
  string line;
  do {
    cout << "> ";
    getline(cin, line);
    cout << endl;
    if(line.size() > 0) {
      Test(line);
    }
  }
  while(line.size() > 0);
}
