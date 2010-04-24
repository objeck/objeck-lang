#include "parser.h"

int main(int argc, char** args) {
  if(argc == 1) {
    string line(args[1]);
    Parser parser("default");
    parser.Parse(line);
  }
}
