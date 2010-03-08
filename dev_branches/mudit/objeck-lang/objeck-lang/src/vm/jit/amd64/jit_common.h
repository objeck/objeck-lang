/***************************************************************************
 * Common JIT compiler that is used by platform dependent code generators. 
 * Translates stack code into IMM form that is used to generate machine code
 * on the future passes.
 *
 * Copyright (c) 2010 Randy Hollines
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
 * - Neither the name of the StackVM Team nor the names of its 
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

#ifndef __JIT_COMMON_H__
#define __JIT_COMMON_H__

#include "../../common.h"

using namespace std;

namespace Runtime {
  enum DagType {
    DAG_INT_LIT = -100,
    DAG_INT_VAR
  };
  
  class DagNode {
    string key;
    int int_value;
    double double_value;
    DagType type;
    DagNode* left;
    DagNode* right;
    vector<DagNode*> associations;
    
  public:
    DagNode(int iv, DagType t) {
      int_value = iv;
      key = IntToString(iv);
      type = t;
      left = NULL;
      right = NULL;
    } 

    DagNode(string k, DagType t) {
      int_value = 0;
      key = k;
      type = t;
      left = NULL;
      right = NULL;
    }    

    DagType GetType() {
      return type;
    }

    string GetKey() {
      return key;
    }

    DagNode* GetLeft() {
      return left;
    }

    void SetLeft(DagNode* l) {
      left = l;
    }
    
    DagNode* GetRight() {
      return right;
    }

    void SetRight(DagNode* r) {
      right = r;
    }

    int GetIntValue() {
      return int_value;
    }

    vector<DagNode*> GetAssociations() {
      return associations;
    }
    
    void AddAssociation(DagNode* a) {
      associations.push_back(a);
    }
  };


  /****************************
   * JIT compiler
   ****************************/
  class JitCompiler {
    static StackProgram* program;
    map<const string, DagNode*> dag;
    stack<DagNode*> sim_stack;
    int temp_var_id;

    void ProcessStore(StackInstr* instr);
    void ProcessIntOperation(StackInstr* instr, InstructionType oper);
    
   public:
    JitCompiler() {
      temp_var_id = 0;
    }

    ~JitCompiler() {
    }

    static void Initialize(StackProgram* p);

    /****************************
     * Compiles stack code
     ****************************/
    bool Compile(StackMethod* method);
    
    /****************************
     * Executes machine code
     ****************************/
    long Execute(StackMethod* method, long* self, long* op_stack, long* stack_pos) {
      return 0;
    }
  };
}
#endif

