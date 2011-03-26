/***************************************************************************
 * VM common.
 *
 * Copyright (c) 2008-2011, Randy Hollines
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

#include "common.h"
#include "loader.h"
#include "interpreter.h"

#ifdef _WIN32
list<HANDLE> StackProgram::thread_ids;
CRITICAL_SECTION StackProgram::program_cs;
#else
list<pthread_t> StackProgram::thread_ids;
pthread_mutex_t StackProgram::program_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/********************************
 * ObjectSerializer struct
 ********************************/
void ObjectSerializer::CheckObject(long* mem, bool is_obj, long depth)
{
  if(mem) {
    // TODO: optimize so this is not a double call.. see below
    StackClass* cls = MemoryManager::Instance()->GetClass(mem);
    if(cls) {
#ifdef _DEBUG
      for(int i = 0; i < depth; i++) {
        cout << "\t";
      }
      cout << "\t----- object: addr=" << mem << "(" << (long)mem << "), num="
           << cls->GetNumberDeclarations() << " -----" << endl;
#endif

      // mark data
      if(MarkMemory(mem)) {
        CheckMemory(mem, cls->GetDeclarations(), cls->GetNumberDeclarations(), depth);
      }
    } 
    else {
      // NOTE: this happens when we are trying to mark unidentified memory
      // segments. these segments may be parts of that stack or temp for
      // register variables
#ifdef _DEBUG
      for(int i = 0; i < depth; i++) {
        cout << "\t";
      }
      cout <<"$: addr/value=" << mem << endl;
      if(is_obj) {
        assert(cls);
      }
#endif
      // primitive or object array
      if(MarkMemory(mem)) {
	long* array = (mem);
	const long size = array[0];
	const long dim = array[1];
	long* objects = (long*)(array + 2 + dim);
	for(long k = 0; k < size; k++) {
	  CheckObject((long*)objects[k], false, 2);
	}
      }
    }
  }
}

void ObjectSerializer::CheckMemory(long* mem, StackDclr** dclrs, const long dcls_size, long depth)
{
  // check method
  for(int i = 0; i < dcls_size; i++) {
#ifdef _DEBUG
    for(int j = 0; j < depth; j++) {
      cout << "\t";
    }
#endif
    
    // update address based upon type
    switch(dclrs[i]->type) {
    case INT_PARM:
#ifdef _DEBUG
      cout << "\t" << i << ": INT_PARM: value=" << (*mem) << endl;
#endif
      // update
      mem++;
      break;

    case FLOAT_PARM: {
#ifdef _DEBUG
      FLOAT_VALUE value;
      memcpy(&value, mem, sizeof(FLOAT_VALUE));
      cout << "\t" << i << ": FLOAT_PARM: value=" << value << endl;
#endif
      // update
      mem += 2;
    }
    break;

    case BYTE_ARY_PARM: {
#ifdef _DEBUG
      // NULL terminated string workaround

      long* array = (long*)(*mem);
      cout << "\t" << i << ": BYTE_ARY_PARM: addr=" << array << "("
           << (long)array << "), size=" << array[0] << " byte(s)" << endl;
#endif
      // mark data
      MarkMemory((long*)(*mem));
      // update
      mem++;
    }
      break;

    case INT_ARY_PARM: {
#ifdef _DEBUG
      long* array = (long*)(*mem);
      cout << "\t" << i << ": INT_ARY_PARM: addr=" << array << "("
           << (long)array << "), size=" << array[0] << " byte(s)" << endl;
#endif
      // mark data
      MarkMemory((long*)(*mem));
      // update
      mem++;
    }
      break;
      
    case FLOAT_ARY_PARM: {
#ifdef _DEBUG
      long* array = (long*)(*mem);
      cout << "\t" << i << ": FLOAT_ARY_PARM: addr=" << array << "("
           << (long)array << "), size=" << array[0] << " byte(s)" << endl;
#endif
      // mark data
      MarkMemory((long*)(*mem));
      // update
      mem++;
    }
      break;
      
    case OBJ_PARM: {
#ifdef _DEBUG
      long* array = (long*)(*mem);
      cout << "\t" << i << ": OBJ_PARM: addr=" << array << "("
           << (long)array << "), id=" << array[0] << endl;
#endif
      // check object
      CheckObject((long*)(*mem), true, depth + 1);
      // update
      mem++;
    }
      break;
      
    case OBJ_ARY_PARM: {
#ifdef _DEBUG
      long* array = (long*)(*mem);
      cout << "\t" << i << ": OBJ_ARY_PARM: addr=" << array << "("
           << (long)array << "), size=" << array[0] << " byte(s)" << endl;
#endif
      // mark data
      if(MarkMemory((long*)(*mem))) {
        long* array = (long*)(*mem);
        const long size = array[0];
        const long dim = array[1];
        long* objects = (long*)(array + 2 + dim);
        for(long k = 0; k < size; k++) {
          CheckObject((long*)objects[k], true, 2);
        }
      }
      // update
      mem++;
    }
      break;
    }
  }
}

void ObjectSerializer::Serialize(long* inst) {
  CheckObject(inst, true, 0);
}

ObjectSerializer::ObjectSerializer(long* i) {
  Serialize(i);
}

ObjectSerializer::~ObjectSerializer() {
}

/********************************
 * SDK functions
 ********************************/
#ifndef _UTILS
void DLLTools_MethodCall(long* op_stack, long *stack_pos, long *instance, 
			 int cls_id, int mthd_id) {
  StackClass* cls = Loader::GetProgram()->GetClass(cls_id);
  if(cls) {
    StackMethod* mthd = cls->GetMethod(mthd_id);
    if(mthd) {
      Runtime::StackInterpreter intpr;
      intpr.Execute((long*)op_stack, (long*)stack_pos, 0, mthd, instance, false);
    }
    else {
      cerr << ">>> DLL call: Unable to locate method; id=" << mthd_id << " <<<" << endl;
      exit(1);
    }
  }
  else {
    cerr << ">>> DLL call: Unable to locate class; id=" << cls_id << " <<<" << endl;
    exit(1);
  }
}

void DLLTools_MethodCall(long* op_stack, long *stack_pos, long *instance, 
			 const char* cls_id, const char* mthd_id) {
  StackClass* cls = Loader::GetProgram()->GetClass(cls_id);
  if(cls) {
    StackMethod* mthd = cls->GetMethod(mthd_id);
    if(mthd) {
      Runtime::StackInterpreter intpr;
      intpr.Execute((long*)op_stack, (long*)stack_pos, 0, mthd, instance, false);
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
#endif
