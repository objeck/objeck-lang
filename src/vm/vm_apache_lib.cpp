/***************************************************************************
 * Wrapper for Apache2 module
 *
 * Copyright (c) 2012, Randy Hollines
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

#include <string>
#include "httpd.h"

#include "vm.h"

static Loader* loader = NULL;
static Runtime::StackInterpreter* intpr = NULL;
static StackMethod* mthd = NULL;

extern "C" 
{
  void Init(const char* prgm_id, const char* cls_id, const char* mthd_id)
  {
#ifdef _DEBUG
    cout << "dynamic lib: Init" << endl;
#endif
  
    // loader; when this goes out of scope program memory is released
    srand(time(NULL)); rand(); // calling rand() once improves random number generation
    loader = new Loader(prgm_id);
    loader->Load();
  
    intpr = new Runtime::StackInterpreter(Loader::GetProgram());

    StackClass* cls = Loader::GetProgram()->GetClass(cls_id);
    if(cls) {
      mthd = cls->GetMethod(mthd_id);
      if(!mthd) {
	cerr << ">>> DLL call: Unable to locate method; name=': " << mthd_id << "' <<<" << endl;
	return;
      }
    }
    else {
      cerr << ">>> DLL call: Unable to locate class; name='" << cls_id << "' <<<" << endl;
      return;
    }
  }

  void Call(request_rec *r)
  {
    if(mthd) { 
      // execute method
      long* op_stack = new long[STACK_SIZE];
      long* stack_pos = new long;

      // create and populate request object      
      long* obj = MemoryManager::Instance()->AllocateObject("ApacheModule",
							    op_stack, *stack_pos);
      if(obj) {
	obj[0] = (long)r;
	
	// set calling parameters
	op_stack[0] = (long)obj;
	(*stack_pos) = 1;
	
	intpr->Execute((long*)op_stack, (long*)stack_pos, 0, mthd, NULL, false);
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
    }
    else {
      cerr << ">>> DLL call: Invalid method!" << endl;
      exit(1);
    }
  }

  void Exit()
  {
    if(loader) {
      delete loader;
      loader = NULL;
    }
    
    if(intpr) {
      delete intpr;
      intpr = NULL;
    }
  
    MemoryManager::Instance()->Clear(); 
  }
}
