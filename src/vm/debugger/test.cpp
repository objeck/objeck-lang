#include "parser.h"

int main(int argc, char** args) {
  string line = "break test.cpp:10";  
  Parser parser("file.cpp");
  parser.Parse(line);
}
