/***************************************************************************
 * JIT compiler for the x86 architecture.
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

#include "jit_common.h"

using namespace Runtime;

StackProgram* JitCompiler::program;

void JitCompiler::Initialize(StackProgram* p) {
  program = p;
}

bool JitCompiler::Compile(StackMethod* method) {
  const long count = method->GetInstructionCount();
  for(int i = 0; i < count; i++) {
    StackInstr* instr = method->GetInstruction(i);

    switch(instr->GetType()) {
    case LOAD_INT_LIT:
      sim_stack.push(new DagNode(instr->GetOperand(), DAG_INT_LIT));
      break;

    case LOAD_INT_VAR: {
      const string key = "v" + IntToString(instr->GetOperand());
      map<const string, DagNode*>::iterator found = dag.find(key);
      if(found != dag.end()) {
	sim_stack.push(found->second);
      }
      else {
	sim_stack.push(new DagNode(key, DAG_INT_VAR));
      }
    }
      break;

    case STOR_INT_VAR:
      ProcessStore(instr);
      break;

    case ADD_INT:
      ProcessIntOperation(instr, ADD_INT);
      break;

    case SUB_INT:
      ProcessIntOperation(instr, SUB_INT);
      break;
    }
  }

  map<const string, DagNode*>::iterator iter;
  for(iter = dag.begin(); iter != dag.end(); iter++) {
    cout << "??: " << iter->second->GetKey() << "<-" << iter->first << endl;
  }

  exit(1);
  return false;      
}

void JitCompiler::ProcessStore(StackInstr* instr) {
  DagNode* node = sim_stack.top();

  sim_stack.pop();
  const string key = "v" + IntToString(instr->GetOperand());
  
  // find nodes that have been invalidated
  map<const string, DagNode*>::iterator iter;
  for(iter = dag.begin(); iter != dag.end(); iter++) {
    DagNode* value = iter->second;
    
    // check node
    if(key == value->GetKey()) {
      dag.erase(iter);
    }
    // check node components
    else if(value->GetLeft() && value->GetRight()) {
      DagNode* left = value->GetLeft();
      DagNode* right = value->GetRight();

      if(key == left->GetKey()) {
	dag.erase(iter);
      }
      else if(key == right->GetKey()) {
	dag.erase(iter);
      }
    }

    // check associations
    vector<DagNode*> associations = value->GetAssociations();
    for(int i  = 0; i < associations.size(); i++) {
      dag.erase(associations[i]->GetKey());
    }
  }
  
  // associate variable with value
  switch(node->GetType()) {
  case DAG_INT_LIT:
    cout << "0: " << key << " = " << node->GetIntValue() << endl; 
    break;
    
  case DAG_INT_VAR:
    cout << "1: " << key << " = " << node->GetKey() << endl;
    break;
  }
  dag.insert(pair<const string, DagNode*>(key, node));
}

void JitCompiler::ProcessIntOperation(StackInstr* instr, InstructionType oper) {
  DagNode* left = sim_stack.top();
  sim_stack.pop();

  DagNode* right = sim_stack.top();
  sim_stack.pop();

  // generate key
  string key = left->GetKey();
  switch(oper) {
  case ADD_INT:
    key += "+";
    break;

  case SUB_INT:
    key += "-";
    break;
  }
  key += right->GetKey();

  DagNode* result = new DagNode("t" + IntToString(temp_var_id++), DAG_INT_VAR);  
  map<const string, DagNode*>::iterator found = dag.find(key);
  if(found != dag.end()) {
    if(found->second->GetKey()[0] = 't') {
      this->sim_stack.push(found->second);
      temp_var_id--;
      return;
    }
    else {
      found->second->AddAssociation(result);
      cout << "2: " << result->GetKey() << " = " << found->second->GetKey() << endl;
    }
  }
  else {
    // look for components
    
    // TODO: memory managment
    found = dag.find(left->GetKey());
    if(found != dag.end()) {
      left = found->second;
    }

    found = dag.find(left->GetKey());
    if(found != dag.end()) {
      right = found->second;
    }

    /*
      if(left->GetType() == DAG_INT_LIT && left->GetType() == DAG_INT_LIT) {
      int fold;
      switch(oper) {
      case ADD_INT:
      fold = left->GetIntValue() + right->GetIntValue();
      break;
      
      case SUB_INT:
      fold = left->GetIntValue() - right->GetIntValue();
      break;
      }
      DagNode* folded = new DagNode(fold, DAG_INT_LIT);
      sim_stack.push(folded);
      
      // emit
      cout << result->GetKey() << " = " << folded->GetKey() << endl;
      return;
      }
      else {
    */
    result->SetLeft(left);
    result->SetRight(right);

    dag.insert(pair<const string, DagNode*>(key, result));
    
    // emit
    cout << "3: " << result->GetKey() << " = " << left->GetKey();
    switch(oper) {
    case ADD_INT:
      cout << "+";
      break;
      
    case SUB_INT:
      cout<< "-";
      break;
    }
    cout << right->GetKey() << endl;
    //}
  }
  
  sim_stack.push(result);
}
