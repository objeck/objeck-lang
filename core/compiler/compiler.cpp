/***************************************************************************
 * Starting point of the language compiler
 *
 * Copyright (c) 2025, Randy Hollines
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
#include <fcntl.h>
#else
#include <dirent.h>
#endif
#include "compiler.h"
#include "types.h"

#define SUCCESS 0
#define COMMAND_ERROR 1
#define PARSE_ERROR 2
#define CONTEXT_ERROR 3

/****************************
 * Starts the compilation process
 ****************************/
int Compile(const std::wstring& src_files, const std::wstring& opt, const std::wstring& dest_file, std::vector<std::pair<std::wstring, std::wstring> > &programs, const std::wstring &sys_lib_path, std::wstring &target, bool alt_syntax, bool is_debug, bool show_asm) {
  // parse source code
  Parser parser(src_files, alt_syntax, programs);
  if(parser.Parse()) {
    const bool is_lib = target == L"lib";

    // analyze parse tree
    ParsedProgram* program = parser.GetProgram();
    ContextAnalyzer analyzer(program, sys_lib_path, is_lib);
    if(analyzer.Analyze(is_lib)) {
      // emit intermediate code
      IntermediateEmitter intermediate(program, is_lib, is_debug);
      intermediate.Translate();
      // intermediate optimizer
      ItermediateOptimizer optimizer(intermediate.GetProgram(), intermediate.GetUnconditionalLabel(), opt, is_lib, is_debug);
      optimizer.Optimize();
      // emit target code
      FileEmitter target(optimizer.GetProgram(), is_lib, is_debug, show_asm, dest_file);
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
int OptionsCompile(std::map<const std::wstring, std::wstring>& arguments, std::list<std::wstring>& argument_options, const std::wstring usage)
{
  // set UTF-8 environment
#ifdef _WIN32
#ifndef _MSYS2_CLANG  
  if(_setmode(_fileno(stdin), _O_U8TEXT) < 0) {
    std::wcerr << "Unable to initialize I/O subsystem" << std::endl;
    exit(1);
  }

  if(_setmode(_fileno(stdout), _O_U8TEXT) < 0) {
    std::wcerr << "Unable to initialize I/O subsystem" << std::endl;
    exit(1);
  }
#endif  
#else
  setlocale(LC_ALL, "");
  setlocale(LC_CTYPE, "UTF-8");
#endif
  
  // check for optimize flag
  std::map<const std::wstring, std::wstring>::iterator result = arguments.find(L"ver");
  if(result != arguments.end()) {

#if defined(_WIN64) && defined(_WIN32) && defined(_M_ARM64)
    std::wcout << VERSION_STRING << L" Objeck (arm64 Windows)" << std::endl;
#elif defined(_WIN64) && defined(_WIN32)
    std::wcout << VERSION_STRING << L" Objeck (Windows x86_64)" << std::endl;
#elif _WIN32
    std::wcout << VERSION_STRING << L" Objeck (Windows x86)" << std::endl;
#elif _OSX
#ifdef _ARM64
    std::wcout << VERSION_STRING << L" Objeck (macOS ARM64)" << std::endl;
#else
    std::wcout << VERSION_STRING << L" Objeck (macOS x86_64)" << std::endl;
#endif
#elif _X64
    std::wcout << VERSION_STRING << L" Objeck (Linux x86_64)" << std::endl;
#elif _ARM32
    std::wcout << VERSION_STRING << L" Objeck (Linux ARMv7)" << std::endl;
#else
    std::wcout << VERSION_STRING << L" Objeck (Linux x86)" << std::endl;
#endif 
    std::wcout << L"---" << std::endl;
    std::wcout << L"Copyright (c) 2025, Randy Hollines" << std::endl;
    std::wcout << L"This is free software; see the source for copying conditions.There is NO" << std::endl;
    std::wcout << L"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE." << std::endl;
    argument_options.remove(L"ver");
    exit(0);
  }

  // check source input
  std::wstring program;
  std::wstring src_files;
  std::wstring dest_file;

  result = arguments.find(L"src");
  if(result == arguments.end()) {
    result = arguments.find(L"in");
    if(result == arguments.end()) {
      std::wcerr << usage << std::endl;
      return COMMAND_ERROR;
    }
    program = L"class Objeck { function : Main(args : String[]) ~ Nil {";
    program += arguments[L"in"];
    program += L"} }";
    argument_options.remove(L"in");
    dest_file = L"program";
  }
  else {
    argument_options.remove(L"src");
    src_files = result->second;
    
    // parse file name w/o extension
    if(!frontend::EndsWith(src_files, L".obs")) {
      src_files += L".obs";
    }
    // parse file wild card
    else if(frontend::EndsWith(src_files, L"*.obs")) {
      std::wstring dir_path;
      std::wstring files_paths;
      frontend::RemoveSubString(src_files, L"*.obs");
      if(src_files.empty()) {
        src_files = L"./";
      }
      std::vector<std::string> file_names = ListDir(UnicodeToBytes(src_files).c_str());
      for(size_t i = 0; i < file_names.size(); ++i) {
        const std::wstring file_name = BytesToUnicode(file_names[i]);
        if(frontend::EndsWith(file_name, L".obs")) {
          files_paths += src_files + file_name + L',';
        }
      }

      if(files_paths.empty()) {
        std::wcerr << L"unknown:(0,0): Unable to open source files" << std::endl;
        exit(1);
      }
      else {
        files_paths.pop_back();
      }
      src_files = files_paths;
    }
  }

  // check program target
  std::wstring target;
  result = arguments.find(L"tar");
  if(result != arguments.end()) {
    target = result->second;
    if(target != L"lib" && target != L"exe") {
      std::wcerr << usage << std::endl;
      return COMMAND_ERROR;
    }
    argument_options.remove(L"tar");
  }

  // check program output
  result = arguments.find(L"dest");
  if(result == arguments.end()) {
    if(!src_files.empty()) {
      dest_file = src_files;
    }
    if(dest_file.find(L',') == std::string::npos) {
      frontend::RemoveSubString(dest_file, L".obs");
    }
    else {
      std::wcerr << usage << std::endl;
      return COMMAND_ERROR;
    }
  }
  else {
    dest_file = result->second;
    argument_options.remove(L"dest");
  }

  if(dest_file.empty()) {
    std::wcerr << usage << std::endl;
    return COMMAND_ERROR;
  }
  
  if((target.empty() || target == L"exe") && !frontend::EndsWith(dest_file, L".obe")) {
    dest_file += L".obe";
  }
  else if(target == L"lib" && !frontend::EndsWith(dest_file, L".obl")) {
    dest_file += L".obl";
  }
    
  // check program libraries path and 'strict' usage
  std::wstring sys_lib_path;
  result = arguments.find(L"strict");
  if(result != arguments.end()) {
    result = arguments.find(L"lib");
    if(result != arguments.end()) {
      sys_lib_path = result->second;
      if(sys_lib_path.empty()) {
        std::wcerr << usage << std::endl;
        return COMMAND_ERROR;
      }
      argument_options.remove(L"lib");
    }
    argument_options.remove(L"strict");
  }
  else {
    sys_lib_path = L"lang,gen_collect";
    result = arguments.find(L"lib");
    if(result != arguments.end()) {
      std::wstring lib_path = result->second;
      if(lib_path.empty()) {
        std::wcerr << usage << std::endl;
        return COMMAND_ERROR;
      }

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
  std::wstring optimize;
  result = arguments.find(L"opt");
  if(result != arguments.end()) {
    optimize = result->second;
    if(optimize != L"s0" && optimize != L"s1" && optimize != L"s2" && optimize != L"s3") {
      std::wcerr << usage << std::endl;
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
    std::wcerr << usage << std::endl;
    return COMMAND_ERROR;
  }

  std::vector<std::pair<std::wstring, std::wstring> > programs;
  programs.push_back(make_pair(L"blob://program.obs", program));

  return Compile(src_files, optimize, dest_file, programs, sys_lib_path, target, alt_syntax, is_debug, show_asm);
}

#ifdef _WIN32
std::vector<std::string> ListDir(const char* p)
{
  std::vector<std::string> files;

  std::string path = p;
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
std::vector<std::string> ListDir(const char* path) {
  std::vector<std::string> files;

  struct dirent** names = nullptr;
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
