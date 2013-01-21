/***************************************************************************
 * Starting point for the VM.
 *
 * Copyright (c) 2008-2013, Randy Hollines
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
 * - Neither the name of the Objeck Team nor the names of its
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

#include "../shared/version.h"
#include "../compiler/linker.h"
#include "../vm/vm.h"

int main(int argc, const char* argv[])
{
  if(argc == 2) {
    string file_name(argv[1]);
    if(file_name.rfind(".obe") != string::npos) {
      cout << "[Contents of Objeck executable file: '" << file_name << "']" << endl << endl;

      // loader; when this goes out of scope program memory is released
      Loader loader(argc, argv);
      loader.Load();      
      // list contents
      StackProgram* program = Loader::GetProgram();      
      program->List();
    }
    else if(file_name.rfind(".obl") != string::npos) {
      cout << "*** [Contains of the Objeck library file: '" << file_name << "'] ***" << endl << endl;

      Library linker(file_name);
      linker.Load();
      // list contents
      linker.List();
    }
    else {
      cerr << "Files must end in '.obe' or 'obl'" << endl;
    }
  }
  else {
    string usage = "Copyright (c) 2008-2011, Randy Hollines. All rights reserved.\n";
    usage += "THIS SOFTWARE IS PROVIDED \"AS IS\" WITHOUT WARRANTY. REFER TO THE\n";
    usage += "license.txt file or http://www.opensource.org/licenses/bsd-license.php\n";
    usage += "FOR MORE INFORMATION.\n\n";
    usage += VERSION_STRING;
    usage += "\n\n";
    usage += "usage: obu <program>\n\n";
    usage += "example: \"obu hello.obe\"";
    cerr << usage << endl << endl;

    return 1;
  }
}
