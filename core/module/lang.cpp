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

ObjeckLang::ObjeckLang(const std::wstring &s, const std::wstring &u, const std::wstring &a)
{
  source = s;
  lib_uses = u;
  cmd_args = a;
  code = nullptr;
}

ObjeckLang::~ObjeckLang()
{
}

bool ObjeckLang::Compile(const std::wstring filename)
{
  const bool is_debug = false;
  const bool is_lib = false;
  const bool show_asm = false;
  const std::wstring opt = L"s1";
  const std::wstring sys_lib_path = lib_uses;

  std::vector<std::pair<std::wstring, std::wstring> > programs;
  programs.push_back(make_pair(filename + L".obs", source));

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

#ifdef _MODULE_STDIO
const std::wstring ObjeckLang::Execute()
#else
void ObjeckLang::Execute()
#endif
{
#ifdef _WIN32
  if(_setmode(_fileno(stdin), _O_U8TEXT) < 0) {
    exit(1);
  }

  if(_setmode(_fileno(stdout), _O_U8TEXT) < 0) {
    exit(1);
  }
#endif

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
  long* stack_pos = new long;
  (*stack_pos) = 0;

  // execute
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

#ifdef _MODULE_STDIO
  const std::wstring output = intpr->GetOutputBuffer().str();
#endif

  // clean up
  delete[] op_stack;
  op_stack = nullptr;

  delete intpr;
  intpr = nullptr;

#ifdef _MODULE_STDIO
  return output;
#endif
}

std::vector<std::wstring> ObjeckLang::GetErrors() 
{
  return errors;
}