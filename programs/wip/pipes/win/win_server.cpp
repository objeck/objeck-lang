#include "pipes.h"
 
int main() 
{ 
   const std::string name = "\\\\.\\pipe\\foo";

   HANDLE pipe;
   if(!CreatePipe(name, pipe)) {
      std::cerr << "Unable to create pipe!" << std::endl;
      exit(1);
   }

   if(!OpenServerPipe(pipe)) {
      std::cerr << "Unable to open server pipe!" << std::endl;
      exit(1);
   }

   std::string line = ReadLine(pipe);
   std::cout << line << std::endl;

   if(!ClosePipe(pipe)) {
      std::cerr << "Unable to close pipe!" << std::endl;
      exit(1);
   }
}