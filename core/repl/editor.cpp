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

 //
 // Document
 //
Document::Document()
{
}

size_t Document::Initialize()
{
  lines.push_back(L"class Shell {");
  lines.push_back(L"  function : Main(args : String[]) ~ Nil {");
  lines.push_back(L"  }");
  lines.push_back(L"}");

  return 3;
}

void Document::List()
{
  std::wcout << L"---" << std::endl;

  auto i = 0;
  for(const auto &line : lines) {
    std::wcout << (++i);
    std::wcout << L": ";
    std::wcout << line << std::endl;
  }
}

bool Document::Insert(size_t line_num, const std::wstring line)
{
  if(line_num < lines.size()) {
    size_t cur_num = 1;

    std::list<std::wstring>::iterator iter = lines.begin();
    while(cur_num++ < line_num) {
      ++iter;
    }

    lines.insert(iter, line);
    return true;
  }

  return false;
}

bool Document::Delete(size_t line_num)
{
  if(line_num < lines.size()) {
    size_t cur_num = 1;

    std::list<std::wstring>::iterator iter = lines.begin();
    while(cur_num++ < line_num) {
      ++iter;
    }

    lines.erase(iter);
    return true;
  }

  return false;
}

//
// Editor
//
Editor::Editor()
{
  cur_pos = doc.Initialize();
}

void Editor::Edit()
{
  bool done = false;
  std::wstring in;
  do {
    std::wcout << L"> ";
    std::getline(std::wcin, in);

    if(in == L"/q") {
      done = true;
    }
    else if(in == L"/l") {
      doc.List();
    }
    else if(StartsWith(in, L"/d")) {
      std::wcout << L"<delete>" << std::endl;
    }
    else {
      Append(in);
    }
  }
  while(!done);
}

void Editor::Append(std::wstring line)
{
  if(doc.Insert(cur_pos, line)) {
    cur_pos++;
  }
}