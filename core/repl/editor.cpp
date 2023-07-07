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

  if(!all && lines.size() == shell_count) {
    std::wcout << L"[No code]" << std::endl;
  }
  else {
    if(all) {
      std::wcout << L"[All code]" << std::endl;
    }

    std::wstringstream buffer;
    for(auto& line : lines) {
      std::wstring string = line.ToString();
      buffer << string;
    }

    std::map<std::wstring, std::wstring> options;
    CodeFormatter formatter(options);
    std::wcout << formatter.Format(buffer.str(), false) << std::endl;
  }
}

bool Document::InsertLine(size_t line_num, const std::wstring line, Line::Type type)
{
  if(line_num > 0 && line_num < lines.size()) {
    size_t cur_num = 0;

    std::list<Line>::iterator iter = lines.begin();
    while(cur_num++ < line_num) {
      ++iter;
    }

    lines.insert(iter, Line(line, type));
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
Editor::Editor() : doc(DEFAULT_FILE_NAME)
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

        // load file
      case L'f':
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
          std::wcout << L"Unable to save file => '" << doc.GetName() << L"'.\n  If the file was not loaded, provide a filename." << std::endl;
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
  std::wcout << "  /f: load file by name" << std::endl;
  std::wcout << "  /s: save file loaded or current buffer" << std::endl;
  std::wcout << "  /r: replace line" << std::endl;
  std::wcout << "  /d: delete line" << std::endl;
  std::wcout << "  /u: edit library use statements" << std::endl;
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
    else {
      if(AppendLine(in)) {
        line_count++;
      }
    }
  } while(!multi_line_done);

  std::wcout << L"=> Inserted " << line_count << " lines." << std::endl;
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
    doc.SetName(in.substr(3));
    if(doc.Save()) {
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
      if(line_pos - 1 < doc.Size()) {
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
      if(doc.DeleteLine(line_pos - 1)) {
        cur_pos = line_pos;
        std::wcout << L"Insert] " << line_pos << L"] ";
        std::getline(std::wcin, in);
        if(AppendLine(in)) {
          std::wcout << "=> Replaced line " << line_pos << L'.' << std::endl;
          return true;
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
  std::wcout << L"=> Current library list: " << lib_uses << std::endl;
  std::wcout << L"New library list] ";
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
  ObjeckLang lang(doc.ToString(), lib_uses);
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
  if(doc.InsertLine(cur_pos - 1, line)) {
    cur_pos++;
    return true;
  }

  return false;
}

/*
CodeFormatter
*/

/*
    Param options options :
    <pre class = L"line-numbers" >> options := Map->New() < std::wstring, std::wstring > ;
    options->Insert("function-space", "false");
    options->Insert("ident-space", "3");
    options->Insert("trim-trailing", "true");
    options->Insert("start-line", "1");
    options->Insert("end-line", "2"); < / pre>
  */
CodeFormatter::CodeFormatter(std::map<std::wstring, std::wstring>& options) 
{
  ASCII_CYAN = L"\x1B[36m";
  ASCII_BLUE_HIGH = L"\x1B[42m";
  ASCII_RED = L"\x1B[36m";
  ASCII_GREEN = L"\x1B[32m";
  ASCII_GREY = L"\x1B[90m";
  ASCII_END = L"\033[0m";

  // get options
  std::map<std::wstring, std::wstring>::iterator iter = options.find(L"function-space");
  stmt_space = iter != options.end() && iter->second == L"true";

  // trim trailing
  iter = options.find(L"trim-trailing");
  stmt_space = iter != options.end() && iter->second == L"true";

  ident_space = 0;
  iter = options.find(L"ident-space");
  if(iter != options.end()) {
    ident_space = (long)iter->second.size() - 1;
  }

  // get start and end range
  start_range = -1;
  iter = options.find(L"start-line");
  if(iter != options.end()) {
    start_range = (long)iter->second.size();
  }

  end_range = -1;
  iter = options.find(L"end-line");
  if(iter != options.end()) {
    end_range = (long)iter->second.size();
  }

  buffer = L"";
}
CodeFormatter::~CodeFormatter() 
{

}

std::wstring CodeFormatter::Format(std::wstring source, bool is_color) 
{
  long tab_space = 0;
  bool insert_tabs = false;

  bool skip_space = false;
  bool in_case = false;
  bool in_for = false;
  bool in_consts = false;

  token = prev_token = next_token = nullptr;

  Scanner scanner(L"", false, source);
  if(insert_tabs) {
    TabSpace(tab_space, ident_space);
    insert_tabs = false;
  }

  bool done = false;
  while(!done) {
    // set tokens
    if(token != nullptr) {
      prev_token = token;
    }
    token = scanner.GetToken(0);
    next_token = scanner.GetToken(1);

    // append tokens
    switch(token->GetType()) {
    case TOKEN_CLASS_ID:
      VerticalSpace(prev_token, tab_space, ident_space);

      if(is_color) {
        AppendBuffer(ASCII_GREEN);
      }

      AppendBuffer(L"class");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_FUNCTION_ID:
      VerticalSpace(prev_token, tab_space, ident_space);

      if(is_color) {
        AppendBuffer(ASCII_GREEN);
      }

      AppendBuffer(L"function");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_METHOD_ID:
      VerticalSpace(prev_token, tab_space, ident_space);
      if(is_color) {
        AppendBuffer(ASCII_GREEN);
      }

      AppendBuffer(L"method");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_PUBLIC_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"public");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_PRIVATE_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"private");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_END_OF_STREAM:
      done = true;
      break;

    case TOKEN_IF_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"if");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_ELSE_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer('\n');
      TabSpace(tab_space, ident_space);
      AppendBuffer(L"else");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;


    case TOKEN_DO_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"do");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_WHILE_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"while");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_FOR_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"for");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }

      in_for = true;
      break;

    case TOKEN_EACH_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"each");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_BREAK_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"break");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_CONTINUE_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"continue");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_RETURN_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"return");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_ALIAS_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"alias");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_LEAVING_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"leaving");
      break;

    case TOKEN_USE_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"use");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_NATIVE_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"native");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_STATIC_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"static");
      break;

    case TOKEN_SELECT_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"select");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_LABEL_ID:
      VerticalSpace(prev_token, tab_space, ident_space);

      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"case ");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }

      in_case = true;
      skip_space = false;
      break;

    case TOKEN_OTHER_ID:
      VerticalSpace(prev_token, tab_space, ident_space);
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"other");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }

      in_case = true;
      skip_space = false;
      break;

    case TOKEN_ASSIGN:
      AppendBuffer(L":=");
      break;

    case TOKEN_ASSESSOR:
      PopBuffer();
      AppendBuffer(L"->");
      break;

      /*
      case TOKEN_LINE_COMMENT {
        if(is_color) {
          AppendBuffer(ASCII_GREY);
        }

        AppendBuffer('#');
        AppendBuffer(token->GetValue());

        if(is_color) {
          AppendBuffer(L"\033[0m");
        }

        AppendBuffer('\n');
        skip_space = insert_tabs = true;
      }

      case TOKEN_MULTI_COMMENT {
        PopBuffer();

        if(is_color) {
          AppendBuffer(ASCII_GREY);
        }

        comment = token->GetValue()->Trim();

        AppendBuffer(L" #~\n");
        each(k : ident_space) {
          AppendBuffer(' ');
        }
        AppendBuffer(' ');

        words = System.Utility.Parser->Tokenize(comment);
        each(word = words) {
          if(word->StartsWith('')) {
            AppendBuffer('\n');
            each(k : ident_space) {
              AppendBuffer(' ');
            }
            AppendBuffer(' ');
            AppendBuffer(word);
          }
          else {
            AppendBuffer(word);
            if(word->StartsWith(';')) {
              AppendBuffer('\n');
              each(k : ident_space) {
                AppendBuffer(' ');
              }
            }
            AppendBuffer(' ');
          }
        }

        AppendBuffer('\n');
        each(k : ident_space) {
          AppendBuffer(' ');
        }
        AppendBuffer(L" ~#");

        AppendBuffer('\n');
        each(k : ident_space) {
          AppendBuffer(' ');
        }

        if(is_color) {
          AppendBuffer(ASCII_END);
        }
      }

      case TOKEN_SEMI_COLON:
        PopBuffer();
        AppendBuffer(';');

        if(next_token->GetType() != TOKEN_CCBRACE && next_token->GetType() != TOKEN_VTAB && !in_for) {
          AppendBuffer('\n');

          skip_space = true;
          if(!= in_for) {
            insert_tabs = true;
          }
        }
        break;
      */

    case TOKEN_LES:
      PopBuffer();
      AppendBuffer('<');
      skip_space = true;
      break;

    case TOKEN_GTR:
      PopBuffer();
      AppendBuffer('>');
      break;

    case TOKEN_COMMA:
      PopBuffer();
      AppendBuffer(',');

      if(in_consts) {
        AppendBuffer('\n');
        insert_tabs = skip_space = true;
      }
      break;

    case TOKEN_NEQL:
      AppendBuffer(L"!=");
      break;

    case TOKEN_AND:
      AppendBuffer('&');
      break;

    case TOKEN_OR:
      AppendBuffer('|');
      break;

    case TOKEN_QUESTION:
      AppendBuffer('?');
      break;

    case TOKEN_IDENT: {
      if(prev_token && (prev_token->GetType() == TOKEN_ASSESSOR || prev_token->GetType() == TOKEN_ADD_ADD || prev_token->GetType() == TOKEN_SUB_SUB)) {
        PopBuffer();
      }
      const std::wstring value = token->GetIdentifier();
      AppendBuffer(value);
    }
      break;

    case TOKEN_CHAR_STRING_LIT: {
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      const std::wstring value = token->GetIdentifier();
      AppendBuffer('"');
      AppendBuffer(value);
      AppendBuffer('"');

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
    }
      break;

    case TOKEN_CHAR_LIT:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer('\'');
      AppendBuffer(token->GetIdentifier());
      AppendBuffer('\'');

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

      /*
    case TOKEN_NUM_LIT:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(token->GetIdentifier());

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;
      */

    case TOKEN_INT_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"size_t");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_FLOAT_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"Float");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_CHAR_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"Char");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_BOOLEAN_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"bool");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_BYTE_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"Byte");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_NIL_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"Nil");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_AND_ID:
      AppendBuffer(L"and");
      break;

    case TOKEN_OR_ID:
      AppendBuffer(L"or");
      break;

    case TOKEN_XOR_ID:
      AppendBuffer(L"xor");
      break;

    case TOKEN_VIRTUAL_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"virtual");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_BUNDLE_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"bundle");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_INTERFACE_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"interface");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_IMPLEMENTS_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"implements");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_ENUM_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"enum");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_CONSTS_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      in_consts = true;
      AppendBuffer(L"consts");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_REVERSE_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"reverse");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_PARENT_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"parent");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_FROM_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"from");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_TRUE_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"true");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_FALSE_ID:
      if(is_color) {
        AppendBuffer(ASCII_CYAN);
      }

      AppendBuffer(L"false");

      if(is_color) {
        AppendBuffer(ASCII_END);
      }
      break;

    case TOKEN_NEW_ID:
      PopBuffer();
      skip_space = true;
      AppendBuffer(L"New");
      break;

    case TOKEN_AS_ID:
      PopBuffer();
      skip_space = true;
      AppendBuffer(L"As");
      break;

    case TOKEN_TYPE_OF_ID:
      PopBuffer();
      skip_space = true;
      AppendBuffer(L"TypeOf");
      break;

    case TOKEN_CRITICAL_ID:
      AppendBuffer(L"critical");
      break;

    case TOKEN_COLON:
      if(in_case) {
        PopBuffer();
      }
      AppendBuffer(':');
      break;

    case TOKEN_ADD:
      AppendBuffer('+');
      break;

    case TOKEN_SUB:
      AppendBuffer('-');
      break;

    case TOKEN_MUL:
      AppendBuffer('*');
      break;

    case TOKEN_DIV:
      AppendBuffer('/');
      break;

    case TOKEN_MOD:
      AppendBuffer('%');
      break;

    case TOKEN_EQL:
      AppendBuffer('=');
      break;

    case TOKEN_LEQL:
      AppendBuffer(L"<=");
      break;

    case TOKEN_GEQL:
      AppendBuffer(L">=");
      break;

    case TOKEN_ADD_ASSIGN:
      AppendBuffer(L"+=");
      break;

    case TOKEN_SUB_ASSIGN:
      AppendBuffer(L"-=");
      break;

    case TOKEN_ADD_ADD:
      AppendBuffer(L"++");
      break;

    case TOKEN_SUB_SUB:
      AppendBuffer(L"--");
      break;

    case TOKEN_MUL_ASSIGN:
      AppendBuffer(L"*=");
      break;

    case TOKEN_DIV_ASSIGN:
      AppendBuffer(L"/=");
      break;

    case TOKEN_LAMBDA:
      AppendBuffer(L"=>");
      break;

    case TOKEN_TILDE:
      AppendBuffer('~');
      break;

    case TOKEN_OPEN_BRACKET:
      if(prev_token && prev_token->GetType() == TOKEN_IDENT) {
        PopBuffer();
      }
      AppendBuffer('[');
      skip_space = true;
      break;

    case TOKEN_CLOSED_BRACKET:
      if(prev_token && prev_token->GetType() != TOKEN_OPEN_BRACKET) {
        PopBuffer();
      }
      AppendBuffer(']');
      break;

    case TOKEN_OPEN_PAREN:
      if(prev_token && !stmt_space) {
        switch(prev_token->GetType()) {
        case TOKEN_IDENT:
        case TOKEN_IF_ID:
        case TOKEN_FOR_ID:
        case TOKEN_EACH_ID:
        case TOKEN_SELECT_ID:
        case TOKEN_WHILE_ID:
          PopBuffer();
          break;

        default:
          break;
        }
      }

      AppendBuffer('(');

      skip_space = true;
      break;

    case TOKEN_CLOSED_PAREN:
      if(prev_token && prev_token->GetType() != TOKEN_OPEN_PAREN) {
        PopBuffer();
      }

      if(in_for) {
        in_for = false;
      }

      AppendBuffer(')');
      break;

    case TOKEN_OPEN_BRACE:
      AppendBuffer(L"{\n");
      skip_space = true;
      tab_space += 1;
      insert_tabs = true;

      if(in_case) {
        in_case = false;
      }
      break;

    case TOKEN_CLOSED_BRACE:
      AppendBuffer('\n');
      tab_space -= 1;
      TabSpace(tab_space, ident_space);
      AppendBuffer('}');

      if(in_consts) {
        in_consts = false;
      }
      break;

      /*
      case TOKEN_VTAB {
        if(next_token->GetType() != TOKEN_CCBRACE) {
          AppendBuffer(L"\n\n");
          TabSpace(tab_space, ident_space);
          skip_space = true;
        }
      }
      */

    default:
      std::wcerr << L"--- OTHER ---" << std::endl;
      break;
    }

    if(!skip_space) {
      AppendBuffer(' ');
    }
    else {
      skip_space = false;
    }

    /*
    // clean up output
    long offset_start = 0;
    each(i : buffer) {
    if(buffer->Get(i) = '\n' | buffer->Get(i) = '\r') {
      offset_start += 1;
    }
    else {
      break;
    }
  }

    offset_end = 0;
    reverse(i : buffer) {
      if(buffer->Get(i) = ' ' | buffer->Get(i) = '\t') {
        offset_end += 1;
      }
      else {
        break;
      }
    }


    // formatted = buffer->Substd::wstring(offset_start, buffer->Size() - offset_start - offset_end);

    if(trim_trailing) {
      formatted = formatted->Trim();
    }

    return formatted;


      }
    */

    // next token
    scanner.NextToken();
  }

  return buffer;
}
