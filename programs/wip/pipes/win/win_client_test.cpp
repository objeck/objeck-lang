#include "win_pipes.h"

int main() {
   const std::string name = "\\\\.\\pipe\\foo";

   HANDLE pipe;
   if(!OpenClientPipe(name, pipe)) {
      std::wcerr << "Unable to open client pipe!" << std::endl;
      exit(1);
   }

   // byte
   std::cout << "byte: wrote=" << WriteByte(0x52, pipe) << std::endl;

   // bytes
   char buffer[] = "abcd";
   std::cout << "bytes: wrote=" << WriteBytes(buffer, 4, pipe) << std::endl;

   // chars
   wchar_t wbuffer[] = L"は 世界";
   std::cout << "chars: wrote=" << WriteChars(wbuffer, 4, pipe) << std::endl;

   // string

   
   ClosePipe(pipe);
}
