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
 * - Redistributions in binary form must reproduceC the above copyright
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

#ifndef __EDITOR_H__
#define __EDITOR_H__

#include "repl.h"

//
// Line
//
class Line {
public:
  enum Type {
    READ_ONLY,
    READ_WRITE
  };

private:
  std::wstring line;
  Line::Type type;

public:
  Line(const Line &l)
  {
    line = l.line;
    type = l.type;
  }

  Line(const std::wstring &l, Line::Type t)
  {
    line = l;
    type = t;
  }

  ~Line() {
  }

  const std::wstring ToString();
  const Line::Type GetType();
};

//
// Document
//
class Document {
  std::list<Line> lines;

 public:
   Document();
  
   ~Document() {
   }

   size_t Reset();
   inline size_t Lines();
   std::wstring ToString();

   void List(size_t cur_pos, bool all);
   bool Insert(size_t line_num, const std::wstring line);
   bool Delete(size_t line_num);
};

//
// Editor
//
class Editor {
  Document doc;
  size_t cur_pos;

public:
  Editor();

  ~Editor() {
  }

  void Edit();

  bool Append(std::wstring line);
  char* Compile();
  void Execute(char* code);
};

#endif