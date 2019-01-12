/***************************************************************************
* Runtime debugger
*
* Copyright (c) 2010-2013 Randy Hollines
* All rights reserved.
*
* Reistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*tree.o scanner.o parser.o test.o
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
#endif

using namespace std;

namespace Runtime {
  class StackInterpreter;

  typedef struct _UserBreak {
    int line_num;
    wstring file_name;
  } UserBreak;

  /********************************
  * Source file
  ********************************/
  class SourceFile {
    wstring file_name;
    vector<wstring> lines;
    int cur_line_num;

  public:
    SourceFile(const wstring &fn, int l) {
      file_name = fn;
      cur_line_num = l;

      const string name(fn.begin(), fn.end());
      ifstream file_in (name.c_str());
      while(file_in.good()) {
        string line;
        getline(file_in, line);
        lines.push_back(BytesToUnicode(line));
      }
      file_in.close();
    }

    ~SourceFile() {
    }

    bool IsLoaded() {
      return lines.size() > 0;
    }

    bool Print(int start) {
      const int window = 5;
      int end = start + window * 2;
      start--;

      if(start >= end || start >= (int)lines.size()) {
        return false;
      }

      if(start - window > 0) {
        start -= window;
        end -= window;
      }
      else {
        start = 0;
        end = window * 2;
      }

      // find leading whitespace
      int leading = 160;
      for(int i = start; i < (int)lines.size() && i < (int)end; i++) {
        const wstring line = lines[i];
        int j = 0;
        while(j < (int)line.size() && (line[j] == L' ' || line[j] == L'\t')) {
          j++;
        }
        
        if(j != 0 && leading > j) {
          leading = j;
        }
      }
      
      for(int i = start; i < (int)lines.size() && i < (int)end; i++) {
        // trim leading whitespace
        wstring line = lines[i];
        const int line_size = (int)line.size();
        if(line_size > 0 && (line[0] == L' ' || line[0] == L'\t') && line_size > leading) {
          line = line.substr(leading);
        }
        
        if(i + 1 == cur_line_num) {
          wcout << right << L"=>" << setw(window) << (i + 1) << L": " << line << endl;
        }
        else {
          wcout << right << setw(window + 2) << (i + 1) << L": " << line << endl;
        }
      }

      return true;
    }

    const wstring& GetFileName() {
      return file_name;
    }
  };

  /********************************
  * Interactive command line
  * debugger
  ********************************/
  class Debugger {
    wstring program_file;
    wstring base_path;
    bool quit;
    // break info
    list<UserBreak*> breaks;
    int cur_line_num;
    wstring cur_file_name;
    StackFrame** cur_call_stack;
    long cur_call_stack_pos;
    bool is_error;
    bool is_next;
    bool is_next_line;
    bool is_jmp_out;
    size_t* ref_mem;
    StackClass* ref_klass;
    // interpreter variables
    vector<wstring> arguments;
    StackInterpreter* interpreter;
    StackProgram* cur_program;
    StackFrame* cur_frame;
    size_t* op_stack;
    long* stack_pos;

    // pretty prints a method
    wstring PrintMethod(StackMethod* method) {
      wstring mthd_name = method->GetName();
      size_t index = mthd_name.find_last_of(':');
      if(index != wstring::npos) {
        mthd_name.replace(index, 1, 1, '(');
        if(mthd_name[mthd_name.size() - 1] == ',') {
          mthd_name.replace(mthd_name.size() - 1, 1, 1, ')');
        }
        else {
          mthd_name += ')';
        }
      }

      index = mthd_name.find_last_of(':');
      if(index != wstring::npos) {
        mthd_name = mthd_name.substr(index + 1);
      }

      return mthd_name;
    }

    // checks to see if a file exists
    bool FileExists(const wstring &file_name, bool is_exe = false) {
      const string name(file_name.begin(), file_name.end());
      const string ending = ".obl";
      if(ending.size() > name.size()) && !std::equal(ending.rbegin(), ending.rend(), name.rbegin()) {
        return false;
      }

      if(!name.ends_with(L".obe")) {
        return false;
      }

      ifstream touch(name.c_str(), ios::binary);
      if(touch.is_open()) {
/*
        if(is_exe) {
          int magic_num;
          // version
          touch.read((char*)&magic_num, sizeof(int));
          // file type
          touch.read((char*)&magic_num, sizeof(int));
          touch.close();
          return magic_num == MAGIC_NUM_EXE;
        }
*/
        touch.close();
        return true;
      }

      return false;
    }

    // checks to see if a directory exists
    bool DirectoryExists(const wstring &wdir_name) {
      const string dir_name(wdir_name.begin(), wdir_name.end());
#ifdef _WIN32
      HANDLE file = CreateFile(dir_name.c_str(), GENERIC_READ,
        FILE_SHARE_READ, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, NULL);

      if(file == INVALID_HANDLE_VALUE) {
        return false;
      }
      CloseHandle(file);

      return true;
#else
      DIR* dir = opendir(dir_name.c_str());
      if(dir) {
        closedir(dir);
        return true;
      }

      return false;
#endif
    }

    // deletes a break point
    bool DeleteBreak(int line_num, const wstring &file_name) {
      UserBreak* user_break = FindBreak(line_num, file_name);
      if(user_break) {
        breaks.remove(user_break);
        return true;
      }

      return false;
    }

    // searches for a valid breakpoint based upon the line number provided
    UserBreak* FindBreak(int line_num, const wstring &file_name) {
      for(list<UserBreak*>::iterator iter = breaks.begin(); iter != breaks.end(); iter++) {
        UserBreak* user_break = (*iter);
        if(user_break->line_num == line_num && user_break->file_name == file_name) {
          return *iter;
        }
      }
      
      return NULL;
    }

    // adds a break
    bool AddBreak(int line_num, const wstring &file_name) {
      if(!FindBreak(line_num, file_name)) {
        UserBreak* user_break = new UserBreak;
        user_break->line_num = line_num;
        user_break->file_name = file_name;
        breaks.push_back(user_break);
        return true;
      }

      return false;
    }

    // lists all breaks
    void ListBreaks() {
      wcout << L"breaks:" << endl;
      list<UserBreak*>::iterator iter;
      for(iter = breaks.begin(); iter != breaks.end(); iter++) {
        wcout << L"  break: file='" << (*iter)->file_name << L":" << (*iter)->line_num << L"'" << endl;
      }
    }

    // prints declarations
    void PrintDeclarations(StackDclr** dclrs, int dclrs_num) {
      for(int i = 0; i < dclrs_num; i++) {
        StackDclr* dclr = dclrs[i];

        // parse name
        size_t param_name_index = dclrs[i]->name.find_last_of(':');
        const wstring &param_name = dclrs[i]->name.substr(param_name_index + 1);
        wcout << L"    parameter: name='" << param_name << L"', ";

        // parse type
        switch(dclr->type) {
        case INT_PARM:
          wcout << L"type=Int" << endl;
          break;

        case CHAR_PARM:
          wcout << L"type=Char" << endl;
          break;

        case FLOAT_PARM:
          wcout << L"type=Float" << endl;
          break;

        case BYTE_ARY_PARM:
          wcout << L"type=Byte[]" << endl;
          break;

        case CHAR_ARY_PARM:
          wcout << L"type=Char[]" << endl;
          break;

        case INT_ARY_PARM:
          wcout << L"type=Int[]" << endl;
          break;

        case FLOAT_ARY_PARM:
          wcout << L"type=Float[]" << endl;
          break;

        case OBJ_PARM:
          wcout << L"type=Object" << endl;
          break;

        case OBJ_ARY_PARM:
          wcout << L"type=Object[]" << endl;
          break;

        case FUNC_PARM:
          wcout << L"type=Function" << endl;
          break;
        }
      }
    }

    Command* ProcessCommand(const wstring &line);
    void ProcessRun();
    void ProcessExe(Load* load);
    void ProcessSrc(Load* load);
    void ProcessArgs(Load* load);
    void ProcessInfo(Info* info);
    void ProcessBreak(FilePostion* break_command);
    void ProcessBreaks();
    void ProcessDelete(FilePostion* break_command);
    void ProcessList(FilePostion* break_command);
    void ProcessPrint(Print* print);
    void ClearProgram();
    void ClearBreaks();

    void EvaluateExpression(Expression* expression);
    void EvaluateReference(Reference* &reference, MemoryContext context);
    void EvaluateInstanceReference(Reference* reference, int index);
    void EvaluateClassReference(Reference* reference, StackClass* klass, int index);
    void EvaluateByteReference(Reference* reference, int index);
    void EvaluateCharReference(Reference* reference, int index);
    void EvaluateIntFloatReference(Reference* reference, int index, bool is_float);
    void EvaluateCalculation(CalculatedExpression* expression);

  public:
    Debugger(const wstring &fn, const wstring &bp) {
      program_file = fn;
      base_path = bp;
      quit = false;
      // clear program
      interpreter = NULL;
      op_stack = NULL;
      stack_pos = NULL;
      cur_line_num = -2;
      cur_frame = NULL;
      cur_program = NULL;
      cur_call_stack = NULL;
      cur_call_stack_pos = 0;
      is_error = false;
      ref_mem = NULL;
      ref_mem = NULL;
      is_jmp_out = false;
    }

    ~Debugger() {
      ClearProgram();
      ClearBreaks();
    }

    // start debugger
    void Debug();

    // runtime callback
    void ProcessInstruction(StackInstr* instr, long ip, StackFrame** call_stack,
      long call_stack_pos, StackFrame* frame);
  };
}
#endif
