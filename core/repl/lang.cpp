/***************************************************************************
 * Objeck in a nutshell
 *
 * Copyright (c) 2023, Randy Hollines
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

#include "lang.h"

//
// Language
//
char* ObjeckLang::Compile(std::wstring input)
{
  const bool is_debug = false;
  const bool is_lib = false;
  const bool show_asm = false;
  const std::wstring opt = L"s1";
  const std::wstring sys_lib_path = L"lang.obl,gen_collect.obl";
  const std::wstring filename = L"shell://code";

  std::vector<std::pair<std::wstring, std::wstring> > programs;
  programs.push_back(make_pair(filename + L".obs", input));

  // parse source code
  Parser parser(L"", false, programs);
  if(parser.Parse()) {
    // analyze parse tree
    ParsedProgram* program = parser.GetProgram();
    ContextAnalyzer analyzer(program, sys_lib_path, is_lib);

    if(analyzer.Analyze()) {
      // emit intermediate code
      IntermediateEmitter intermediate(program, is_lib, is_debug);
      intermediate.Translate();

      // intermediate optimizer
      ItermediateOptimizer optimizer(intermediate.GetProgram(), intermediate.GetUnconditionalLabel(), opt, is_lib, is_debug);
      optimizer.Optimize();

      // emit target code
      FileEmitter target(optimizer.GetProgram(), is_lib, is_debug, show_asm, filename + L".obe");
      return target.GetBinary();
    }
  }

  return nullptr;
}

void ObjeckLang::Execute(char* code)
{
  Loader loader(code);
  loader.Load();

  // execute
  size_t* op_stack = new size_t[OP_STACK_SIZE];
  long* stack_pos = new long;
  (*stack_pos) = 0;

  // start the interpreter...
  Runtime::StackInterpreter* intpr = new Runtime::StackInterpreter(Loader::GetProgram());
  Runtime::StackInterpreter::AddThread(intpr);
  intpr->Execute(op_stack, stack_pos, 0, loader.GetProgram()->GetInitializationMethod(), nullptr, false);

#ifdef _DEBUG
  std::wcout << L"# final std::stack: pos=" << (*stack_pos) << L" #" << std::endl;
  if((*stack_pos) > 0) {
    for(int i = 0; i < (*stack_pos); ++i) {
      std::wcout << L"dump: value=" << op_stack[i] << std::endl;
    }
  }
#endif

  // clean up
  delete[] op_stack;
  op_stack = nullptr;
}