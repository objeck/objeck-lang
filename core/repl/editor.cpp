/***************************************************************************
 * REPL editor
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

#include "editor.h"

#include "../compiler/types.h"
#include "../compiler/compiler.h"
#include "../vm/vm.h"
#include "../shared/version.h"
#include "../debugger/color.h"

//
// Document
//
size_t Document::Reset()
{
  lines.clear();

  lines.push_back(Line(L"use class System.IO.Console;", Line::Type::RW_LINE));
  lines.push_back(Line(L"use Collection;", Line::Type::RW_LINE));
  lines.push_back(Line(L"", Line::Type::RW_LINE));
  lines.push_back(Line(L"class Repl {", Line::Type::RO_CLS_START_LINE));
  lines.push_back(Line(L"   function : Main(args : String[]) ~ Nil {", Line::Type::RO_FUNC_START_LINE));
  lines.push_back(Line(L"   }", Line::Type::RO_FUNC_END_LINE));
  lines.push_back(Line(L"}", Line::Type::RO_CLS_END_LINE));
  shell_count = lines.size();

  return shell_count - 2;
}

bool Document::LoadFile(const std::wstring& file)
{
  lines.clear();

  std::ifstream read_file(UnicodeToBytes(file));
  if(read_file.good()) {
    std::string line;
    while(std::getline(read_file, line)) {
      lines.push_back(Line(BytesToUnicode(line), Line::Type::RW_LINE));
    }
    read_file.close();
    name = file;

    return true;
  }
  
  return false;
}

bool Document::Save(std::wstring filename)
{
  std::ofstream write_file(UnicodeToBytes(filename));
  if(write_file.good()) {
    // list code
    std::wstringstream buffer;
    size_t ident_count = 0;
    for(auto& line : lines) {
      std::wstring line_str = line.ToString();
      Editor::Trim(line_str);

      if(!line_str.empty() && line_str.front() == L'}') {
        ident_count--;
      }

      for(size_t j = 0; j < ident_count; ++j) {
        buffer << L"   ";
      }
      buffer << line_str << std::endl;

      if(!line_str.empty() && (line_str.front() == L'{' || line_str.back() == L'{')) {
        ident_count++;
      }

    }

    write_file << UnicodeToBytes(buffer.str()) << std::endl;
    write_file.close();

    return true;
  }

  return false;
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

void Document::List(size_t cur_pos, bool all)
{
  std::wstringstream buffer;

  // before code
  if(cur_pos == 0) {
    buffer << "=> ";
  }
  else {
    buffer << "   ";
  }
  buffer << std::setw(3) << 0 << L": --- '" << name << L"' ---" << std::endl;

  // list code
  size_t index = 0, ident_count = 0;
  for(auto& line : lines) {
    ++index;

    if(all || line.GetType() == Line::Type::RW_LINE) {
      if(index == cur_pos) {
        buffer << "=> ";
      }
      else {
        buffer << "   ";
      }

      buffer << std::setw(3) << index;
      buffer << L": ";

      std::wstring line_str = line.ToString();
      Editor::Trim(line_str);

      if(!line_str.empty() && line_str.front() == L'}') {
        ident_count--;
      }

      for(size_t j = 0; j < ident_count; ++j) {
        buffer << L"   ";
      }
      buffer << line_str << std::endl;

      if(!line_str.empty() && (line_str.front() == L'{' || line_str.back() == L'{')) {
        ident_count++;
      }
    }
  }

  std::wcout << buffer.str() << std::endl;
}

bool Document::InsertLine(size_t line_num, const std::wstring line, Line::Type type)
{
  if(line_num < lines.size()) {
    size_t cur_num = 0;

    std::list<Line>::iterator iter = lines.begin();
    while(cur_num++ < line_num) {
      ++iter;
    }

    lines.insert(iter, Line(line, type));
    return true;
  }
  else if(line_num == lines.size()) {
    lines.push_back(Line(line, type));
    return true;
  }

  return false;
}

bool Document::DeleteLine(size_t line_num)
{
  if(line_num < lines.size()) {
    size_t cur_num = 0;

    std::list<Line>::iterator iter = lines.begin();
    while(cur_num++ < line_num) {
      ++iter;
    }

    if(iter->GetType() == Line::Type::RW_LINE) {
      lines.erase(iter);
      return true;
    }
  }

  return false;
}

#ifdef _DEBUG
void Document::Debug(size_t cur_pos)
{
  std::wcout << L"[DEBUG]\n---" << std::endl;

  size_t index = 0;
  for(auto& line : lines) {
    ++index;

    switch(line.GetType()) {
    case Line::Type::RO_LINE:
      std::wcout << L"[RO_LINE]       ";
      break;

    case Line::Type::RO_CLS_START_LINE:
      std::wcout << L"[RO_CLS_START]  ";
      break;

    case Line::Type::RO_CLS_END_LINE:
      std::wcout << L"[RO_CLS_END]    ";
      break;

    case Line::Type::RO_FUNC_START_LINE:
      std::wcout << L"[RO_FUNC_START] ";
      break;

    case Line::Type::RO_FUNC_END_LINE:
      std::wcout << L"[RO_FUNC_END]   ";
      break;

    case Line::Type::RW_LINE:
      std::wcout << L"[RW_LINE]       ";
      break;

    case Line::Type::RW_CLS_START_LINE:
      std::wcout << L"[RW_CLS_START]\t";
      break;

    case Line::Type::RW_CLS_END_LINE:
      std::wcout << L"[RW_CLS_END]    ";
      break;

    case Line::Type::RW_FUNC_START_LINE:
      std::wcout << L"[RW_FUNC_START] ";
      break;

    case Line::Type::RW_FUNC_END_LINE:
      std::wcout << L"[RW_FUNC_END]   ";
      break;
    }

    if(index == cur_pos) {
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
#endif

//
// Editor
//
Editor::Editor() : doc(DEFAULT_FILE_NAME)
{
  compiler_libs = USES_STRING;
  compiler_opt_level = L"s1";
  cur_pos = doc.Reset();
}

void Editor::Edit(std::wstring input, std::wstring libs, std::wstring opt, int mode, bool is_exit)
{
  Runtime::ColorInit();

  // process command line
  if(input.empty()) {
    std::wcout << Runtime::C(Runtime::CLR_BOLD) << L"Objeck REPL (" << VERSION_STRING << L")" << Runtime::C(Runtime::CLR_RESET)
               << Runtime::C(Runtime::CLR_GRAY) << L"\n['/h' for help, omit ';' to print an expression]\n---" << Runtime::C(Runtime::CLR_RESET) << std::endl;
  }
  else {
    // set libraries
    if(!libs.empty()) {
      compiler_libs = libs;
    }

    // set optimizations 
    if(!opt.empty()) {
      compiler_opt_level = opt;
    }

    // file name
    if(mode == 1) {
      input.insert(0, L"/o ");
      if(DoLoadFile(input)) {
        DoExecute();
      }
      else {
        std::wcout << L"Unable to read file: '" << input << L"'." << std::endl;
        if(!is_exit) {
          DoReset();
        }
      }
    }
    // source code
    else {
      DoInsertLine(input);
      DoExecute();
    }

    if(is_exit) {
      exit(0);
    }
  }

  std::wstring in;
  bool done = false;
  do {
    std::wcout << Runtime::C(Runtime::CLR_GREEN) << L"> " << Runtime::C(Runtime::CLR_RESET);
    std::getline(std::wcin, in);

    // command
    if(in.size() > 1 && in.front() == L'/') {
      switch(in.at(1)) {
        // quit
      case L'q':
        if(in.size() == 2) {
          done = true;
        }
        else {
          std::wcout << SYNTAX_ERROR << std::endl;
        }
        break;

        // help
      case L'h':
        if(in.size() == 2) {
          DoHelp();
        }
        else {
          std::wcout << SYNTAX_ERROR << std::endl;
        }
        break;

        // list
      case L'l':
        if(in.size() == 2) {
          doc.List(cur_pos, true);
        }
        else {
          std::wcout << SYNTAX_ERROR << std::endl;
        }
        break;

        // edit command-line arguments
      case L'a':
        DoCmdArgs(in);
        DoExecute();
        break;

        // insert single line below current position
      case L'i':
        if(in.size() == 2) {
          DoInsertBelow();
        }
        else {
          std::wcout << SYNTAX_ERROR << std::endl;
        }
        break;

        // insert multiple lines
      case L'm':
        DoInsertMultiLine(in);
        DoExecute();
        break;

        // list defined variables
      case L'v':
        if(in.size() == 2) {
          DoVars();
        }
        else {
          std::wcout << SYNTAX_ERROR << std::endl;
        }
        break;

        // clear screen
      case L'c':
        if(in.size() == 2) {
          DoClear();
        }
        else {
          std::wcout << SYNTAX_ERROR << std::endl;
        }
        break;

        // delete line
      case L'd':
        if(DoDeleteLine(in)) {
          DoExecute();
        }
        break;

        // goto line
      case L'g':
        DoGotoLine(in);
        break;

        // open file
      case L'o':
        if(DoLoadFile(in)) {
          DoExecute();
        }
        else {
          std::wcout << L"Unable to read file: '" << in << L"'." << std::endl;
          DoReset();
        }
        break;

        // save file
      case L's':
        if(DoSaveFile(in)) {
          std::wcout << L"File saved => '" << in << L".'" << std::endl;
        }
        else {
          std::wcout << L"Unable to save file: '" << in << L"'.\n  If the file was not loaded, provide a filename.\n  Ensure the location can be save to." << std::endl;
        }
        break;

        // replace line
      case L'r':
        if(DoReplaceLine(in)) {
          DoExecute();
        }
        else {
          std::wcout << "Line number read-only or invalid." << std::endl;
        }
        break;

        // edit uses
      case L'u':
        DoUseLibraries(in);
        break;

        // edit uses
      case L'p':
        DoOptLevel(in);
        break;

        // reset
      case L'x':
        DoReset();
        break;

#ifdef _DEBUG
        // debug
      case L'`':
        doc.Debug(cur_pos);
        break;
#endif

        // other
      default:
        std::wcout << SYNTAX_ERROR << std::endl;
        break;
      }
    }
    // evaluate expression / statement (handles multi-line, echo, rollback)
    else {
      DoInput(in);
    }
  }
  while(!done);

  std::wcout << "Goodbye." << std::endl;
}

void Editor::DoHelp()
{
  const wchar_t* hdr = Runtime::C(Runtime::CLR_BOLD);
  const wchar_t* cmd = Runtime::C(Runtime::CLR_CYAN);
  const wchar_t* rst = Runtime::C(Runtime::CLR_RESET);

  std::wcout << hdr << "=> Entering code" << rst << std::endl;
  std::wcout << "  end a line with " << cmd << "';'" << rst << "   add a statement to the program (re-runs the buffer)" << std::endl;
  std::wcout << "  omit the " << cmd << "';'" << rst << "          evaluate the line as an expression and print its value" << std::endl;
  std::wcout << "  open brace " << cmd << "'{'" << rst << "        keeps reading until braces balance (multi-line block)" << std::endl;
  std::wcout << hdr << "=> Commands" << rst << std::endl;
  std::wcout << "  " << cmd << "/q" << rst << ": quit                 " << cmd << "/x" << rst << ": reset buffer" << std::endl;
  std::wcout << "  " << cmd << "/h" << rst << ": help                 " << cmd << "/c" << rst << ": clear screen" << std::endl;
  std::wcout << "  " << cmd << "/l" << rst << ": list program         " << cmd << "/v" << rst << ": list defined variables" << std::endl;
  std::wcout << "  " << cmd << "/g" << rst << ": goto line            " << cmd << "/a" << rst << ": add command-line arguments" << std::endl;
  std::wcout << "  " << cmd << "/i" << rst << ": insert line below    " << cmd << "/m" << rst << ": insert multiple lines below" << std::endl;
  std::wcout << "  " << cmd << "/r" << rst << ": replace line         " << cmd << "/d" << rst << ": delete line (or range, e.g. '2-4')" << std::endl;
  std::wcout << "  " << cmd << "/u" << rst << ": set library uses     " << cmd << "/p" << rst << ": set compiler optimization" << std::endl;
  std::wcout << "  " << cmd << "/o" << rst << ": open file by name    " << cmd << "/s" << rst << ": save buffer to <name>.obs" << std::endl;
  std::wcout << "---" << std::endl;
  std::wcout << "User guide: https://objeck.org/getting_started.html" << std::endl;
}

void Editor::DoReset()
{
  compiler_libs = USES_STRING;
  compiler_opt_level = L"s1";
  cur_pos = doc.Reset();

  std::wcout << L"=> Document reset." << std::endl;
}

void Editor::DoInsertLine(std::wstring& in)
{
  if(!AppendLine(in)) {
    std::wcout << "=> Unable to insert line." << std::endl;
  }
}

void Editor::DoInsertMultiLine(std::wstring& in)
{
  if(in.size() == 2) {
    size_t line_count = 0;
    bool multi_line_done = false;
    do {
      std::wcout << L"Insert '/m' to exit] ";
      std::getline(std::wcin, in);
      if(in == L"/m") {
        multi_line_done = true;
      }
      else if(AppendLine(in)) {
        line_count++;
      }
    } while(!multi_line_done);

    std::wcout << L"=> Inserted " << line_count << " lines." << std::endl;
  }
  else {
    std::wcout << SYNTAX_ERROR << std::endl;
  }
}

void Editor::DoCmdArgs(std::wstring& in)
{
  if(in.size() == 2) {
    std::wcout << L"=> Current arguments: " << cmd_args << std::endl;
    std::wcout << L"New arguments] ";

    std::getline(std::wcin, cmd_args);
    Trim(cmd_args);
  }
  else {
    std::wcout << SYNTAX_ERROR << std::endl;
  }
}

bool Editor::DoLoadFile(std::wstring& in)
{
  if(in.size() > 2) {
    in = in.substr(2);
    if(doc.LoadFile(Trim(in))) {
      cur_pos = 1;
      return true;
    }
  }

  return false;
}

bool Editor::DoSaveFile(std::wstring& in)
{
  if(in.size() > 2 && EndsWith(in, L".obs")) {
    in = in.substr(2);

    const std::wstring filename = Trim(in);
    if(doc.Save(filename)) {
      doc.SetName(filename);
      return true;
    }
  }

  return false;
}

void Editor::DoGotoLine(std::wstring& in)
{
  if(in.size() > 2) {
    in = in.substr(2);
    try {
      const size_t line_pos = std::stoi(in);
      if(line_pos <= doc.Size()) {
        cur_pos = line_pos;
        std::wcout << "=> Cursor at line " << in << L'.' << std::endl;
      }
      else {
        std::wcout << "=> Line number " << in << L" is invalid." << std::endl;
      }
    }
    catch(std::invalid_argument& e) {
#ifdef _WIN32
      UNREFERENCED_PARAMETER(e);
#endif
      std::wcout << SYNTAX_ERROR << std::endl;
    }
  }
  else {
    std::wcout << SYNTAX_ERROR << std::endl;
  }
}

bool Editor::DoReplaceLine(std::wstring& in)
{
  if(in.size() > 2) {
    in = in.substr(2);
    try {
      const size_t line_pos = std::stoi(Trim(in));
      if(line_pos <= doc.Size()) {
        if(doc.DeleteLine(line_pos - 1)) {
          std::wcout << L"Insert " << line_pos << L"] ";
          std::getline(std::wcin, in);

          cur_pos = line_pos - 1;
          if(AppendLine(in)) {
            std::wcout << "=> Replaced line " << line_pos << L'.' << std::endl;
            return true;
          }
        }
      }
      else {
        std::wcout << "=> Line number " << in << L" is invalid or read-only." << std::endl;
      }
    }
    catch(std::invalid_argument& e) {
#ifdef _WIN32
      UNREFERENCED_PARAMETER(e);
#endif
      std::wcout << SYNTAX_ERROR << std::endl;
    }
  }
  else {
    try {
      const size_t line_pos = cur_pos;
      if(line_pos <= doc.Size()) {
        if(doc.DeleteLine(line_pos - 1)) {
          std::wcout << L"Insert " << line_pos << L"] ";
          std::getline(std::wcin, in);

          cur_pos = line_pos - 1;
          if(AppendLine(in)) {
            std::wcout << "=> Replaced line " << line_pos << L'.' << std::endl;
            return true;
          }
        }
      }
      else {
        std::wcout << "=> Line number " << in << L" is invalid or read-only." << std::endl;
      }
    }
    catch(std::invalid_argument& e) {
#ifdef _WIN32
      UNREFERENCED_PARAMETER(e);
#endif
      std::wcout << SYNTAX_ERROR << std::endl;
    }
  }

  return false;
}

bool Editor::DoDeleteLine(std::wstring& in)
{
  if(in.size() > 2) {
    in = in.substr(2);
    try {
    
      const size_t range_index = in.find('-');
      if(range_index != std::wstring::npos && range_index < in.size()) {
        std::wstring start_range_str(in.substr(0, range_index));
        const int start_line_pos = std::stoi(Trim(start_range_str));

        std::wstring end_range_str = in.substr(range_index + 1);
        const int end_line_pos = std::stoi(Trim(end_range_str));

        if(start_line_pos < end_line_pos) {
          for(int i = start_line_pos; i <= end_line_pos; ++i) {
            if(!doc.DeleteLine(start_line_pos - 1)) {
              std::wcout << "=> Line " << start_line_pos << L" is read-only." << std::endl;
              return false;
            }
          }
          
          std::wcout << L"=> Removed lines " << start_line_pos << L" through " << end_line_pos << L'.' << std::endl;
          if(start_line_pos < (int)doc.Size()) {
            cur_pos = start_line_pos - 1;
          }

          return true;
        }
        else {
          std::wcout << SYNTAX_ERROR << std::endl;
        }
      }
      else {
        const int line_pos = std::stoi(Trim(in));
        if(doc.DeleteLine(line_pos - 1)) {
          std::wcout << L"=> Removed line " << line_pos << L'.' << std::endl;
          if(line_pos < (int)doc.Size()) {
            cur_pos = line_pos - 1;
          }

          return true;
        }
        else {
          std::wcout << "=> Line " << line_pos << L" is read-only." << std::endl;
        }
      }
    }
    catch(std::invalid_argument& e) {
#ifdef _WIN32
      UNREFERENCED_PARAMETER(e);
#endif
      std::wcout << SYNTAX_ERROR << std::endl;
    }
  }
  else {
    std::wcout << SYNTAX_ERROR << std::endl;
  }

  return false;
}

void Editor::DoOptLevel(std::wstring& in)
{
  if(in.size() == 2) {
    std::wcout << L"=> Currently optimization level: " << compiler_opt_level << std::endl;
    std::wcout << L"New level (s0, s1, s2, s3): ";
    std::getline(std::wcin, in);

    in.erase(std::remove_if(in.begin(), in.end(), isspace), in.end());
    if(!in.empty()) {
      if(in == L"s0" || in == L"s1" || in == L"s2" || in == L"s3") {
        compiler_opt_level = in;
      }
      else {
        std::wcout << SYNTAX_ERROR << std::endl;
      }
    }
    else {
      std::wcout << SYNTAX_ERROR << std::endl;
    }
  }
  else {
    std::wcout << SYNTAX_ERROR << std::endl;
  }
}

void Editor::DoUseLibraries(std::wstring &in)
{
  if(in.size() == 2) {
    std::wcout << L"=> Currently used library list: " << compiler_libs << std::endl;
    std::wcout << L"New comma separated list: ";
    std::getline(std::wcin, in);

    in.erase(std::remove_if(in.begin(), in.end(), isspace), in.end());
    if(!in.empty()) {
      if(in.back() == L',') {
        in.pop_back();
      }
      compiler_libs = in;
    }
    else {
      std::wcout << SYNTAX_ERROR << std::endl;
    }
  }
  else {
    std::wcout << SYNTAX_ERROR << std::endl;
  }
}

bool Editor::DoExecute()
{
  // setup file name and source pair
  std::vector<std::pair<std::wstring, std::wstring>> file_source;
  file_source.push_back(make_pair(doc.GetName(), doc.ToString()));

  // compile code
  ObjeckLang lang(L"lang.obl," + compiler_libs);
  if(lang.Compile(file_source, compiler_opt_level)) {
    // execute
    lang.Execute(cmd_args);
    return true;
  }
  else {
    // show errors
    auto errors = lang.GetErrors();
    for(auto& error : errors) {
      std::wcout << Runtime::C(Runtime::CLR_RED) << error << Runtime::C(Runtime::CLR_RESET) << std::endl;
    }
    return false;
  }
}

bool Editor::AppendLine(std::wstring line)
{
  if(doc.InsertLine(cur_pos, line)) {
    cur_pos++;
    return true;
  }

  return false;
}

int Editor::BraceDelta(const std::wstring& line)
{
  int delta = 0;
  bool in_str = false, in_chr = false;

  for(size_t i = 0; i < line.size(); ++i) {
    const wchar_t c = line[i];

    if(in_str) {
      if(c == L'\\') { ++i; }
      else if(c == L'"') { in_str = false; }
      continue;
    }
    if(in_chr) {
      if(c == L'\\') { ++i; }
      else if(c == L'\'') { in_chr = false; }
      continue;
    }

    if(c == L'#') { break; }            // rest of line is a comment
    else if(c == L'"') { in_str = true; }
    else if(c == L'\'') { in_chr = true; }
    else if(c == L'{') { ++delta; }
    else if(c == L'}') { --delta; }
  }

  return delta;
}

void Editor::DoInput(std::wstring in)
{
  Trim(in);

  // empty line: re-run the current buffer
  if(in.empty()) {
    DoExecute();
    return;
  }

  // accumulate a block while braces stay open (multi-line if/while/function bodies)
  std::vector<std::wstring> block;
  block.push_back(in);
  int depth = BraceDelta(in);
  while(depth > 0) {
    std::wcout << Runtime::C(Runtime::CLR_GRAY) << L"... " << Runtime::C(Runtime::CLR_RESET);
    std::wstring cont;
    if(!std::getline(std::wcin, cont)) {
      break;   // EOF / piped input exhausted
    }
    block.push_back(cont);
    depth += BraceDelta(cont);
  }

  std::wstring first = block.front();
  Trim(first);
  const bool is_block = (block.size() > 1) || (!first.empty() && first.back() == L'{');
  const bool is_stmt  = (!first.empty() && first.back() == L';');
  const bool is_decl  = (first.find(L":=") != std::wstring::npos);

  // a single value-expression (no ';', no ':=', not a block): print it once and don't persist
  if(!is_block && !is_stmt && !is_decl) {
    DoEvalExpression(first);
    return;
  }

  // statement(s): insert, run, and roll back if it breaks compilation
  const size_t start_pos = cur_pos;
  size_t inserted = 0;
  for(auto& line : block) {
    std::wstring l = line;
    Trim(l);
    // a bare declaration without a terminator still gets one so it compiles
    if(!l.empty() && l.back() != L';' && l.back() != L'{' && l.back() != L'}' &&
       l.find(L":=") != std::wstring::npos) {
      l += L';';
    }
    if(AppendLine(l)) {
      inserted++;
    }
  }

  if(!DoExecute()) {
    for(size_t i = 0; i < inserted; ++i) {
      doc.DeleteLine(start_pos);
    }
    cur_pos = start_pos;
    std::wcout << Runtime::C(Runtime::CLR_GRAY) << L"=> line discarded; buffer unchanged" << Runtime::C(Runtime::CLR_RESET) << std::endl;
  }
}

void Editor::DoEvalExpression(const std::wstring& expr)
{
  const size_t p = cur_pos;
  const std::wstring wrapped = L"(" + expr + L")->ToString()->PrintLine();";
  if(AppendLine(wrapped)) {
    DoExecute();          // prints the value, or shows a compile error in red
    doc.DeleteLine(p);    // one-shot: never persisted, so it can't replay
    cur_pos = p;
  }
}

void Editor::DoVars()
{
  // No live VM symbol table is kept between runs, so report the declarations
  // currently in the buffer (lines containing ':=').
  const std::wstring src = doc.ToString();
  std::wstringstream ss(src);
  std::wstring line;
  std::vector<std::pair<std::wstring, std::wstring>> vars;

  while(std::getline(ss, line)) {
    const size_t a = line.find(L":=");
    if(a != std::wstring::npos) {
      std::wstring name = line.substr(0, a);
      std::wstring val = line.substr(a + 2);
      Trim(name);
      Trim(val);
      if(!val.empty() && val.back() == L';') {
        val.pop_back();
        Trim(val);
      }
      if(!name.empty()) {
        vars.push_back(make_pair(name, val));
      }
    }
  }

  std::wcout << Runtime::C(Runtime::CLR_BOLD) << L"=> Variables" << Runtime::C(Runtime::CLR_RESET) << std::endl;
  if(vars.empty()) {
    std::wcout << Runtime::C(Runtime::CLR_GRAY) << L"  (none)" << Runtime::C(Runtime::CLR_RESET) << std::endl;
    return;
  }
  for(auto& v : vars) {
    std::wcout << L"  " << Runtime::C(Runtime::CLR_CYAN) << v.first << Runtime::C(Runtime::CLR_RESET)
               << L" := " << v.second << std::endl;
  }
}

void Editor::DoClear()
{
  // ANSI clear-screen + home; C() returns the sequence only when VT/color is active
  std::wcout << Runtime::C(L"\033[2J\033[H") << std::flush;
}

void Editor::DoInsertBelow()
{
  std::wcout << Runtime::C(Runtime::CLR_GRAY) << L"Insert] " << Runtime::C(Runtime::CLR_RESET);
  std::wstring line;
  std::getline(std::wcin, line);
  DoInput(line);
}
