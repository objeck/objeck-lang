/***************************************************************************
 * Platform independent language optimizer.
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

#ifndef __OPTIMIZE_H__
#define __OPTIMIZE_H__

#include "target.h"
#include <list>

using namespace backend;

/****************************
 * Performs basic-block
 * optimizations on
 * intermediate code.
 *
 * Order of 4 optimizations:
 * 1 - constant folding
 * 2 - strength reduction
 * 3 - replace store/load with copy instruction
 * 4 - method inlining
 ****************************/
class ItermediateOptimizer {
  IntermediateProgram* program;
  int optimization_level;
  int inline_end;
  IntermediateMethod* current_method;
  bool merge_blocks;
  int cur_line_num;
  map<int, int> inline_lbls;
  
  vector<IntermediateBlock*> OptimizeMethod(vector<IntermediateBlock*> input);

  // inline method calls
  IntermediateBlock* InlineMethodCall(IntermediateBlock* inputs);
  void InlineMethodCall(IntermediateMethod* called, IntermediateBlock* outputs);
  // cleans up jumps
  IntermediateBlock* CleanJumps(IntermediateBlock* inputs);
  // integer constant folding
  IntermediateBlock* FoldIntConstants(IntermediateBlock* input);
  void CalculateIntFold(IntermediateInstruction* instr, list<IntermediateInstruction*> &calc_stack,
                        IntermediateBlock* outputs);
  // float constant folding
  IntermediateBlock* FoldFloatConstants(IntermediateBlock* input);
  void CalculateFloatFold(IntermediateInstruction* instr,
                          list<IntermediateInstruction*> &calc_stack,
                          IntermediateBlock* outputs);
  // strength reduction
  IntermediateBlock* StrengthReduction(IntermediateBlock* inputs);
  void CalculateReduction(IntermediateInstruction* instr,
                          list<IntermediateInstruction*> &calc_stack,
                          IntermediateBlock* outputs);
  void ApplyReduction(IntermediateInstruction* test,
                      IntermediateInstruction* instr,
                      IntermediateInstruction* top_instr,
                      list<IntermediateInstruction*> &calc_stack,
                      IntermediateBlock* outputs);
  void AddBackReduction(IntermediateInstruction* instr,
                        IntermediateInstruction* top_instr,
                        list<IntermediateInstruction*> &calc_stack,
                        IntermediateBlock* outputs);
  // instruction replacement
  IntermediateBlock* InstructionReplacement(IntermediateBlock* inputs);
  void ReplacementInstruction(IntermediateInstruction* instr,
                              list<IntermediateInstruction*> &calc_stack,
                              IntermediateBlock* outputs);
  
  inline bool AllowsInlining() {
    const string &method_name = current_method->GetName();
    std::string sys_prefix("System.$");   
    std::string time_prefix("Time."); std::string io_prefix("Concurrency.");
    std::string net_prefix("API."); std::string intro_prefix("Introspection.");
    if(!method_name.compare(0, sys_prefix.size(), sys_prefix) ||
       !method_name.compare(0, time_prefix.size(), time_prefix) ||
       !method_name.compare(0, io_prefix.size(), io_prefix) ||
       !method_name.compare(0, net_prefix.size(), net_prefix) ||
       !method_name.compare(0, intro_prefix.size(), intro_prefix)) {
      return false;
    }
    
    return true;
  }
  
  inline bool CanBeInlining(IntermediateMethod* called) {
    // TODO: could be speed up with a cache
    const string &method_name = called->GetName();
    const string &new_cls_prefix = called->GetClass()->GetName() + ":New";
    std::string sys_prefix("System.$");
    std::string time_prefix("Time."); std::string io_prefix("Concurrency.");
    std::string net_prefix("API."); std::string intro_prefix("Introspection.");
    // check general properties
    if(!called->IsVirtual() && called->GetInstructionCount() < 16 &&
       !(current_method->GetClass()->GetId() == program->GetStartClassId() && 
	 current_method->GetId() == program->GetStartMethodId()) &&  
       current_method->GetSpace() <= 128 &&
       // check bundles
       method_name.compare(0, new_cls_prefix.size(), new_cls_prefix) != 0 &&
       method_name.compare(0, sys_prefix.size(), sys_prefix) != 0 &&
       method_name.compare(0, time_prefix.size(), time_prefix) != 0 &&
       method_name.compare(0, io_prefix.size(), io_prefix)  != 0 &&
       method_name.compare(0, net_prefix.size(), net_prefix)  != 0 &&
       method_name.compare(0, intro_prefix.size(), intro_prefix) != 0) {
      // check instructions
      int rtrn_count = 0;
      vector<IntermediateBlock*> blocks = called->GetBlocks();
      for(unsigned int i = 0; i < blocks.size(); i++) {
	vector<IntermediateInstruction*> input_instrs = blocks[i]->GetInstructions();
	for(unsigned int j = 0; j < input_instrs.size(); j++) {
	  IntermediateInstruction* instr = input_instrs[j];
	  switch(instr->GetType()) {
	  case RTRN:
	    ++rtrn_count;
	    if(rtrn_count > 1) {
	      return false;
	    }
	    break;
	  
	  case TRAP:
	  case TRAP_RTRN:
	  case LOAD_CLS_MEM:
	  case DYN_MTHD_CALL:
	    return false;

	  default:
	    break;
	  }
	}
      }
      
      return true;
    }

    return false;
  }
  
 public:
  ItermediateOptimizer(IntermediateProgram* p, string optimize) {
    program = p;
    inline_end = -1;
    cur_line_num = -1;
    merge_blocks = false;

    if(optimize == "s1") {
      optimization_level = 1;
    } 
    else if(optimize == "s2") {
      optimization_level = 2;
    } 
    else if(optimize == "s3") {
      optimization_level = 3;
    } 
    else {
      optimization_level = 1;
    }
  }
  
  ~ItermediateOptimizer() {
  }

  void Optimize();

  IntermediateProgram* GetProgram() {
    return program;
  }
};

#endif
