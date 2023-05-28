/***************************************************************************
* Starting point of the interactive REPL
*
* Copyright (c) 2023, Randy Hollines
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
* - Neither the name of the Objeck team nor the names of its
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


#define USAGE_ERROR -1
#define SYSTEM_ERROR -2

#include "../shared/version.h"
#include <iostream>
#include <sstream>

/****************************
* Program start. Parses command
* line parameters.
****************************/
int main(int argc, char* argv[])
{
  bool done = false;
  std::wstring in;
  do {
    std::wcout << L"> ";
    std::getline(std::wcin, in);

    if(in == L"quit" || in == L"q") {
      done = true;
    }
    else if(in == L"/l") {
      std::wcout << L"<list>" << std::endl;
    }
    else if(in == L"/g") {
      std::wcout << L"<goto>" << std::endl;
    }
    else {
      std::wcout << L"  [" << in << L']' << std::endl;
    }
  } 
  while(!done);
}
