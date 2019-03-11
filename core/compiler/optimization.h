/***************************************************************************
 * Platform independent language optimizer.
 *
 * Copyright (c) 2008-2018, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduceC the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the Objeck team nor the names of its
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

#ifdef _SCRIPTER
#include "../scripter/loader.h"
#else
#include "emit.h"
#endif

#include <deque>

using namespace backend;

#define LOCL_INLINE_MEM_MAX 128

/****************************
 * Performs optimizations on
 * intermediate code.
 *
 * Order of 4 optimizations:
 * 0 - clean up jumps and other unneeded instructions (always happens)
 * 1 - setter and getter inlining / advanced method inlining / constant folding
 * 2 - strength reduction
 * 3 - replace store/load with copy instruction
 ****************************/
class ItermediateOptimizer {
  IntermediateProgram* program;
  int optimization_level;
  int unconditional_label;
  IntermediateMethod* current_method;
  bool merge_blocks;
  int cur_line_num;
  
  vector<IntermediateBlock*> OptimizeMethod(vector<IntermediateBlock*> input);
  vector<IntermediateBlock*> InlineMethod(vector<IntermediateBlock*> inputs);
  // inline setters/getters
  IntermediateBlock* InlineSettersGetters(IntermediateBlock* inputs);
  // cleans up jumps and remove useless instructions
  IntermediateBlock* CleanJumps(IntermediateBlock* inputs);
  IntermediateBlock* RemoveUselessInstructions(IntermediateBlock* inputs);
  // advanced method inlining
  IntermediateBlock* InlineMethod(IntermediateBlock* inputs);
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


  bool CanInlineMethod(IntermediateMethod* mthd_called, set<IntermediateMethod*> &inlined_mthds, set<int> &lbl_jmp_offsets) {
    // don't inline the same method more then once, since you'll have label/jump conflicts
    set<IntermediateMethod*>::iterator found = inlined_mthds.find(mthd_called);
    if(found != inlined_mthds.end()) {
      return false;
    }
    
    // don't inline recursive calls
    if(mthd_called == current_method) {
      return false;
    }

    // don't inline parameter calls
    if(mthd_called->GetName().find(current_method->GetName()) != string::npos) {
      return false;
    }
    
    // don't inline method calls for primitive objects
    const wstring called_cls_name = mthd_called->GetName();
    if(called_cls_name.find(L'$') != wstring::npos) {
			if(called_cls_name == L"System.$Float:Sin:f," ||
				 called_cls_name == L"System.$Float:Cos:f,") {
				return true;
			}

      return false;
    }
    
    // methods are in the same class, such that instance and class
    // offset will not have to be ajusted 
    if(mthd_called->GetClass() != current_method->GetClass()) {
      return false;
    }

    if(current_method->GetSpace() + mthd_called->GetSpace() > LOCL_INLINE_MEM_MAX) {
      return false;
    }
    
    // ignore constructors
    const wstring called_mthd_name = mthd_called->GetName();
    if(called_mthd_name.find(L":New:") != wstring::npos) {
      return false;
    }

    // don't inline into "main" since it's not JTI compiled
    const wstring curr_mthd_name = current_method->GetName();
    if(curr_mthd_name.find(L":Main:o.System.String*,") != wstring::npos) {
      return false;
    }
    
    // check instructions
    vector<IntermediateBlock*> mthd_called_blocks = mthd_called->GetBlocks();
    vector<IntermediateInstruction*> mthd_called_instrs = mthd_called_blocks[0]->GetInstructions();

    // must have at least an instruction, non-virtual
    if(mthd_called_instrs.size() < 2) {
      return false;
    }

    bool found_rtrn = false;
    for(size_t j = 0; j < mthd_called_instrs.size(); j++) {
      IntermediateInstruction* mthd_called_instr = mthd_called_instrs[j];
      switch(mthd_called_instr->GetType()) {
      case instructions::LOAD_INT_VAR:
      case instructions::STOR_INT_VAR:
      case instructions::COPY_INT_VAR:
      case instructions::LOAD_FLOAT_VAR:
      case instructions::STOR_FLOAT_VAR:
      case instructions::COPY_FLOAT_VAR:
        // if the method/function is in another class it must only have local references
        if(mthd_called_instr->GetOperand2() != LOCL) {
          return false;
        }
        break;

        // ignore special instructions
      case instructions::TRAP:
      case instructions::TRAP_RTRN:
      case instructions::CPY_BYTE_ARY:
      case instructions::CPY_CHAR_ARY:
      case instructions::CPY_INT_ARY:
      case instructions::CPY_FLOAT_ARY:
      case instructions::DLL_LOAD:
      case instructions::DLL_UNLOAD:
      case instructions::DLL_FUNC_CALL:
      case instructions::THREAD_JOIN:
      case instructions::THREAD_SLEEP:
      case instructions::THREAD_MUTEX:
      case instructions::CRITICAL_START:
      case instructions::CRITICAL_END:
      case instructions::LIB_NEW_OBJ_INST:
      case instructions::LIB_MTHD_CALL:
      case instructions::LIB_OBJ_INST_CAST:
      case instructions::LIB_FUNC_DEF:
        return false;

      case instructions::LOAD_CLS_MEM:
        if(mthd_called->GetClass() != current_method->GetClass()) {
          return false;
        }
        break;
	
        // look for conflicting jump offsets
      case instructions::LBL:
      case instructions::JMP:
        if(lbl_jmp_offsets.find(mthd_called_instr->GetOperand()) != lbl_jmp_offsets.end()) {
          return false;
        }
        break;

      case instructions::RTRN:
        if(found_rtrn) {
          return false;
        }
        found_rtrn = true;
        break;
	
      default:
        break;
      }
    }
    
    return true;
  }
  
  //
  // atempts to inline a method
  //
  inline int CanInlineSetterGetter(IntermediateMethod* mthd_called) {
    if(current_method == mthd_called) {
      return -1;
    };

    // ignore interfaces
    vector<IntermediateBlock*> blocks = mthd_called->GetBlocks();
    if(blocks.size() < 1) {
      return -1;
    }

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
  ItermediateOptimizer(IntermediateProgram* p, int u, wstring o, bool d) {
    program = p;
    cur_line_num = -1;
    merge_blocks = false;
    unconditional_label = u; 

    if(d) {
      optimization_level = 0;
    }
    else {
      if(o == L"s0") {
        optimization_level = 0;
      } 
      else if(o == L"s1") {
        optimization_level = 1;
      }
      else if(o == L"s2") {
        optimization_level = 2;
      } 
      else if(o == L"s3") {
        optimization_level = 3;
      } 
      else {
        optimization_level = 3;
      }
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
