/***************************************************************************
 * Objeck in a nutshell
 *
 * Copyright (c) 2025, Randy Hollines
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

ObjeckLang::ObjeckLang(const std::wstring &u)
{
  lib_uses = u;
  code = nullptr;
}

ObjeckLang::~ObjeckLang()
{
  if(code) {
    free(code);
    code = nullptr;
  }
}

//
// Compile
//
bool ObjeckLang::Compile(std::vector<std::pair<std::wstring, std::wstring>> &file_source, const std::wstring opt_level)
{
  const bool is_debug = false;
  const bool is_lib = false;
  const bool show_asm = false;

  // parse source code
  Parser parser(L"", false, file_source);
  if(parser.Parse()) {
    // analyze parse tree
    ParsedProgram* program = parser.GetProgram();
    ContextAnalyzer analyzer(program, lib_uses, is_lib);

    if(analyzer.Analyze()) {
      // emit intermediate code
      IntermediateEmitter intermediate(program, is_lib, is_debug);
      intermediate.Translate();

      // intermediate optimizer
      ItermediateOptimizer optimizer(intermediate.GetProgram(), intermediate.GetUnconditionalLabel(), opt_level, is_lib, is_debug);
      optimizer.Optimize();

      // emit target code
      const std::wstring target_name = file_source.front().first;
      FileEmitter target(optimizer.GetProgram(), is_lib, is_debug, show_asm, target_name + L".obe");

      // free and set compiled code
      if(code) {
        free(code);
        code = nullptr;
      }
      code = target.GetBinary();

      return code != nullptr;
    }
    else {
      errors = analyzer.GetErrors();
    }
  }
  else {
    errors = parser.GetErrors();
  }

  return false;
}

//
// Execute
//
#ifdef _MODULE_STDIO
const std::wstring ObjeckLang::Execute(const std::wstring cmd_args)
#else
void ObjeckLang::Execute(const std::wstring cmd_args)
#endif
{
  // parse command-line argument string
  std::wstringstream cmd_param_stream;
  std::vector<std::wstring> params;
  for(auto& ch : cmd_args) {
    if(ch == L' ' || ch == L'\t') {
      std::wstring cmd_param = cmd_param_stream.str();
      if(!cmd_param.empty()) {
        params.push_back(cmd_param);
        cmd_param_stream.str(std::wstring());
      }
    }
    else {
      cmd_param_stream << ch;
    }
  }

  std::wstring cmd_param = cmd_param_stream.str();
  if(!cmd_param.empty()) {
    params.push_back(cmd_param);
    cmd_param_stream.str(std::wstring());
  }

  // load code
  Loader loader(code, params);
  loader.Load();

  // initialize execution
  size_t* op_stack = new size_t[OP_STACK_SIZE];
  size_t* stack_pos = new size_t;
  (*stack_pos) = 0;

  // execute
  Runtime::StackInterpreter* intpr = new Runtime::StackInterpreter(Loader::GetProgram(), 0);
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

#ifdef _MODULE_STDIO
  const std::wstring output = intpr->GetOutputBuffer().str();
#endif

  Runtime::StackInterpreter::RemoveThread(intpr);
  Runtime::StackInterpreter::HaltAll();

  Runtime::StackInterpreter::Clear();
  MemoryManager::Clear();

  delete intpr;
  intpr = nullptr;

  // clean up
  delete[] op_stack;
  op_stack = nullptr;

  delete stack_pos;
  stack_pos = nullptr;

#ifdef _MODULE_STDIO
  return output;
#endif
}

#ifdef _MODULE_STDIO
const std::wstring ObjeckLang::Execute{
  return Execute(L"");
#else
void ObjeckLang::Execute() {
  Execute(L"");
}
#endif

//
// Errors
//
std::vector<std::wstring> ObjeckLang::GetErrors() 
{
  return errors;
}