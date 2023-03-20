#include "pipes.h"

int main() {
   const char name[] = "/tmp/objk";

   HANDLE pipe;
   if(!OpenClientPipe(name, pipe)) {
      std::wcerr << "Unable to open pipe!" << std::endl;
      exit(1);
   }

   const std::string line = "Hi Ya!\r\n";
   WriteLine(line, pipe);
   
   ClosePipe(pipe);
}
