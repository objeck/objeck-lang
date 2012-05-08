/***************************************************************************
 * Starting point for the VM.
 *
 * Copyright (c) 2008-2012, Randy Hollines
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

#define SUCCESS 0
#define USAGE_ERROR -1

static Loader* loader;
static Runtime::StackInterpreter* intpr;

static void Init(const char* arg)
{
  // loader; when this goes out of scope program memory is released
  srand(time(NULL)); rand(); // calling rand() once improves random number generation
  Loader* loader = new Loader(arg);
  loader->Load();
  
  intpr = new Runtime::StackInterpreter(Loader::GetProgram());
}

static void Request(const char* cls_id, const char* mthd_id)
{
  StackClass* cls = Loader::GetProgram()->GetClass(cls_id);
  if(cls) {
    StackMethod* mthd = cls->GetMethod(mthd_id);
    if(mthd) {
      // execute
      long* op_stack = new long[STACK_SIZE];
      long* stack_pos = new long;
      (*stack_pos) = 0;

      intpr->Execute((long*)op_stack, (long*)stack_pos, 0, mthd, NULL, false);

      // clean up
      delete[] op_stack;
      op_stack = NULL;

      delete stack_pos;
      stack_pos = NULL;
    }
    else {
      cerr << ">>> DLL call: Unable to locate method; name=': " << mthd_id << "' <<<" << endl;
      exit(1);
    }
  }
  else {
    cerr << ">>> DLL call: Unable to locate class; name='" << cls_id << "' <<<" << endl;
    exit(1);
  }
}

static void Exit()
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
