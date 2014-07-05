/***************************************************************************
* Starting point of the embedded ide compiler
*
* Copyright (c) 2014, Randy Hollines
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

#include "compiler.h"
#include "../shared/version.h"
#include <iostream>
#include <string>
#include <list>
#include <map>

using namespace std;

/****************************
* Program start. Parses command
* line parameters.
****************************/
extern "C" {
#ifdef _WIN32
  __declspec(dllexport)
#endif
  int ide_main(ostream& out, const wstring path_string)
  {
      wstring usage;
      usage += L"Copyright (c) 2008-2014, Randy Hollines. All rights reserved.\n";
      usage += L"THIS SOFTWARE IS PROVIDED \"AS IS\" WITHOUT WARRANTY. REFER TO THE\n";
      usage += L"license.txt file or http://www.opensource.org/licenses/bsd-license.php\n";
      usage += L"FOR MORE INFORMATION.\n\n";
      usage += VERSION_STRING;
      usage += L"\n\n";
      usage += L"usage: obc -src <program [(',' program)...]> [-opt (s0|s1|s2|s3)] [-lib libary [(libary ',')...]] [-tar (exe|web|lib)] [-alt] [-debug] -dest <output>\n";
      usage += L"example: \"obc -src ..\\examples\\hello.obs -dest hello.obe\"\n\n";
      usage += L"parameters:\n";
      usage += L"  -src:   [input]  source files (separated by ',')\n";
      usage += L"  -lib:   [input]  linked libraries (separated by ',')\n";
      usage += L"  -tar:   [output] target type 'lib' for linkable library or 'exe' for\n";
      usage += L"          executable default is 'exe'\n";
      usage += L"  -dest:  [output] file name\n";
      usage += L"  -opt:   [option] compiler optimizations s0-s3 (s3 being the most aggressive)\n";
      usage += L"          default is s0\n";
      usage += L"  -alt:   [option] allows use of C++ like syntax verses the UML like default\n";
      usage += L"  -debug: [option] compile with debug symbols";

      int status;
      if (true) {
        // get command line parameters
        list<wstring> argument_options;
        map<const wstring, wstring> arguments = ParseCommnadLine(path_string);
        for (map<const wstring, wstring>::iterator intr = arguments.begin(); intr != arguments.end(); ++intr) {
          argument_options.push_back(intr->first);
        }
        // compile source
        status = Compile(arguments, argument_options, usage, out);
      }
      else {
        out << UnicodeToBytes(usage) << endl << endl;
        status = USAGE_ERROR;
      }

      return status;
  }
}