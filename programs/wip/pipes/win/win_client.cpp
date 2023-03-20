#include "pipes.h"

int main() {
   const std::string name = "\\\\.\\pipe\\foo";

   HANDLE pipe;
   if(!OpenClientPipe(name, pipe)) {
      std::wcerr << "Unable to open client pipe!" << std::endl;
      exit(1);
   }

   const std::string line = "Hi Ya!\r\n";
   WriteLine(line, pipe);
   
   ClosePipe(pipe);
}
