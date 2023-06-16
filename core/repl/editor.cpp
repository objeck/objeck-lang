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
    else {
      std::wcout << L"[Lines of code]" << std::endl;
    }

    auto index = 0;
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

bool Document::InsertLine(size_t line_num, const std::wstring line, int padding)
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

    lines.insert(iter, Line(padding_str + line, Line::Type::RW_LINE));
    return true;
  }

  return false;
}

size_t Document::InsertFunction(const std::wstring text)
{
  size_t index = 0;
  for(auto& line : lines) {
    if(line.GetType() == Line::Type::RO_FUNC_END_LINE) {
      InsertLine(index + 1, text, 2);
      return index;
    }
    ++index;
  }

  return std::wstring::npos;
}

size_t Document::DeleteFunction(const std::wstring name)
{
  return std::wstring::npos;
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
  std::wcout << L"Objeck REPL (" << VERSION_STRING << L")" << std::endl << L"---" << std::endl;

  std::wstring in; bool done = false;
  do {
    std::wcout << L"> ";
    std::getline(std::wcin, in);

    // quit
    if(in == L"q" || in == L"quit") {
      done = true;
    }
    // help
    else if(in == L"h" || in == L"help") {
      DoHelp();
    }
    else {
      switch(in.front()) {
        // list
      case L'l':
        doc.List(cur_pos, false);
        break;

        // list all
      case L'a':
        doc.List(cur_pos, true);
        break;

        // insert line
      case L'i':
        DoInsertLine(in);
        break;

        // insert multiple lines
      case L'm':
        DoInsertMultiLine(in);
        break;

        // delete line
      case L'd':
        DoDeleteLine(in);
        break;

        // insert function/method
      case L'f':
        DoInsertFunction(in);
        break;

        // goto line
      case L'g':
        DoGotoLine(in);
        break;

        // replace line
      case L'r':
        DoReplaceLine(in);
        break;

        // execute
      case L'u':
        DoUseLibraries(in);
        break;

        // execute
      case L'x':
        DoExecute();
        break;

        // reset
      case L'o':
        DoReset();
        break;

        // other
      default:
        std::wcout << SYNTAX_ERROR << std::endl;
        break;
      }
    }
  }
  while(!done);

  std::wcout << "Goodbye." << std::endl;
}

bool Editor::AppendFunction(std::wstring line)
{
  Trim(line);

  // validate input
  const bool found_func = line.find(L"function") != std::wstring::npos;
  const bool found_method = line.find(L"method") != std::wstring::npos;

  size_t open_paren_pos = std::wstring::npos;
  size_t closed_paren_pos = std::wstring::npos;
  size_t tilde_pos = std::wstring::npos;

  size_t char_pos = 0;
  for(std::wstring::reverse_iterator iter = line.rbegin(); iter != line.rend(); ++iter, ++char_pos) {
    const auto line_char = *iter;
    switch(*iter) {
    case L'(':
      open_paren_pos = char_pos;
      break;

    case L')':
      if(closed_paren_pos == std::wstring::npos) {
        closed_paren_pos = char_pos;
      }
      break;

    case L'~':
      tilde_pos = char_pos;
      break;
    }
  }

  // '(' and ')' are scanned backwards
  if((found_func || found_method) && open_paren_pos > closed_paren_pos && tilde_pos !=  std::wstring::npos) {
    if(line.back() != L'{') {
      line += L" {";
    }

    cur_pos = doc.InsertFunction(line);
    cur_pos += 2;
    doc.InsertLine(cur_pos, L"}", 2);
    cur_pos++;
    return true;
  }

  return false;
}

void Editor::DoHelp()
{
  std::wcout << "=> Commands" << std::endl;
  std::wcout << "  q: quit" << std::endl;
  std::wcout << "  h: help" << std::endl;
  std::wcout << "  l: lists lines" << std::endl;
  std::wcout << "  a: lists full program" << std::endl;
  std::wcout << "  o: reset" << std::endl;
  std::wcout << "  g: goto line" << std::endl;
  std::wcout << "  i: insert line" << std::endl;
  std::wcout << "  m: insert multiple lines" << std::endl;
  std::wcout << "  f: insert function or method" << std::endl;
  std::wcout << "  r: replace line" << std::endl;
  std::wcout << "  d: delete line" << std::endl;
  std::wcout << "  u: edit library use statements" << std::endl;
  std::wcout << "  x: execute program" << std::endl;
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
  std::wcout << L"Insert] ";
  std::getline(std::wcin, in);
  if(AppendLine(in, 4)) {
    std::wcout << "=> Inserted line." << std::endl;
  }
}

void Editor::DoInsertMultiLine(std::wstring in)
{
  size_t line_count = 0;
  bool multi_line_done = false;
  do {
    std::wcout << L"Insert '/m' to exit] ";
    std::getline(std::wcin, in);
    if(in == L"m") {
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
  std::wcout << L"Goto line? ";
  std::getline(std::wcin, in);

  const size_t line_pos = std::stoi(in);
  if(line_pos - 1 < doc.Size()) {
    cur_pos = line_pos;
    std::wcout << "=> Cursor at line " << in << L'.' << std::endl;
  }
  else {
    std::wcout << SYNTAX_ERROR << std::endl;
  }
}

void Editor::DoReplaceLine(std::wstring& in)
{
  std::wcout << L"Replace line? ";
  std::getline(std::wcin, in);

  const size_t line_pos = std::stoi(in);
  if(doc.DeleteLine(line_pos - 1)) {
    cur_pos = line_pos;
    std::wcout << L"Insert] " << line_pos << L"] ";
    std::getline(std::wcin, in);
    if(AppendLine(in, 4)) {
      std::wcout << "=> Replaced line " << line_pos << L'.' << std::endl;
    }
  }
  else {
    std::wcout << "=> Line " << in << L" is read-only." << std::endl;
  }
}

void Editor::DoDeleteLine(std::wstring& in)
{
  std::wcout << L"Delete line? ";
  std::getline(std::wcin, in);

  const int line_pos = std::stoi(in);
  if(doc.DeleteLine(line_pos - 1)) {
    std::wcout << L"=> Removed line " << line_pos << L'.' << std::endl;
  }
  else {
    std::wcout << "=> Line " << in << L" is read-only." << std::endl;
  }
}

void Editor::DoInsertFunction(std::wstring in)
{
  std::wcout << L"Signature] ";
  std::getline(std::wcin, in);

  if(AppendFunction(in)) {
    std::wcout << L"=> Added function/method." << std::endl;
  }
  else {
    std::wcout << L"=> Unable to added function/method. Please check the signature." << std::endl;
  }
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
    std::wcout << lang.Execute() << std::endl;
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
