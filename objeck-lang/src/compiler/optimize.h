/***************************************************************************
 * Platform independent language optimizer.
 *
 * Copyright (c) 2008, 2009, 2010 Randy Hollines
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
  
  vector<IntermediateBlock*> OptimizeMethod(vector<IntermediateBlock*> input);

  // inline method calls
  IntermediateBlock* InlineMethodCall(IntermediateBlock* inputs);
  void InlineMethodCall(IntermediateMethod* called, IntermediateBlock* outputs);
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

public:
  ItermediateOptimizer(IntermediateProgram* p, string optimize) {
    program = p;
    inline_end = -1;
    cur_line_num = -1;
    merge_blocks = false;

    if(optimize == "s1") {
      optimization_level = 1;
    } else if(optimize == "s2") {
      optimization_level = 2;
    } else if(optimize == "s3") {
      optimization_level = 3;
    } else if(optimize == "s4") {
      optimization_level = 4;
    } else {
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
