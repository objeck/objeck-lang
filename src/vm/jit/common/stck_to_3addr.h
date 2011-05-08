/***************************************************************************
 * JIT compiler for the AMD64 architecture.
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
#ifndef __REG_ALLOC_H__
#define __REG_ALLOC_H__

#include "../../os/posix/memory.h"
#include "../../os/posix/posix.h"
#include <sys/mman.h>
#include <errno.h>

#include "../../common.h"
#include "../../interpreter.h"

using namespace std;

namespace Runtime {
  /********************************
   * JitCompilerIA64 class
   ********************************/
  class JitCompilerIA64 {
    static StackProgram* program;
    StackMethod* method;
    bool compile_success;
    long instr_index;
    
    // setup and teardown
    void Prolog();
    void Epilog(long imm);
    void RegisterRoot();
    void UnregisterRoot();
    
    // stack conversion operations
    void ProcessParameters(long count);
    void ProcessInstructions();
    
    
  public: 
    static void Initialize(StackProgram* p);
    
    JitCompilerIA64() {
    }
    
    ~JitCompilerIA64() {
    }

    //
    // Compiles stack code into IA-32 machine code
    //
    bool Compile(StackMethod* cm) {
      compile_success = true;
      instr_index = 0;
      
      if(!cm->GetNativeCode()) {
	method = cm;
	long cls_id = method->GetClass()->GetId();
	long mthd_id = method->GetId();
	
#ifdef _DEBUG
	cout << "---------- Compiling Native Code: method_id=" << cls_id << "," 
	     << mthd_id << "; mthd_name='" << method->GetName() << "'; params=" 
	     << method->GetParamCount() << " ----------" << endl;
#endif	
	// setup
	Prolog();
	// method information
	// register root
	RegisterRoot();
	// translate parameters
	ProcessParameters(method->GetParamCount());
	// tranlsate program
	ProcessInstructions();
	if(!compile_success) {
	  return false;
	}
	
	compile_success = true;
	
	// release our lock, native code has been compiled and set
	pthread_mutex_unlock(&cm->jit_mutex);
      }
      
      return compile_success;
    }
  };    
}

#endif  
