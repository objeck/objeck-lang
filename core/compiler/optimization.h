/***************************************************************************
 * Platform independent language optimizer.
 *
 * Copyright (c) 2008-2022, Randy Hollines
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

#include "emit.h"
#include <deque>

using namespace backend;

#define LOCL_INLINE_MEM_MAX 128

/****************************
 * Performs optimizations on
 * intermediate code.
 *
 * Order of 4 optimizations:
 * 0.0 - clean up jumps and other unneeded instructions (always happens)
 * 1.1 - setter and getter inlining
 * 1.2 - advanced method inlining 
 * 1.3 - constant propagation
 * 1.4 - constant folding
 * 2.1 - strength reduction
 * 3.1 - replace store+load with copy
 ****************************/

union PropValue {
  int int_value;
  double float_value;
};

class ItermediateOptimizer {
  IntermediateProgram* program;
  set<wstring> can_inline;
  int optimization_level;
  int unconditional_label;
  IntermediateMethod* current_method;
  bool merge_blocks;
  int cur_line_num;
  bool is_lib;
  
  vector<IntermediateBlock*> OptimizeMethod(vector<IntermediateBlock*> input);
  vector<IntermediateBlock*> InlineMethod(vector<IntermediateBlock*> inputs);
  vector<IntermediateBlock*> JumpToLocation(vector<IntermediateBlock*> inputs);
  
  // inline setters/getters
  IntermediateBlock* InlineSettersGetters(IntermediateBlock* inputs);

  // cleans up jumps and remove useless instructions
  IntermediateBlock* CleanJumps(IntermediateBlock* inputs);
  IntermediateBlock* RemoveUselessInstructions(IntermediateBlock* inputs);

  // advanced method inlining
  IntermediateBlock* InlineMethod(IntermediateBlock* inputs);

  // jump to address
  IntermediateBlock* JumpToLocation(IntermediateBlock* inputs);

  // constant propagation
  IntermediateBlock* ConstantProp(IntermediateBlock* input);

  // dead store
  IntermediateBlock* DeadStore(IntermediateBlock* input);

  // integer constant folding
  IntermediateBlock* FoldIntConstants(IntermediateBlock* input);
  void CalculateIntFold(IntermediateInstruction* instr, deque<IntermediateInstruction*> &calc_stack,
                        IntermediateBlock* outputs);

  // float constant folding
  IntermediateBlock* FoldFloatConstants(IntermediateBlock* input);
  void CalculateFloatFold(IntermediateInstruction* instr, deque<IntermediateInstruction*> &calc_stack, IntermediateBlock* outputs);
  // strength reduction
  IntermediateBlock* StrengthReduction(IntermediateBlock* inputs);
  void CalculateReduction(IntermediateInstruction* instr, deque<IntermediateInstruction*> &calc_stack, IntermediateBlock* outputs);

  void ApplyReduction(IntermediateInstruction* test, IntermediateInstruction* instr, IntermediateInstruction* top_instr,
                      deque<IntermediateInstruction*> &calc_stack, IntermediateBlock* outputs);

  void AddBackReduction(IntermediateInstruction* instr, IntermediateInstruction* top_instr, 
                        deque<IntermediateInstruction*> &calc_stack, IntermediateBlock* outputs);
  
  // instruction replacement
  IntermediateBlock* InstructionReplacement(IntermediateBlock* inputs);
  void ReplacementInstruction(IntermediateInstruction* instr, deque<IntermediateInstruction*> &calc_stack, IntermediateBlock* outputs);

  bool CanInlineMethod(IntermediateMethod* mthd_called, set<IntermediateMethod*> &inlined_mthds, set<int> &lbl_jmp_offsets);
  
  int CanInlineSetterGetter(IntermediateMethod* mthd_called);
  
 public:
   ItermediateOptimizer(IntermediateProgram* p, int u, wstring o, bool l, bool d);
  
  ~ItermediateOptimizer() {
  }

  void Optimize();

  IntermediateProgram* GetProgram() {
    return program;
  }
};

#endif