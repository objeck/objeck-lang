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

#include "vm.h"
#include "../shared/version.h"
#include <iostream>
#include <string>

using namespace std;

int main(const int argc, const char* argv[])
{
  if(argc > 1) {
    // enable UTF-8 enviroment
    setlocale(LC_ALL, "");
    setlocale(LC_CTYPE, "UTF-8");

    // Initialize OpenSSL
    CRYPTO_malloc_init();
    SSL_library_init();
    
    return Execute(argc, argv);
  } 
  else {
    wstring usage = L"Copyright (c) 2008-2013, Randy Hollines. All rights reserved.\n";
    usage += L"THIS SOFTWARE IS PROVIDED \"AS IS\" WITHOUT WARRANTY. REFER TO THE\n";
    usage += L"license.txt file or http://www.opensource.org/licenses/bsd-license.php\n";
    usage += L"FOR MORE INFORMATION.\n\n";
    usage += VERSION_STRING;
    usage += L"\n\n";
    usage += L"usage: obr <program>\n\n";
    usage += L"example: \"obr hello.obe\"";
    wcerr << usage << endl << endl;

    return 1;
  }
}
