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

#include "../compiler/types.h"
#include "../compiler/compiler.h"
#include "../vm/vm.h"
#include "../shared/version.h"

//
// Document
//
size_t Document::Reset()
{
  lines.clear();

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

bool Document::Save()
{
  std::ofstream write_file(UnicodeToBytes(name));
  if(write_file.good()) {
    for(auto& line : lines) {
      write_file << UnicodeToBytes(line.ToString()) << std::endl;
    }
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
  // before code
  if(cur_pos == 0) {
    std::wcout << "=> ";
  }
  else {
    std::wcout << "   ";
  }
  std::wcout << L"0: --- '" << name << L"' --- " << std::endl;

  // list code
  size_t index = 0, ident_count = 0;
  for(auto& line : lines) {
    ++index;

    if(all || line.GetType() == Line::Type::RW_LINE) {
      if(index == cur_pos) {
        std::wcout << "=> ";
      }
      else {
        std::wcout << "   ";
      }

      std::wcout << index;
      std::wcout << L": ";

      std::wstring line_str = line.ToString();
      Editor::Trim(line_str);

      if(!line_str.empty() && line_str.front() == L'}') {
        ident_count--;
      }

      for(size_t j = 0; j < ident_count; ++j) {
        std::wcout << L"   ";
      }
      std::wcout << line_str << std::endl;

      if(!line_str.empty() && (line_str.front() == L'{' || line_str.back() == L'{')) {
        ident_count++;
      }
    }
  }
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
  lib_uses = USES_STRING;
  cur_pos = doc.Reset();
}

void Editor::Edit()
{
  std::wcout << L"Objeck REPL (" << VERSION_STRING << L")\n['/h' for help]\n---" << std::endl;

  std::wstring in; bool done = false;
  do {
    std::wcout << L"> ";
    std::getline(std::wcin, in);

    // command
    if(in.size() > 1 && in.front() == L'/') {
      switch(in.at(1)) {
        // quit
      case L'q':
        done = true;
        break;

        // help
      case L'h':
        DoHelp();
        break;

        // list
      case L'l':
        doc.List(cur_pos, true);
        break;

        // edit command-line arguments
      case L'a':
        DoCmdArgs(in);
        DoExecute();
        break;

        // insert multiple lines
      case L'm':
        DoInsertMultiLine(in);
        DoExecute();
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
          std::wcout << L"Unable to read file." << std::endl;
        }
        break;

        // save file
      case L's':
        if(DoSaveFile(in)) {
          std::wcout << L"File saved => '" << doc.GetName() << L".'" << std::endl;
        }
        else {
          std::wcout << L"Unable to save file: '" << doc.GetName() << L"'.\n  If the file was not loaded, provide a filename.\n  Ensure the location can be save to." << std::endl;
        }
        break;

        // replace line
      case L'r':
        if(DoReplaceLine(in)) {
          DoExecute();
        }
        break;

        // edit uses
      case L'u':
        DoUseLibraries(in);
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
    // insert and execute
    else if(!in.empty()) {
      DoInsertLine(in);
      DoExecute();
    }
    else {
      DoExecute();
    }
  }
  while(!done);

  std::wcout << "Goodbye." << std::endl;
}

void Editor::DoHelp()
{
  std::wcout << "=> Commands" << std::endl;
  std::wcout << "  /q: quit" << std::endl;
  std::wcout << "  /h: help" << std::endl;
  std::wcout << "  /l: list program" << std::endl;
  std::wcout << "  /g: goto line" << std::endl;
  std::wcout << "  /i: insert line below" << std::endl;
  std::wcout << "  /m: insert multiple lines below" << std::endl;
  std::wcout << "  /r: replace line" << std::endl;
  std::wcout << "  /d: delete line" << std::endl;
  std::wcout << "  /u: change library use statements" << std::endl;
  std::wcout << "  /o: open file by name" << std::endl;
  std::wcout << "  /s: save buffer or current file" << std::endl;
  std::wcout << "  /x: reset" << std::endl;
  std::wcout << "---" << std::endl;
  std::wcout << "User guide: https://objeck.org/getting_started.html" << std::endl;
}

void Editor::DoReset()
{
  lib_uses = USES_STRING;
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
  } 
  while(!multi_line_done);

  std::wcout << L"=> Inserted " << line_count << " lines." << std::endl;
}

void Editor::DoCmdArgs(std::wstring& in)
{
  std::wcout << L"=> Current arguments: " << cmd_args << std::endl;
  std::wcout << L"New arguments] ";
  
  std::getline(std::wcin, cmd_args);
}

bool Editor::DoLoadFile(std::wstring& in)
{
  if(in.size() > 2) {
    in = in.substr(3);
    if(doc.LoadFile(in)) {
      cur_pos = 1;
      return true;
    }
  }

  return false;
}

bool Editor::DoSaveFile(std::wstring& in)
{
  if(in.size() > 2 && EndsWith(in, L".obs")) {
    if(doc.Save()) {
      doc.SetName(in.substr(3));
      return true;
    }
  }
  else if(doc.GetName() != DEFAULT_FILE_NAME && doc.Save()) {
    return true;
  }

  return false;
}

void Editor::DoGotoLine(std::wstring& in)
{
  if(in.size() > 2) {
    in = in.substr(3);
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
    in = in.substr(3);
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
    std::wcout << SYNTAX_ERROR << std::endl;
  }

  return false;
}

bool Editor::DoDeleteLine(std::wstring& in)
{
  if(in.size() > 2) {
    in = in.substr(3);
    try {
      const int line_pos = std::stoi(Trim(in));
      if(doc.DeleteLine(line_pos - 1)) {
        std::wcout << L"=> Removed line " << line_pos << L'.' << std::endl;
        return true;
      }
      else {
        std::wcout << "=> Line " << in << L" is read-only." << std::endl;
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

void Editor::DoUseLibraries(std::wstring &in)
{
  std::wcout << L"=> Currently used library list: " << lib_uses << std::endl;
  std::wcout << L"New list] ";
  std::getline(std::wcin, in);

  in.erase(std::remove_if(in.begin(), in.end(), isspace), in.end());
  if(!in.empty()) {
    if(in.back() == L',') {
      in.pop_back();
    }
    lib_uses = in;
  }
  else {
    std::wcout << SYNTAX_ERROR << std::endl;
  }
}

void Editor::DoExecute()
{
  ObjeckLang lang(doc.ToString(), L"lang.obl," + lib_uses, cmd_args);
  if(lang.Compile(doc.GetName())) {
    lang.Execute();
  }
  else {
    auto errors = lang.GetErrors();
    for(auto& error : errors) {
      std::wcout << error << std::endl;
    }
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
