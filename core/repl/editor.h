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
#include "../module/lang.h"

#define DEFAULT_FILE_NAME L"shell://repl.obs"

class CodeFormatter;

//
// Line
//
class Line {
public:
  enum Type {
    // read-only lines
    RO_LINE,
    RO_CLS_START_LINE,
    RO_CLS_END_LINE,
    RO_FUNC_START_LINE,
    RO_FUNC_END_LINE,
    // read/write line
    RW_LINE,
    RW_CLS_START_LINE,
    RW_CLS_END_LINE,
    RW_FUNC_START_LINE,
    RW_FUNC_END_LINE
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

  const std::wstring ToString() {
    return line;
  }

  const Line::Type GetType() {
    return type;
  }
};

//
// Document
//
class Document {
  std::wstring name;
  std::list<Line> lines;
  size_t shell_count;

 public:
   Document(std::wstring n) {
     name = n;
     shell_count = 0;
   }

   ~Document() {
   }

   size_t Size() {
     return lines.size();
   }

   std::wstring GetName() {
     return name;
   }

   void SetName(const std::wstring &n) {
     name = n;
   }

   bool Save();

   size_t Reset();
   bool LoadFile(const std::wstring &file);
   std::wstring ToString();
   void List(size_t cur_pos, bool all);
   bool InsertLine(size_t line_num, const std::wstring line, Line::Type = Line::Type::RW_LINE);
   bool DeleteLine(size_t line_num);
#ifdef _DEBUG
   void Debug(size_t cur_pos);
#endif
};

//
// Editor
//
class Editor {
  Document doc;
  std::wstring lib_uses;
  size_t cur_pos;

public:
  Editor();

  ~Editor() {
  }

  // start REPL loop
  void Edit();

  // commands
  void DoReset();
  void DoHelp();
  void DoExecute();
  void DoUseLibraries(std::wstring &in);
  void DoInsertLine(std::wstring &in);
  bool DoLoadFile(std::wstring &in);
  bool DoSaveFile(std::wstring& in);
  void DoInsertMultiLine(std::wstring &in);
  bool DoDeleteLine(std::wstring& in);
  bool DoReplaceLine(std::wstring& in);
  void DoGotoLine(std::wstring& in);

  // utility functions
  bool AppendLine(std::wstring line);
  
  static inline void LeftTrim(std::wstring& str) {
    str.erase(str.begin(), std::find_if(str.begin(), str.end(), [] (wchar_t ch) {
      return !std::isspace(ch);     
    }));
  }

  static inline void RightTrim(std::wstring& str) {
    str.erase(std::find_if(str.rbegin(), str.rend(), [] (wchar_t ch) {
      return !std::isspace(ch);
    }).base(), str.end());
  }

  static inline std::wstring& Trim(std::wstring& str) {
    LeftTrim(str); 
    RightTrim(str);
    return str;
  }

  bool EndsWith(const std::wstring& str, std::wstring const& ending) {
    if(str.length() >= ending.length()) {
      return str.compare(str.length() - ending.length(), ending.length(), ending) == 0;
    }

    return false;
  }
};












/*
Code formatter
*/
class CodeFormatter {
  Token* token; Token* prev_token; Token* next_token;
  bool skip_space;

  bool stmt_space;
  long ident_space;

  std::wstring buffer;
  long start_range;
  long end_range;

  bool trim_trailing;

  std::wstring ASCII_RED;
  std::wstring ASCII_BLUE_HIGH;
  std::wstring ASCII_CYAN;
  std::wstring ASCII_GREEN;
  std::wstring ASCII_GREY;
  std::wstring ASCII_END;

  void AppendBuffer(std::wstring str) {
    
      if(!buffer.empty() && (buffer.back() == L' ' && (buffer.at(buffer.size() - 2) == L'}' || buffer.at(buffer.size() - 2) == L';'))) {
        buffer.pop_back();
      }

      buffer += str;
    
  } 

  void AppendBuffer(wchar_t str) {

    if(!buffer.empty() && (buffer.back() == L' ' && (buffer.at(buffer.size() - 2) == L'}' || buffer.at(buffer.size() - 2) == L';'))) {
      buffer.pop_back();
    }

    buffer += str;
  }

  void PopBuffer() {
    buffer.pop_back();
  }

  bool InRange()  {
    return (start_range < 0 && end_range < 0) | (token != nullptr && start_range <= token->GetLineNumber() && end_range >= token->GetLineNumber());
  }
  
  void VerticalSpace(Token* prev_token, long tab_space, long ident_space) {
    if(prev_token->GetType() == TOKEN_OPEN_BRACE) {
      AppendBuffer(L"\n\n");
      TabSpace(tab_space, ident_space);
      skip_space = true;
    }
  }

  void TabSpace(long tab_space, long) {
    if(tab_space > 0) {
      for(long i = 0; i < tab_space; ++i) {
        for(long j = 0; j < ident_space; ++j) {
          AppendBuffer(' ');
        };
        AppendBuffer(' ');
      };
    }
    else {
      for(long i = 0; i < tab_space; ++i) {
        AppendBuffer('\t');
      };
    };
  }

public:
  /*
    Constructor
      param options options :
    <pre class = L"line-numbers" >> options := Map->New() < std::wstring, std::wstring > ;
    options->Insert("function-space", "false");
    options->Insert("ident-space", "3");
    options->Insert("trim-trailing", "true");
    options->Insert("start-line", "1");
    options->Insert("end-line", "2"); < / pre>
      */
  CodeFormatter(std::map<std::wstring, std::wstring>& options) {
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
    };

    // get start and end range
    start_range = -1;
    iter = options.find(L"start-line");
    if(iter != options.end()) {
      start_range = (long)iter->second.size();
    };

    end_range = -1;
    iter = options.find(L"end-line");
    if(iter != options.end()) {
      end_range = (long)iter->second.size();
    };

    buffer = L"";
  }
  ~CodeFormatter() {

  }

  std::wstring Format(std::wstring source, bool is_color) {
    Scanner scanner(L"", false, source);

    token = scanner.GetToken();

    long tab_space = 0;
    bool insert_tabs = false;

    bool skip_space = false;
    bool in_case = false;
    bool in_for = false;
    bool in_consts = false;

    prev_token = next_token = nullptr;

    long i = 0;
    token = scanner.GetToken(0);

    if(insert_tabs) {
      TabSpace(tab_space, ident_space);
      insert_tabs = false;
    };

    bool done = false;
    while(!done) {
      // set current, previous and next tokens
      if(token != nullptr) {
        prev_token = token;
      }
      token = scanner.GetToken(0);
      next_token = scanner.GetToken(1);

      switch(token->GetType()) {
      case TOKEN_CLASS_ID:
        VerticalSpace(prev_token, tab_space, ident_space);

        if(is_color) {
          AppendBuffer(ASCII_GREEN);
        };

        AppendBuffer(L"class");

        if(is_color) {
          AppendBuffer(ASCII_END);
        }
        break;

      case TOKEN_FUNCTION_ID:
        VerticalSpace(prev_token, tab_space, ident_space);

        if(is_color) {
          AppendBuffer(ASCII_GREEN);
        };

        AppendBuffer(L"function");

        if(is_color) {
          AppendBuffer(ASCII_END);
        }
        break;

      case TOKEN_END_OF_STREAM:
        done = -1;
        break;
      }

      // next token
      scanner.NextToken();

      /*
          case  TOKEN_METHOD_ID {
            VerticalSpace(prev_token, tab_space, ident_space);
            if(is_color) {
              AppendBuffer(ASCII_GREEN);
            };

            AppendBuffer(L"method");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_PUBLIC_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"public");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_PRIVATE_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"private");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_IF_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"if");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_ELSE_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer('\n');
            TabSpace(tab_space, ident_space);
            AppendBuffer(L"else");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_DO_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"do");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_WHILE_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"while");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_FOR_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"for");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };

          in_for = true;
          }

          case  TOKEN_EACH_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"each");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_BREAK_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"break");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_CONTINUE_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"continue");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_RETURN_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"return");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_ALIAS_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"alias");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_LEAVING_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"leaving");
          }

          case  TOKEN_USE_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"use");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_NATIVE_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"native");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_STATIC_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"static");
          }

          case  TOKEN_SELECT_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"select");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_LABEL_ID {
            VerticalSpace(prev_token, tab_space, ident_space);

            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"case ");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };

          in_case = true;
            skip_space = false;
          }

          case  TOKEN_OTHER_ID {
            VerticalSpace(prev_token, tab_space, ident_space);
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"other");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };

          in_case = true;
            skip_space = false;
          }

          case  TOKEN_ASGN {
            AppendBuffer(L":=");
          }

          case  TOKEN_ASSESSOR {
            PopBuffer();
            AppendBuffer(L"->");
          }

          case  TOKEN_LINE_COMMENT {
            if(is_color) {
              AppendBuffer(ASCII_GREY);
            };

            AppendBuffer('#');
            AppendBuffer(token->GetValue());

            if(is_color) {
              AppendBuffer(L"\033[0m");
            };

            AppendBuffer('\n');
            skip_space = insert_tabs = true;
          }

          case  TOKEN_MULTI_COMMENT {
            PopBuffer();

            if(is_color) {
              AppendBuffer(ASCII_GREY);
            };

          comment = token->GetValue()->Trim();

            AppendBuffer(L" #~\n");
            each(k : ident_space) {
              AppendBuffer(' ');
            };
            AppendBuffer(' ');

          words = System.Utility.Parser->Tokenize(comment);
            each(word = words) {
              if(word->StartsWith('')) {
                AppendBuffer('\n');
                each(k : ident_space) {
                  AppendBuffer(' ');
                };
                AppendBuffer(' ');
                AppendBuffer(word);
              }
              else {
                AppendBuffer(word);
                if(word->StartsWith(';')) {
                  AppendBuffer('\n');
                  each(k : ident_space) {
                    AppendBuffer(' ');
                  };
                }
                AppendBuffer(' ');
              }
            };

            AppendBuffer('\n');
            each(k : ident_space) {
              AppendBuffer(' ');
            };
            AppendBuffer(L" ~#");

            AppendBuffer('\n');
            each(k : ident_space) {
              AppendBuffer(' ');
            };

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_SEMI {
            PopBuffer();
            AppendBuffer(';');

            if(next_token->GetType() < > TOKEN_CCBRACE & next_token->GetType() < > TOKEN_VTAB & !=in_for) {
              AppendBuffer('\n');

              skip_space = true;
              if(!=in_for) {
              insert_tabs = true;
              };
            };
          }

          case  TOKEN_LESS {
            PopBuffer();
            AppendBuffer('<');
            skip_space = true;
          }

          case  TOKEN_GTR {
            PopBuffer();
            AppendBuffer('>');
          }

          case  TOKEN_COMMA {
            PopBuffer();
            AppendBuffer(',');

            if(in_consts) {
              AppendBuffer('\n');
            insert_tabs = skip_space = true;
            };
          }

          case  TOKEN_NEQL {
            AppendBuffer(L"!=");
          }

          case  TOKEN_AND {
            AppendBuffer('&');
          }

          case  TOKEN_OR {
            AppendBuffer('|');
          }

          case  TOKEN_QUESTION {
            AppendBuffer('?');
          }

          case  TOKEN_IDENT {
            if(prev_token->GetType() = TOKEN_ASSESSOR |
               prev_token->GetType() = TOKEN_ADD_ADD |
               prev_token->GetType() = TOKEN_SUB_SUB) {
              PopBuffer();
            };
          value = token->GetValue();
            AppendBuffer(L"{$value}");
          }

          case  TOKEN_std::wstring_LIT {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            }

          value = token->GetValue();
            AppendBuffer('"');
            AppendBuffer(value);
            AppendBuffer('"');

            if(is_color) {
              AppendBuffer(ASCII_END);
            }
          }

          case  TOKEN_CHAR_LIT {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            }

            AppendBuffer('\'');
            AppendBuffer(token->GetValue());
            AppendBuffer('\'');

            if(is_color) {
              AppendBuffer(ASCII_END);
            }
          }

          case  TOKEN_NUM_LIT {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            }

            AppendBuffer(token->GetValue());

            if(is_color) {
              AppendBuffer(ASCII_END);
            }
          }

          case  TOKEN_std::wstring_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"std::wstring");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_INT_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"size_t");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_FLOAT_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"Float");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_CHAR_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"Char");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_bool_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"bool");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_BYTE_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"Byte");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_NIL_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"nullptr");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_AND_ID {
            AppendBuffer(L"and");
          }

          case  TOKEN_OR_ID {
            AppendBuffer(L"or");
          }

          case  TOKEN_XOR_ID {
            AppendBuffer(L"xor");
          }

          case  TOKEN_VIRTUAL_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"virtual");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_BUNDLE_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"bundle");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_INTERFACE_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"interface");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_IMPLEMENTS_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"implements");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_ENUM_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"enum");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_CONSTS_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

          in_consts = true;
            AppendBuffer(L"consts");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_REVERSE_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"reverse");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_PARENT_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"parent");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_FROM_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"from");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_TRUE_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"true");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_FALSE_ID {
            if(is_color) {
              AppendBuffer(ASCII_CYAN);
            };

            AppendBuffer(L"false");

            if(is_color) {
              AppendBuffer(ASCII_END);
            };
          }

          case  TOKEN_NEW_ID {
            PopBuffer();
            skip_space = true;
            AppendBuffer(L"New");
          }

          case  TOKEN_AS_ID {
            PopBuffer();
            skip_space = true;
            AppendBuffer(L"As");
          }

          case  TOKEN_TYPE_OF_ID {
            PopBuffer();
            skip_space = true;
            AppendBuffer(L"TypeOf");
          }

          case  TOKEN_CRITICAL_ID {
            AppendBuffer(L"critical");
          }

          case  TOKEN_COLON {
            if(in_case ) {
              PopBuffer();
            };
            AppendBuffer(':');
          }

          case  TOKEN_ADD {
            AppendBuffer('+');
          }

          case  TOKEN_SUB {
            AppendBuffer('-');
          }

          case  TOKEN_MUL {
            AppendBuffer('*');
          }

          case  TOKEN_DIV {
            AppendBuffer('/');
          }

          case  TOKEN_MOD {
            AppendBuffer('%');
          }

          case  TOKEN_QUOTE {
            AppendBuffer('\'');
          }

          case  TOKEN_FWD_SLASH {
            AppendBuffer('\\');
          }

          case  TOKEN_EQL {
            AppendBuffer('=');
          }

          case  TOKEN_LESS_EQL {
            AppendBuffer(L"<=");
          }

          case  TOKEN_ESCAPE {
            AppendBuffer(L"\e");
          }


          case  TOKEN_GTR_EQL {
            AppendBuffer(L">=");
          }

          case  TOKEN_ADD_ASN {
            AppendBuffer(L"+=");
          }

          case  TOKEN_SUB_ASN {
            AppendBuffer(L"-=");
          }

          case  TOKEN_ADD_ADD {
            AppendBuffer(L"++");
          }

          case  TOKEN_SUB_SUB {
            AppendBuffer(L"--");
          }

          case  TOKEN_MUL_ASN {
            AppendBuffer(L"*=");
          }

          case  TOKEN_DIV_ASN {
            AppendBuffer(L"/=");
          }

          case  TOKEN_LAMBDA {
            AppendBuffer(L"=>");
          }

          case  TOKEN_TILDE {
            AppendBuffer('~');
          }

          case  TOKEN_OBRACE {
            if(prev_token->GetType() = TOKEN_IDENT) {
              PopBuffer();
            };
            AppendBuffer('[');
            skip_space = true;
          }

          case  TOKEN_CBRACE {
            if(prev_token->GetType() < > TOKEN_OBRACE) {
              PopBuffer();
            };
            AppendBuffer(']');
          }

          case  TOKEN_OPREN {
            if(!=stmt_space) {
              select(prev_token->GetType()) {
                case  TOKEN_IDENT
                  case  TOKEN_IF_ID
                  case  TOKEN_FOR_ID
                  case  TOKEN_EACH_ID
                  case  TOKEN_SELECT_ID
                  case  TOKEN_WHILE_ID{
                    PopBuffer();
                }
              };
            };

            AppendBuffer('(');

            skip_space = true;
          }

          case  TOKEN_CPREN {
            if(prev_token->GetType() < > TOKEN_OPREN) {
              PopBuffer();
            };

            if(in_for) {
            in_for = false;
            };

            AppendBuffer(')');
          }

          case  TOKEN_OCBRACE {
            AppendBuffer(L"{\n");
            skip_space = true;
            tab_space += 1;
          insert_tabs = true;

            if(in_case ) {
            in_case = false;
            };
          }

          case  TOKEN_CCBRACE {
            AppendBuffer('\n');
            tab_space -= 1;
            TabSpace(tab_space, ident_space);
            AppendBuffer('}'); // TODO: Fix me!

              if(in_consts) {
              in_consts = false;
              };
          }

          case  TOKEN_VTAB {
            if(next_token->GetType() < > TOKEN_CCBRACE) {
              AppendBuffer(L"\n\n");
              TabSpace(tab_space, ident_space);
              skip_space = true;
            };
          }

          other {
            "--- OTHER ---"->ErrorLine();
          }
        };

        if(!=skip_space) {
          AppendBuffer(' ');
        }
        else {
          skip_space = false;
        };
      };
    };

    // clean up output
      offset_start = 0;
    each(i : buffer) {
      if(buffer->Get(i) = '\n' | buffer->Get(i) = '\r') {
        offset_start += 1;
      }
      else {
        break;
      };
    };

    offset_end = 0;
      reverse(i : buffer) {
        if(buffer->Get(i) = ' ' | buffer->Get(i) = '\t') {
          offset_end += 1;
        }
        else {
          break;
        };
      };

    formatted = buffer->Substd::wstring(offset_start, buffer->Size() - offset_start - offset_end);

      if(trim_trailing) {
      formatted = formatted->Trim();
      };

      return formatted;
      */
    }

    return buffer;
  }
};

#endif