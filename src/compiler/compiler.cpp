/***************************************************************************
 * Starting point of the language compiler
 *
 * Copyright (c) 2008-2013, Randy Hollines
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

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#include "compiler.h"

using namespace std;

#define SUCCESS 0
#define COMMAND_ERROR 1
#define PARSE_ERROR 2
#define CONTEXT_ERROR 3

/****************************
 * Starts the compilation
 * process.
 ****************************/
int Compile(map<const wstring, wstring> &arguments, list<wstring> &argument_options, const wstring usage)
{
  // set UTF-8 environment
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_U16TEXT);
#else
  setlocale(LC_ALL, "");
  setlocale(LC_CTYPE, "UTF-8");
#endif
  
  // check source input
  wstring run_string;
  map<const wstring, wstring>::iterator result = arguments.find(L"src");
  if(result == arguments.end()) {
    result = arguments.find(L"run");
    if(result == arguments.end()) {
      wcerr << usage << endl << endl;
      return COMMAND_ERROR;
    }
    run_string = L"bundle Default { class Run { function : Main(args : wstring[]) ~ Nil {";
    run_string += arguments[L"run"];
    run_string += L"} } }";
    argument_options.remove(L"run");
  }
  else {
    argument_options.remove(L"src");
  }
  
  // check program output
  result = arguments.find(L"dest");
  if(result == arguments.end()) {
    wcerr << usage << endl << endl;
    return COMMAND_ERROR;
  }
  argument_options.remove(L"dest");
  
  // check program libraries path
  wstring sys_lib_path;
  const char* sys_lib_env_path = getenv("OBJECK_LIBS");
  if(sys_lib_env_path) {
    string temp = sys_lib_env_path;
    sys_lib_path.append(temp.begin(), temp.end());
    sys_lib_path += L"/lang.obl";
  }
  else {
    sys_lib_path = L"lang.obl";
  }
  
  result = arguments.find(L"lib");
  if(result != arguments.end()) {
    sys_lib_path += L"," + result->second;
    argument_options.remove(L"lib");
  }
  
  // check for optimize flag
  wstring optimize;
  result = arguments.find(L"opt");
  if(result != arguments.end()) {
    optimize = result->second;
    if(optimize != L"s0" && optimize != L"s1" && optimize != L"s2" && optimize != L"s3") {
      wcerr << usage << endl << endl;
      return COMMAND_ERROR;
    }
    argument_options.remove(L"opt");
  }
  
  // check program libraries path
  wstring target;
  result = arguments.find(L"tar");
  if(result != arguments.end()) {
    target = result->second;
    if(target != L"lib" && target != L"web" && target != L"exe") {
      wcerr << usage << endl << endl;
      return COMMAND_ERROR;
    }
    argument_options.remove(L"tar");
  }
  
  // check for debug flag
  bool is_debug = false;
  result = arguments.find(L"debug");
  if(result != arguments.end()) {
    is_debug = true;
    argument_options.remove(L"debug");
  }

  if(argument_options.size() != 0) {
    wcerr << usage << endl << endl;
    return COMMAND_ERROR;
  }
  
  // parse source code  
  Parser parser(arguments[L"src"], run_string);
  if(parser.Parse()) {
    bool is_lib = false;
    bool is_web = false;
    
    if(target == L"lib") {
      is_lib = true;
    }
    else if(target == L"web") {
      is_web = true;
    }
  
    // analyze parse tree
    ParsedProgram* program = parser.GetProgram();
    ContextAnalyzer analyzer(program, sys_lib_path, is_lib, is_web);
    if(analyzer.Analyze()) {
      // emit intermediate code
      IntermediateEmitter intermediate(program, is_lib, is_debug);
      intermediate.Translate();
      // intermediate optimizer
      ItermediateOptimizer optimizer(intermediate.GetProgram(), intermediate.GetUnconditionalLabel(), arguments[L"opt"]);
      optimizer.Optimize();
      // emit target code
      TargetEmitter target(optimizer.GetProgram(), is_lib, is_debug, is_web, arguments[L"dest"]);;
      target.Emit();
      return SUCCESS;
    }
    else {
      return CONTEXT_ERROR;
    }
  }
  else {
    return PARSE_ERROR;
  }
}
