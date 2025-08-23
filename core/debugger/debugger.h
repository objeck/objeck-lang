/***************************************************************************
* Runtime debugger
*
* Copyright (c) 2010-2019 Randy Hollines
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*tree.o scanner.o parser.o test.o
* - Redistributions of source code must retain the above copyright
* notice, this list of conditions and the following disclaimer.
* - Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in
* the documentation and/or other materials provided with the distribution.
* - Neither the name of the Objeck Team nor the names of its
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

#ifndef __DEBUGGER_H__
#define __DEBUGGER_H__

#include "../vm/common.h"
#include "../vm/loader.h"
#include "../vm/interpreter.h"
#include "../vm/../shared/version.h"
#include "tree.h"
#include "parser.h"
#include <iomanip>
#ifdef _WIN32
#include "windows.h"
#include <fcntl.h>
#include <io.h>
#else
#include <dirent.h>
#include <readline/readline.h>
#include <readline/history.h>
#endif

namespace Runtime {
  class StackInterpreter;
  class Debugger;
  
  typedef struct _UserBreak {
    int line_num;
    std::wstring file_name;
  } UserBreak;

  /********************************
  * Source file
  ********************************/
  class SourceFile {
    std::wstring file_name;
    std::vector<std::wstring> lines;
    int cur_line_num;
    Debugger* debugger;

  public:
    SourceFile(const std::wstring &fn, int l, class Debugger* d) {
      file_name = fn;
      cur_line_num = l;
      debugger = d;

      const std::string name = UnicodeToBytes(fn);
      std::ifstream file_in (name.c_str());
      while(file_in.good()) {
        std::string line;
        std::getline(file_in, line);
        lines.push_back(BytesToUnicode(line));
      }
      file_in.close();
    }

    ~SourceFile() {
    }

    bool IsLoaded() {
      return lines.size() > 0;
    }

    bool Print(int start);

    const std::wstring& GetFileName() {
      return file_name;
    }
  };

  /********************************
  * Interactive command line
  * debugger
  ********************************/
  class Debugger {
    std::wstring program_file_param;
    std::wstring base_path_param;
    std::wstring args_param;
    bool quit;
    // break info
    std::list<UserBreak*> breaks;
    int cur_line_num;
    std::wstring cur_file_name;
    StackFrame** cur_call_stack;
    long cur_call_stack_pos;
    long jump_stack_pos;
    bool is_error;
    bool is_step_into;
    bool is_next_line;
    bool is_step_out;
    int continue_state;
    size_t* ref_mem;
    StackClass* ref_klass;
    // interpreter variables
    std::vector<std::wstring> arguments;
    StackInterpreter* interpreter;
    StackProgram* cur_program;
    StackFrame* cur_frame;
    size_t* op_stack;
    size_t* stack_pos;
    Loader* loader;

    // pretty prints a method
    std::wstring PrintMethod(StackMethod* method);

    // checks to see if a file exists
    bool FileExists(const std::wstring &file_name, bool is_exe = false);

    // checks to see if a directory exists
    bool DirectoryExists(const std::wstring &wdir_name);

    // deletes a break point
    bool DeleteBreak(int line_num, const std::wstring &file_name);

    // searches for a valid breakpoint based upon the line number provided
    UserBreak* FindBreak(int line_num, const std::wstring& file_name);

    // adds a break
    bool AddBreak(int line_num, const std::wstring &file_name);

    // lists all breaks
    void ListBreaks();

    // prints declarations
    void PrintDeclarations(StackDclr** dclrs, int dclrs_num);
  
    void ClearProgram(bool clear_loader = true);
    void DoLoad();
    void ClearReload() {
      ClearProgram(false);
      DoLoad();
    }

    Command* ProcessCommand(const std::wstring &line);
    void ProcessRun();
    void ProcessBin(Load* load);
    void ProcessSrc(Load* load);
    void ProcessArgs(Load* load);
    void ProcessArgs(const std::wstring& temp);
    void ProcessInfo(Info* info);
    void ProcessBreak(FilePostion* break_command);
    void ProcessBreaks();
    void ProcessDelete(FilePostion* break_command);
    void ProcessPrint(Print* print);
    void ClearBreaks();
    void EvaluateExpression(Expression* expression);
    void EvaluateReference(Reference* &reference, MemoryContext context);
    void EvaluateInstanceReference(Reference* reference, int index);
    void EvaluateClassReference(Reference* reference, StackClass* klass, int index);
    void EvaluateByteReference(Reference* reference, int index);
    void EvaluateCharReference(Reference* reference, int index);
    void EvaluateIntFloatReference(Reference* reference, int index, bool is_float);
    void EvaluateCalculation(CalculatedExpression* expression);

    // utility functions
    std::wstring ToFloat(size_t value) {
      wchar_t buffer[16];

      if(value > 1000000) {
#ifdef _WIN32
        swprintf_s(buffer, L"%.2fM", (double)value / (double)1000000);
#else
        swprintf(buffer, 15, L"%.2fM", (double)value / (double)1000000);
#endif
      }
      else {
#ifdef _WIN32
        swprintf_s(buffer, L"%.2fK", (double)value / (double)1000);
#else
        swprintf(buffer, 15, L"%.2fK", (double)value / (double)1000);
#endif
      }

      return buffer;
    }

    std::wstring Trim(const std::wstring& input, const std::wstring& whitespace = L" \t\r\n") {
      const size_t start = input.find_first_not_of(whitespace);
      if(start != std::wstring::npos) {
        const size_t end = input.find_last_not_of(whitespace);
        return input.substr(start, end - start + 1);
      }

      return L"";
    }

    void ReadLine(std::wstring &output) {
// #ifdef _WIN32
      std::wcout << L"> ";
      std::getline(std::wcin, output);
/*
#else
      char* input = readline(nullptr);
      if(input) {
        if(strlen(input) > 0) {
          add_history(input);
          BytesToUnicode(input, output);
        }
        free(input);
        input = nullptr;
      }
#endif
*/
    }

  public:
    Debugger(const std::wstring &fn, const std::wstring &bp, const std::wstring &ap) {
      program_file_param = fn;
      base_path_param = bp;
      args_param = ap;
      quit = false;
      // clear program
      interpreter = nullptr;
      op_stack = nullptr;
      stack_pos = nullptr;
      cur_line_num = -1;
      cur_frame = nullptr;
      cur_program = nullptr;
      cur_call_stack = nullptr;
      cur_call_stack_pos = jump_stack_pos = 0;
      is_error = false;
      ref_mem = nullptr;
      ref_mem = nullptr;
      is_step_out = false;
      loader = nullptr;
    }

    ~Debugger() {
      ClearProgram();
      ClearBreaks();
    }

    // start debugger
    void Debug();

    // searches for a valid breakpoint based upon the line number provided
    UserBreak* FindBreak(int line_num);

    // runtime callback
    void ProcessInstruction(StackInstr* instr, long ip, StackFrame** call_stack, long call_stack_pos, StackFrame* frame);
  };
}

#endif
