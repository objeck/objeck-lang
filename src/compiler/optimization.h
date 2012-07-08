/***************************************************************************
 * Platform independent language optimizer.
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
#include <deque>

using namespace backend;

/****************************
 * Performs basic-block
 * optimizations on
 * intermediate code.
 *
 * Order of 4 optimizations:
 * 0 - clean up jumps (always happens)
 * 1 - method inlining
 * 2 - constant folding
 * 3 - strength reduction
 * 4 - replace store/load with copy instruction
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
  // cleans up jumps and remove useless instructions
  IntermediateBlock* CleanJumps(IntermediateBlock* inputs);
  IntermediateBlock* RemoveUselessInstructions(IntermediateBlock* inputs);
  // integer constant folding
  IntermediateBlock* FoldIntConstants(IntermediateBlock* input);
  void CalculateIntFold(IntermediateInstruction* instr, deque<IntermediateInstruction*> &calc_stack,
                        IntermediateBlock* outputs);
  // float constant folding
  IntermediateBlock* FoldFloatConstants(IntermediateBlock* input);
  void CalculateFloatFold(IntermediateInstruction* instr,
                          deque<IntermediateInstruction*> &calc_stack,
                          IntermediateBlock* outputs);
  // strength reduction
  IntermediateBlock* StrengthReduction(IntermediateBlock* inputs);
  void CalculateReduction(IntermediateInstruction* instr,
                          deque<IntermediateInstruction*> &calc_stack,
                          IntermediateBlock* outputs);
  void ApplyReduction(IntermediateInstruction* test,
                      IntermediateInstruction* instr,
                      IntermediateInstruction* top_instr,
                      deque<IntermediateInstruction*> &calc_stack,
                      IntermediateBlock* outputs);
  void AddBackReduction(IntermediateInstruction* instr,
                        IntermediateInstruction* top_instr,
                        deque<IntermediateInstruction*> &calc_stack,
                        IntermediateBlock* outputs);
  // instruction replacement
  IntermediateBlock* InstructionReplacement(IntermediateBlock* inputs);
  void ReplacementInstruction(IntermediateInstruction* instr,
                              deque<IntermediateInstruction*> &calc_stack,
                              IntermediateBlock* outputs);
  
  //
  // atempts to inline a method
  //
  inline int CanInline(IntermediateMethod* mthd_called) {
    vector<IntermediateBlock*> blocks = mthd_called->GetBlocks();
#ifdef _DEBUG
    assert(blocks.size() == 1);
#endif
    if(mthd_called->GetNumParams() == 0) {
      vector<IntermediateInstruction*> instrs = blocks[0]->GetInstructions();
      //
      // getter instance pattern
      //
      if(instrs.size() == 3) {
	if(instrs[0]->GetType() == LOAD_INST_MEM &&
	   instrs[1]->GetOperand2() == INST && (instrs[1]->GetType() == LOAD_INT_VAR || 
						instrs[1]->GetType() == LOAD_FLOAT_VAR) &&
	   instrs[2]->GetType() == RTRN) {
	  return 0;
	}	
	return -1;
      }
      //
      // getter literal pattern
      //
      else if(instrs.size() == 2) {
	if((instrs[0]->GetType() == LOAD_INT_LIT || instrs[0]->GetType() == LOAD_FLOAT_LIT) &&
	   instrs[1]->GetType() == RTRN) {
	  return 1;
	}	
	return -1;
      }
    }
    else if(mthd_called->GetNumParams() == 1) {
      vector<IntermediateInstruction*> instrs = blocks[0]->GetInstructions();      
      if(instrs.size() == 5) {
	//
	// character print pattern
	//
      	if(instrs[0]->GetType() == STOR_INT_VAR && instrs[0]->GetOperand() == 0 && instrs[0]->GetOperand2() == LOCL &&
	   instrs[1]->GetType() == LOAD_INT_VAR && instrs[1]->GetOperand() == 0 && instrs[1]->GetOperand2() == LOCL &&
	   instrs[2]->GetType() == LOAD_INT_LIT && instrs[2]->GetOperand() == -3984 &&
	   instrs[3]->GetType() == TRAP && instrs[3]->GetOperand() == 2  &&
	   instrs[4]->GetType() == RTRN) {
	  return 2;
	}
	//
	// setter instance pattern
	//
	else if(instrs[0]->GetType() == STOR_INT_VAR && instrs[0]->GetOperand() == 0 && instrs[0]->GetOperand2() == LOCL &&
		instrs[1]->GetType() == LOAD_INT_VAR && instrs[1]->GetOperand() == 0 && instrs[1]->GetOperand2() == LOCL &&
		instrs[2]->GetType() == LOAD_INST_MEM &&
		instrs[3]->GetType() == STOR_INT_VAR && instrs[3]->GetOperand2() == INST &&
		instrs[4]->GetType() == RTRN) {
	  return 3;
	}
	else if(instrs[0]->GetType() == STOR_FLOAT_VAR && instrs[0]->GetOperand() == 0 && instrs[0]->GetOperand2() == LOCL &&
		instrs[1]->GetType() == LOAD_FLOAT_VAR && instrs[1]->GetOperand() == 0 && instrs[1]->GetOperand2() == LOCL &&
		instrs[2]->GetType() == LOAD_INST_MEM &&
		instrs[3]->GetType() == STOR_FLOAT_VAR && instrs[3]->GetOperand2() == INST &&
		instrs[4]->GetType() == RTRN) {
	  return 3;
	}

	return -1;
      }
    }
    
    return -1;
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
      optimization_level = 0;
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
