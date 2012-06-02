/***************************************************************************
 * Starting point for FastCGI module
 *
 * Copyright (c) 2012 Randy Hollines
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

#include "fcgi_config.h"
#include "fcgiapp.h"

#include <iostream>
#include <dlfcn.h>
#include <stdlib.h>

using namespace std;

// Objeck VM lifecycle functions
typedef void (*vm_init_def)(const char*, const char*, const char*);
typedef void (*vm_call_def)(FCGX_Stream*, FCGX_Stream*, FCGX_Stream*, FCGX_ParamArray);
typedef void (*vm_exit_def)();

int main(const int argc, const char* argv[])
{
  void* dynamic_lib = dlopen("./obr.so", RTLD_LAZY);
  if(!dynamic_lib) {
    cerr << ">>> Runtime error loading DLL: " << dlerror() << " <<<" << endl;
    exit(1);
  }
    
  // call load function
  char* error;
  vm_init_def init_ptr = (vm_init_def)dlsym(dynamic_lib, "Init");
  if((error = dlerror()) != NULL)  {
    cerr << ">>> Runtime error calling function: " << error << " <<<" << endl;
    exit(1);
  }

  vm_call_def call_ptr = (vm_call_def)dlsym(dynamic_lib, "Call");
  if((error = dlerror()) != NULL)  {
    cerr << ">>> Runtime error calling function: " << error << " <<<" << endl;
    exit(1);
  }
  (*init_ptr)("../compiler/a.obe", "FastCgiModule", "FastCgiModule:Request:");  
  
  FCGX_Stream *in, *out, *err;
  FCGX_ParamArray envp;
  while (FCGX_Accept(&in, &out, &err, &envp) >= 0) {
    (*call_ptr)(in, out, err, envp);    
  }
  
  return 0;
}
