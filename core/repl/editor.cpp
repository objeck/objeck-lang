/***************************************************************************
 * REPL editor
 *
 * Copyright (c) 2023, Randy Hollines
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

#include "editor.h"

#include "../compiler/compiler.h"
#include "../compiler/types.h"

#include "../vm/vm.h"

//
// Line
//
const std::wstring Line::ToString() {
  return line;
}

const Line::Type Line::GetType() {
  return type;
}

//
// Document
//
Document::Document()
{
}

size_t Document::Lines()
{
  return lines.size();
}

size_t Document::Reset()
{
  lines.clear();

  lines.push_back(Line(L"class Shell {", Line::Type::READ_ONLY));
  lines.push_back(Line(L"  function : Main(args : String[]) ~ Nil {", Line::Type::READ_ONLY));
  lines.push_back(Line(L"  }", Line::Type::READ_ONLY));
  lines.push_back(Line(L"}", Line::Type::READ_ONLY));

  return 3;
}

std::wstring Document::ToString()
{
  std::wstring buffer;

  for(auto &line : lines) {
    buffer += line.ToString();
    buffer += L'\n';
  }

  return buffer;
}

void Document::List(size_t cur_pos)
{
  std::wcout << L"---" << std::endl;

  auto index = 0;
  for(auto& line : lines) {
    if(++index == cur_pos) {
      std::wcout << "=> ";
    }
    else {
      std::wcout << "   ";
    }

    std::wcout << index;
    std::wcout << L": ";
    std::wcout << line.ToString() << std::endl;
  }
}

bool Document::Insert(size_t line_num, const std::wstring line)
{
  if(line_num < lines.size()) {
    size_t cur_num = 1;

    std::list<Line>::iterator iter = lines.begin();
    while(cur_num++ < line_num) {
      ++iter;
    }

    lines.insert(iter, Line(line, Line::Type::READ_WRITE));
    return true;
  }

  return false;
}

bool Document::Delete(size_t line_num)
{
  if(line_num < lines.size()) {
    size_t cur_num = 1;

    std::list<Line>::iterator iter = lines.begin();
    while(cur_num++ < line_num) {
      ++iter;
    }

    if(iter->GetType() == Line::Type::READ_WRITE) {
      lines.erase(iter);
      return true;
    }
  }

  return false;
}

//
// Editor
//
Editor::Editor()
{
  cur_pos = doc.Reset();
}

void Editor::Edit()
{
  bool done = false;
  std::wstring in;
  do {
    std::wcout << L"> ";
    std::getline(std::wcin, in);

    // quit
    if(in == L"/q" || in == L"/quit") {
      done = true;
    }
    // help
    else if(in == L"/h" || in == L"/help") {
      std::wcout << "Commands:" << std::endl;
      std::wcout << "  /q: quit" << std::endl;
      std::wcout << "  /h: help" << std::endl;
      std::wcout << "  /l: list" << std::endl;
      std::wcout << "  /x: reset" << std::endl;
      std::wcout << "  /g: goto line" << std::endl;
      std::wcout << "  /i: insert line" << std::endl;
      std::wcout << "  /m: insert multiple lines" << std::endl;
      std::wcout << "  /d: delete line" << std::endl;
    }
    // list
    else if(in == L"/l") {
      doc.List(cur_pos);
    }
    // build and run
    else if(in == L"/r") {
      auto code = Compile();
      if(code) {
        Execute(code);
      }
    }
    // reset
    else if(in == L"/x") {
      doc.Reset();
      std::wcout << L"Document reset." << std::endl;
    }
    // delete line
    else if(StartsWith(in, L"/d ")) {
      const size_t offset = in.find_last_of(L' ');
      if(offset != std::wstring::npos) {
        const std::wstring line_pos_str = in.substr(offset);
        if(doc.Delete(std::stoi(line_pos_str))) {
          cur_pos--;
          std::wcout << L"Deleted line " << line_pos_str << L'.' << std::endl;
        }
        else {
          std::wcout << "Line " << line_pos_str << L" is read-only." << std::endl;
        }
      }
      else {
        std::wcout << SYNTAX_ERROR << std::endl;
      }
    }
    // goto line
    else if(StartsWith(in, L"/g ")) {
      const size_t offset = in.find_last_of(L' ');
      if(offset != std::wstring::npos) {
        const std::wstring line_pos_str = in.substr(offset);
        const size_t line_pos = std::stoi(line_pos_str);
        if(line_pos < doc.Lines()) {
          cur_pos = line_pos;
          std::wcout << "Cursor at line " << line_pos_str << L'.' << std::endl;
        }
        else {
          std::wcout << SYNTAX_ERROR << std::endl;
        }
      }
      else {
        std::wcout << SYNTAX_ERROR << std::endl;
      }
    }
    // insert line
    else if(StartsWith(in, L"/i ")) {
      if(Append(in.substr(3))) {
        std::wcout << "Line inserted." << std::endl;
      }
    }
    // insert multiple lines
    else if(StartsWith(in, L"/m")) {
      size_t line_count = 0;
      bool multi_done = false;
      do {
        std::wcout << L"] ";
        std::getline(std::wcin, in);
        if(in == L"/m") {
          multi_done = true;
        }
        else {
          if(Append(in)) {
            line_count++;
          }
        }
      } 
      while(!multi_done);

      std::wcout << line_count << " lines inserted." << std::endl;
    }
    else {
      std::wcout << SYNTAX_ERROR << std::endl;
    }
  }
  while(!done);

  std::wcout << "Goodbye." << std::endl;
}

char* Editor::Compile()
{
  const std::wstring input = doc.ToString();

  const bool is_debug = false;
  const bool is_lib = false;
  const bool show_asm = false;
  const std::wstring opt = L"s3";
  const std::wstring sys_lib_path = L"lang.obl,gen_collect.obl";
  const std::wstring filename = L"blob://program";

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
      return target.Get();
    }
  }

  return nullptr;
}

void Editor::Execute(char* code)
{
  wchar_t** commands = nullptr;
  int argc = 0;

  Loader loader(code);
  loader.Load();

  // execute
  size_t* op_stack = new size_t[OP_STACK_SIZE];
  long* stack_pos = new long;
  (*stack_pos) = 0;

#ifdef _TIMING
  clock_t start = clock();
#endif
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

  delete[] code;
  code = nullptr;
}

bool Editor::Append(std::wstring line)
{
  if(doc.Insert(cur_pos, line)) {
    cur_pos++;
    return true;
  }
  
  return false;
}