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
  lines.push_back(Line(L"  function : Main(args : String[]) ~ Nil {", Line::Type::RO_FUNC_START_LINE));
  lines.push_back(Line(L"  }", Line::Type::RO_FUNC_END_LINE));
  lines.push_back(Line(L"}", Line::Type::RO_CLS_END_LINE));
  shell_count = lines.size();

  return shell_count - 1;
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
  if(!all && lines.size() == shell_count) {
    std::wcout << L"[No code]" << std::endl;
  }
  else {
    if(all) {
      std::wcout << L"[All code]" << std::endl;
    }

    size_t index = 0;
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
        std::wcout << line.ToString() << std::endl;
      }
    }
  }
}

bool Document::InsertLine(size_t line_num, const std::wstring line, int padding, Line::Type type)
{
  if(line_num > 0 && line_num < lines.size()) {
    size_t cur_num = 0;

    std::list<Line>::iterator iter = lines.begin();
    while(cur_num++ < line_num) {
      ++iter;
    }

    std::wstring padding_str;
    while(padding--) {
      padding_str += L' ';
    }

    lines.insert(iter, Line(padding_str + line, type));
    return true;
  }

  return false;
}

bool Document::DeleteLine(size_t line_num)
{
  if(line_num > 0 && line_num < lines.size()) {
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
Editor::Editor()
{
  lib_uses = L"lang.obl,gen_collect.obl";
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
        doc.List(cur_pos, false);
        break;

        // list all
      case L'a':
        doc.List(cur_pos, true);
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
      case L'o':
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
  }
  while(!done);

  std::wcout << "Goodbye." << std::endl;
}

void Editor::DoHelp()
{
  std::wcout << "=> Commands" << std::endl;
  std::wcout << "  /q: quit" << std::endl;
  std::wcout << "  /h: help" << std::endl;
  std::wcout << "  /l: lists lines" << std::endl;
  std::wcout << "  /a: lists full program" << std::endl;
  std::wcout << "  /o: reset" << std::endl;
  std::wcout << "  /g: goto line" << std::endl;
  std::wcout << "  /i: insert line above" << std::endl;
  std::wcout << "  /m: insert multiple lines above" << std::endl;
  std::wcout << "  /f: insert function or method" << std::endl;
  std::wcout << "  /r: replace line" << std::endl;
  std::wcout << "  /d: delete line" << std::endl;
  std::wcout << "  /u: edit library use statements" << std::endl;
  std::wcout << "  /x: execute program" << std::endl;
  std::wcout << "---" << std::endl;
  std::wcout << "Online guide: https://objeck.org/getting_started.html" << std::endl;
}

void Editor::DoReset()
{
  lib_uses = L"lang.obl,gen_collect.obl";
  doc.Reset();

  std::wcout << L"=> Document reset." << std::endl;
  cur_pos = 3;
}

void Editor::DoInsertLine(std::wstring in)
{
  if(!AppendLine(in, 4)) {
    std::wcout << "=> Unable to insert line." << std::endl;
  }
}

void Editor::DoInsertMultiLine(std::wstring in)
{
  size_t line_count = 0;
  bool multi_line_done = false;
  do {
    std::wcout << L"Insert '/m' to exit] ";
    std::getline(std::wcin, in);
    if(in == L"/m") {
      multi_line_done = true;
    }
    else {
      if(AppendLine(in, 4)) {
        line_count++;
      }
    }
  } 
  while(!multi_line_done);

  std::wcout << L"=> Inserted " << line_count << " lines." << std::endl;
}

void Editor::DoGotoLine(std::wstring& in)
{
  if(in.size() > 2) {
    in = in.substr(2);
    try {
      const size_t line_pos = std::stoi(in);
      if(line_pos - 1 < doc.Size()) {
        cur_pos = line_pos;
        std::wcout << "=> Cursor at line " << in << L'.' << std::endl;
      }
      else {
        std::wcout << "=> Line number " << in << L" is invalid." << std::endl;
      }
    }
    catch(std::invalid_argument& e) {
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
      if(doc.DeleteLine(line_pos - 1)) {
        cur_pos = line_pos;
        std::wcout << L"Insert] " << line_pos << L"] ";
        std::getline(std::wcin, in);
        if(AppendLine(in, 4)) {
          std::wcout << "=> Replaced line " << line_pos << L'.' << std::endl;
          return true;
        }
      }
      else {
        std::wcout << "=> Line number " << in << L" is invalid or read-only." << std::endl;
      }
    }
    catch(std::invalid_argument& e) {
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
    in = in.substr(2);
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
      std::wcout << SYNTAX_ERROR << std::endl;
    }
  }
  else {
    std::wcout << SYNTAX_ERROR << std::endl;
  }

  return false;
}

void Editor::DoUseLibraries(std::wstring in)
{
  std::wcout << L"=> Current library list: " << lib_uses << std::endl;
  std::wcout << L"New library list] ";
  std::getline(std::wcin, in);

  if(in.empty()) {
    lib_uses = in;
  }
  else {
    std::wcout << SYNTAX_ERROR << std::endl;
  }
}

void Editor::DoExecute()
{
  ObjeckLang lang(doc.ToString(), lib_uses);
  if(lang.Compile()) {
    std::wcout << lang.Execute();
  }
  else {
    auto errors = lang.GetErrors();
    for(auto& error : errors) {
      std::wcout << error << std::endl;
    }
  }
}

bool Editor::AppendLine(std::wstring line, const int padding)
{
  if(doc.InsertLine(cur_pos - 1, line, padding)) {
    cur_pos++;
    return true;
  }

  return false;
}
