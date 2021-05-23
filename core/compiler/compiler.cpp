/***************************************************************************
 * Starting point of the language compiler
 *
 * Copyright (c) 2008-2021, Randy Hollines
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
#include "types.h"

using namespace std;

#define SUCCESS 0
#define COMMAND_ERROR 1
#define PARSE_ERROR 2
#define CONTEXT_ERROR 3

/****************************
 * Starts the compilation process
 ****************************/
int Compile(const wstring& src_file, const wstring& opt, const wstring& dest_file, const wstring &run_string, const wstring &sys_lib_path, wstring &target, bool alt_syntax, bool is_debug, bool show_asm) {
  // parse source code  
  Parser parser(src_file, alt_syntax, run_string);
  if(parser.Parse()) {
    bool is_lib = false;
    bool is_web = false;

    if(target == L"lib") {
      is_lib = true;
    }

    if(target == L"web") {
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
      ItermediateOptimizer optimizer(intermediate.GetProgram(), intermediate.GetUnconditionalLabel(), opt, is_lib, is_debug);
      optimizer.Optimize();
      // emit target code
      FileEmitter target(optimizer.GetProgram(), is_lib, is_debug, is_web, show_asm, dest_file);
      target.Emit();

      return SUCCESS;
    }
    else {
      return CONTEXT_ERROR;
    }
  }
  
  return PARSE_ERROR;
}

/****************************
 * Processes compilation options
 ****************************/
int OptionsCompile(map<const wstring, wstring>& arguments, list<wstring>& argument_options, const wstring usage)
{
  // set UTF-8 environment
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_U8TEXT);
#else
  setlocale(LC_ALL, "");
  setlocale(LC_CTYPE, "UTF-8");
#endif

  // check for optimize flag
  map<const wstring, wstring>::iterator result = arguments.find(L"ver");
  if(result != arguments.end()) {
#if defined(_WIN64) && defined(_WIN32)
    wcout << VERSION_STRING << L" Objeck (x86-64 Windows)" << endl;
#elif _WIN32
    wcout << VERSION_STRING << L" Objeck (x86 Windows)" << endl;
#elif _OSX
    wcout << VERSION_STRING << L" Objeck (x86-64 macOS)" << endl;
#elif _X64
    wcout << VERSION_STRING << L" Objeck (x86-64 Linux)" << endl;
#elif _ARM32
    wcout << VERSION_STRING << L" Objeck (ARMv7 Linux)" << endl;
#else
    wcout << VERSION_STRING << L" Objeck (x86 Linux)" << endl;
#endif 
    wcout << L"---" << endl;
    wcout << L"Copyright (c) 2008-2021, Randy Hollines" << endl;
    wcout << L"This is free software; see the source for copying conditions.There is NO" << endl;
    wcout << L"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE." << endl;
    argument_options.remove(L"ver");
    exit(0);
  }

  // check source input
  wstring run_string;
  wstring src_file;
  result = arguments.find(L"src");
  if(result == arguments.end()) {
    result = arguments.find(L"in");
    if(result == arguments.end()) {
      wcerr << usage << endl;
      return COMMAND_ERROR;
    }
    run_string = L"class Run { function : Main(args : String[]) ~ Nil {";
    run_string += arguments[L"in"];
    run_string += L"} }";
    argument_options.remove(L"in");
  }
  else {
    argument_options.remove(L"src");
    src_file = result->second;
    // pare file name w/o extension
    if(!frontend::EndsWith(src_file, L".obs")) {
      src_file += L".obs";
    }
    // parse file wildcard
    else if(frontend::EndsWith(src_file, L"*.obs")) {
      wstring dir_path;
      wstring files_paths;
      frontend::RemoveSubString(src_file, L"*.obs");
      if(src_file.empty()) {
        src_file = L"./";
      }
      vector<string> file_names = ListDir(UnicodeToBytes(src_file).c_str());
      for(size_t i = 0; i < file_names.size(); ++i) {
        const wstring file_name = BytesToUnicode(file_names[i]);
        if(frontend::EndsWith(file_name, L".obs")) {
          files_paths += src_file + file_name + L',';
        }
      }
      if(!files_paths.empty()) {
        files_paths.pop_back();
      }
      src_file = files_paths;
    }
  }

  // check program target
  wstring target;
  result = arguments.find(L"tar");
  if(result != arguments.end()) {
    target = result->second;
    if(target != L"lib" && target != L"web" && target != L"exe") {
      wcerr << usage << endl;
      return COMMAND_ERROR;
    }
    argument_options.remove(L"tar");
  }

  // check program output
  wstring dest_file;
  result = arguments.find(L"dest");
  if(result == arguments.end()) {
    dest_file = src_file;
    if(dest_file.find(L',') == string::npos) {
      frontend::RemoveSubString(dest_file, L".obs");
    }
    else {
      wcerr << usage << endl;
      return COMMAND_ERROR;
    }
  }
  else {
    dest_file = result->second;
    argument_options.remove(L"dest");
  }
  
  if((target.empty() || target == L"exe") && !frontend::EndsWith(dest_file, L".obe")) {
    dest_file += L".obe";
  }
  else if(target == L"lib" && !frontend::EndsWith(dest_file, L".obl")) {
    dest_file += L".obl";
  }
    
  // check program libraries path and 'strict' usage
  wstring sys_lib_path;
  result = arguments.find(L"strict");
  if(result != arguments.end()) {
    result = arguments.find(L"lib");
    if(result != arguments.end()) {
      sys_lib_path = result->second;
      argument_options.remove(L"lib");
    }
    argument_options.remove(L"strict");
  }
  else {
    sys_lib_path = L"lang,gen_collect";
    result = arguments.find(L"lib");
    if(result != arguments.end()) {
      wstring lib_path = result->second;
      // --- START: command line clean up
      frontend::RemoveSubString(lib_path, L".obl");
      frontend::RemoveSubString(lib_path, L"gen_collect,");
      frontend::RemoveSubString(lib_path, L"gen_collect");
      frontend::RemoveSubString(lib_path, L"collect,");
      frontend::RemoveSubString(lib_path, L"collect");
      frontend::RemoveSubString(lib_path, L",,");
      // --- END
      sys_lib_path += L"," + lib_path;
      argument_options.remove(L"lib");
    }
  }

  // check for optimization flags
  wstring optimize;
  result = arguments.find(L"opt");
  if(result != arguments.end()) {
    optimize = result->second;
    if(optimize != L"s0" && optimize != L"s1" && optimize != L"s2" && optimize != L"s3") {
      wcerr << usage << endl;
      return COMMAND_ERROR;
    }
    argument_options.remove(L"opt");
  }
  
  // use alternate syntax
  bool alt_syntax = false;
  result = arguments.find(L"alt");
  if(result != arguments.end()) {
    alt_syntax = true;
    argument_options.remove(L"alt");
  }

  // check for debug flag
  bool is_debug = false;
  result = arguments.find(L"debug");
  if(result != arguments.end()) {
    is_debug = true;
    argument_options.remove(L"debug");
  }

  // check for asm flag
  bool show_asm = false;
  result = arguments.find(L"asm");
  if(result != arguments.end()) {
    show_asm = true;
    argument_options.remove(L"asm");
  }

  if(argument_options.size() != 0) {
    wcerr << usage << endl;
    return COMMAND_ERROR;
  }

  return Compile(src_file, optimize, dest_file, run_string, sys_lib_path, target, alt_syntax, is_debug, show_asm);
}

#ifdef _WIN32
vector<string> ListDir(const char* p)
{
  vector<string> files;

  string path = p;
  if(path.size() > 0 && path[path.size() - 1] == '\\') {
    path += "*";
  }
  else {
    path += "\\*";
  }

  WIN32_FIND_DATA file_data;
  HANDLE find = FindFirstFile(path.c_str(), &file_data);
  if(find == INVALID_HANDLE_VALUE) {
    return files;
  }
  else {
    files.push_back(file_data.cFileName);

    BOOL b = FindNextFile(find, &file_data);
    while(b) {
      files.push_back(file_data.cFileName);
      b = FindNextFile(find, &file_data);
    }
    FindClose(find);
  }

  return files;
}
#else
vector<string> ListDir(const char* path) {
  vector<string> files;

  struct dirent** names;
  int n = scandir(path, &names, 0, alphasort);
  if(n > 0) {
    while(n--) {
      if((strcmp(names[n]->d_name, "..") != 0) && (strcmp(names[n]->d_name, ".") != 0)) {
        files.push_back(names[n]->d_name);
      }
      free(names[n]);
    }
    free(names);
  }

  return files;
}
#endif
