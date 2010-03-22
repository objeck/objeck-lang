/***************************************************************************
 * Starting point for the VM.
 *
 * Copyright (c) 2008-2010 Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the StackVM Team nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#ifdef _DEBUG
#ifdef _WIN32
// #include "vld.h"
#endif
#endif

#include "windows.h"
#include <iostream>
#include <string>

using namespace std;

typedef int (*Execute)(const int, char**);

int main(const int argc, char* argv[])
{
  if(argc > 1) {
    int status;
    HINSTANCE compiler_lib = LoadLibrary("obr.dll");
    if(compiler_lib) {
      Execute _Execute = (Execute)GetProcAddress(compiler_lib, "Execute");
      if(_Execute) {
        status = _Execute(argc, argv);
      }
      else {
        cerr << "Unable to envoke virtual machine!" << endl;
        status = -2;
      }
      // clean up
      FreeLibrary(compiler_lib);
    }
    else {
      cerr << "Unable to loaded obr.dll!" << endl;
      status = -2;
    }
  } 
  else {
    string usage = "Copyright (c) 2008-2010, Randy Hollines. All rights reserved.\n";
    usage += "THIS SOFTWARE IS PROVIDED \"AS IS\" WITHOUT WARRANTY. REFER TO THE\n";
    usage += "license.txt file or http://www.opensource.org/licenses/bsd-license.php\n";
    usage += "FOR MORE INFORMATION.\n\n";
    usage += "usage: obr <program>\n\n";
    usage += "example: \"obr prgm1.obe\"";
    cerr << usage << endl << endl;
  }
}
