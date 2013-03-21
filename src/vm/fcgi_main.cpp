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
#include "vm.h"

#define SUCCESS 0
#define USAGE_ERROR -1

void PrintEnv(FCGX_Stream* out, const char* label, char** envp)
{
  cout << endl << label << endl;
  for( ; *envp != NULL; envp++) {
    cout << "\t" << *envp << endl;
  }
}

int main(const int argc, const char* argv[])
{
  const char* prgm_path = FCGX_GetParam("PROGRAM_PATH", environ);
  if(!prgm_path) {
    cerr << "Unable to find program, please ensure the 'PROGRAM_PATH' variable has been set correctly." << endl;
    exit(1);
  }
  
  // load program
  srand(time(NULL)); rand();
  Loader loader(prgm_path);
  loader.Load();

  // ignore web applications
  if(!loader.IsWeb()) {
    cerr << "Please recompile the code to be a web application." << endl;
    exit(1);
  }
  
#ifdef _TIMING
  clock_t start = clock();
#endif
  
  // locate starting class and method
  StackMethod* mthd = loader.GetStartMethod();
  if(!mthd) {
    cerr << "Unable to locate the 'Request(args)' function." << endl;
    exit(1);
  }
  
#ifdef _DEBUG
  cerr << "### Loaded method: " << mthd->GetName() << " ###" << endl;
#endif
  
  Runtime::StackInterpreter intpr(Loader::GetProgram());
  
  // go into accept loop...
  FCGX_Stream*in;
  FCGX_Stream* out;
  FCGX_Stream* err;
  FCGX_ParamArray envp;
  
  while(mthd && (FCGX_Accept(&in, &out, &err, &envp) >= 0)) {    
    // execute method
    long* op_stack = new long[CALC_STACK_SIZE];
    long* stack_pos = new long;
    
    // create request
    long* req_obj = MemoryManager::Instance()->AllocateObject("FastCgi.Request", 
							      op_stack, *stack_pos, false);
    if(req_obj) {
      req_obj[0] = (long)in;
      req_obj[1] = (long)envp;
      
      // create response
      long* res_obj = MemoryManager::Instance()->AllocateObject("FastCgi.Response", 
								op_stack, *stack_pos, false);
      if(res_obj) { 	
	res_obj[0] = (long)out;
	res_obj[1] = (long)err;
	
	// set calling parameters
	op_stack[0] = (long)req_obj;
	op_stack[1] = (long)res_obj;
	*stack_pos = 2;
 	
	// execute method
	intpr.Execute((long*)op_stack, (long*)stack_pos, 0, mthd, NULL, false);
      }
      else {
	cerr << ">>> DLL call: Unable to allocate object FastCgi.Response <<" << endl;
	// TODO: error
	return 1;
      }
    }
    else {
      cerr << ">>> DLL call: Unable to allocate object FastCgi.Request <<<" << endl;
      // TODO: error
      return 1;
    }
    
#ifdef _DEBUG
    cout << "# final stack: pos=" << (*stack_pos) << " #" << endl;
    if((*stack_pos) > 0) {
      for(int i = 0; i < (*stack_pos); i++) {
	cout << "dump: value=" << (void*)(*stack_pos) << endl;
      } 
    }
#endif
    
    // clean up
    delete[] op_stack;
    op_stack = NULL;

    delete stack_pos;
    stack_pos = NULL;
    
#ifdef _DEBUG
    PrintEnv(out, "Request environment", envp);
    PrintEnv(out, "Initial environment", environ);
#endif
  }
  
  return 0;
}
