#include "parser.h"

void Test(const string &line) {
  Parser parser("file.cpp");
  parser.Parse(line);
}

int main(int argc, char** args) {
  // Test("print a");
  // Test("print b->c");
  Test("print e[10]");
}
