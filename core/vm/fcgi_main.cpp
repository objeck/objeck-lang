/***************************************************************************
 * Starting point for FastCGI module
 *
 * Copyright (c) 2023 Randy Hollines
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

#include "fcgi_main.h"

/******************************
 * FCGI entry point
 ******************************/
int main(const int argc, const char* argv[])
{
  FCGX_Init();
  
  FCGX_Stream*in; FCGX_Stream* out; FCGX_Stream* err; FCGX_ParamArray envp;
  while((FCGX_Accept(&in, &out, &err, &envp) >= 0)) {
    PrintEnv(out, "envp: ", envp);
    FCGX_FClose(out);

  }

  return 13;
}

/******************************
* Dump environment variables
******************************/
void PrintEnv(FCGX_Stream* out, const char* label, char** envp)
{
  FCGX_FPrintF(out, "\n", label, "\r\n");
  for(; *envp != nullptr; envp++) {
    FCGX_FPrintF(out, "\t", *envp, "\r\n");
  }
}
