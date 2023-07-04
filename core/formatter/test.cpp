#include "formatter.h"

int main(int argc, char* argv[])
{
  if(argc > 1) {
    CodeFormatter formatter(BytesToUnicode(argv[1]), true);
    formatter.Format();
  }
}