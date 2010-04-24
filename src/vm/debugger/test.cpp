#include "parser.h"

int main(int argc, char** args) {
  if(argc == 1) {
    string line(args[1]);
    Parser parser;
    parser.Parse(line);
  }
}
