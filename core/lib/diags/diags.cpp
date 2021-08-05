/***************************************************************************
 * Diagnostics support for Objeck
 *
 * Copyright (c) 2011-2015, Randy Hollines
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
 * - Neither the name of the Objeck Team nor the names of its
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

#include "diags.h"

#include "../../../compiler/parser.h"
#include "../../../compiler/context.h"

using namespace std;
using namespace frontend;

extern "C" {

  //
  // initialize diagnostics environment
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void load_lib()
  {
#ifdef _DEBUG
    OpenLogger("debug.log");
#endif
  }

  //
  // release diagnostics resources
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void unload_lib()
  {
#ifdef _DEBUG
    CloseLogger();
#endif
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void diag_tree_release(VMContext& context)
  {
    ParsedProgram* program = (ParsedProgram*)APITools_GetIntValue(context, 0);
    if(program) {
      delete program;
      program = nullptr;
    }
  }

  //
  // Parse file
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_parse_file(VMContext& context)
  {
    const wstring src_file(APITools_GetStringValue(context, 2));
    const wstring sys_path(APITools_GetStringValue(context, 3));

#ifdef _DEBUG
    wcout << L"Parse file: src_file='" << src_file << L"', sys_path='" << sys_path << L"'" << endl;
#endif

    Parser parser(src_file, false, L"");
    const bool was_parsed = parser.Parse();

    APITools_SetIntValue(context, 0, (size_t)parser.GetProgram());
    APITools_SetIntValue(context, 1, was_parsed ? 1 : 0);
  }
  
  //
  // Parse text
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_parse_text(VMContext& context)
  {
    const wstring src_text(APITools_GetStringValue(context, 2));
    const wstring sys_path(APITools_GetStringValue(context, 3));

#ifdef _DEBUG
    wcout << L"Parse file: text_size=" << src_text.size() << L", sys_path='" << sys_path << L"'" << endl;
#endif

    Parser parser(L"", false, src_text);
    const bool was_parsed = parser.Parse();

    APITools_SetIntValue(context, 0, (size_t)parser.GetProgram());
    APITools_SetIntValue(context, 1, was_parsed ? 1 : 0);
  }

  //
  // get diagnostics (error and warnings)
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_get_diagnostics(VMContext& context)
  {
    size_t* tree_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)tree_obj[0];

    // if parsed
    if(!tree_obj[1]) {
      vector<wstring> error_strings = program->GetErrorStrings();
      size_t* diagnostics_array = FormatErrors(context, error_strings);

      // diagnostics
      size_t* file_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
      file_symb_obj[0] = (size_t)APITools_CreateStringValue(context, L"Diagnostics");
      file_symb_obj[2] = (size_t)diagnostics_array;
      file_symb_obj[1] = file_symb_obj[4] = file_symb_obj[5] = file_symb_obj[6] = file_symb_obj[7] = -1;

      APITools_SetObjectValue(context, 0, file_symb_obj);
    }
    else {
      ContextAnalyzer analyzer(program, L"lang.obl,gen_collect.obl", false, false);
      if(!analyzer.Analyze()) {
        vector<wstring> error_strings = program->GetErrorStrings();
        size_t* diagnostics_array = FormatErrors(context, error_strings);

        // diagnostics
        size_t* file_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
        file_symb_obj[0] = (size_t)APITools_CreateStringValue(context, L"Diagnostics");
        file_symb_obj[2] = (size_t)diagnostics_array;
        file_symb_obj[1] = file_symb_obj[4] = file_symb_obj[5] = file_symb_obj[6] = file_symb_obj[7] = -1;

        APITools_SetObjectValue(context, 0, file_symb_obj);
      }
      else {
        APITools_SetObjectValue(context, 0, 0);
      }
    }
  }
    
  //
  // get symbols
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_get_symbols(VMContext& context)
  {
    size_t* tree_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)tree_obj[0];

    wstring file_name;
    vector<ParsedBundle*> bundles = program->GetBundles();
    size_t* bundle_array = APITools_MakeIntArray(context, (int)bundles.size());
    size_t* bundle_array_ptr = bundle_array + 3;
    for(size_t i = 0; i < bundles.size(); ++i) {
      ParsedBundle* bundle = bundles[i];

      file_name = bundle->GetFileName();

      size_t* bundle_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
      const wstring bundle_name = bundle->GetName();
      bundle_symb_obj[0] = (size_t)APITools_CreateStringValue(context, bundle_name.empty() ? L"Default" : bundle_name);
      bundle_symb_obj[1] = 2; // namespace type
      bundle_symb_obj[4] = bundle->GetLineNumber();
      bundle_symb_obj[5] = bundle->GetLinePosition();
      bundle_symb_obj[6] = bundle->GetEndLineNumber();
      bundle_symb_obj[7] = bundle->GetEndLinePosition();
      bundle_array_ptr[i] = (size_t)bundle_symb_obj;

      vector<Class*> klasss = bundle->GetClasses();
      size_t* klass_array = APITools_MakeIntArray(context, (int)klasss.size());
      size_t* klass_array_ptr = klass_array + 3;
      for(size_t j = 0; j < klasss.size(); ++j) {
        Class* klass = klasss[j];

        size_t* klass_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
        klass_symb_obj[0] = (size_t)APITools_CreateStringValue(context, klass->GetName());
        klass_symb_obj[1] = 5; // class type
        klass_symb_obj[4] = klass->GetLineNumber();
        klass_symb_obj[5] = klass->GetLinePosition();
        klass_symb_obj[6] = klass->GetEndLineNumber();
        klass_symb_obj[7] = klass->GetEndLinePosition();
        klass_array_ptr[j] = (size_t)klass_symb_obj;

        vector<Method*> mthds = klass->GetMethods();
        size_t* mthds_array = APITools_MakeIntArray(context, (int)mthds.size());
        size_t* mthds_array_ptr = mthds_array + 3;
        for(size_t k = 0; k < mthds.size(); ++k) {
          Method* mthd = mthds[k];

          size_t* mthd_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");

          wstring mthd_name = mthd->GetName();
          const size_t mthd_name_index = mthd_name.find_last_of(':');
          if(mthd_name_index != wstring::npos) {
            mthd_name = mthd_name.substr(mthd_name_index + 1, mthd_name.size() - mthd_name_index - 1);
          }
          mthd_symb_obj[0] = (size_t)APITools_CreateStringValue(context, mthd_name);

          mthd_symb_obj[1] = 6; // method type
          mthd_symb_obj[4] = mthd->GetLineNumber();
          mthd_symb_obj[5] = mthd->GetLinePosition();
          mthd_symb_obj[6] = mthd->GetEndLineNumber();
          mthd_symb_obj[7] = mthd->GetEndLinePosition();
          mthds_array_ptr[k] = (size_t)mthd_symb_obj;
        }
        klass_symb_obj[2] = (size_t)mthds_array;
      }
      bundle_symb_obj[2] = (size_t)klass_array;
    }

    // file root
    size_t* file_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
    file_symb_obj[0] = (size_t)APITools_CreateStringValue(context, file_name);
    file_symb_obj[1] = 1; // file type
    file_symb_obj[2] = (size_t)bundle_array;
    file_symb_obj[4] = file_symb_obj[5] = file_symb_obj[6] = file_symb_obj[7] = -1;

    APITools_SetObjectValue(context, 0, file_symb_obj);
  }

  //
  // get references
  //
#ifdef _WIN32
    __declspec(dllexport)
#endif
    void diag_find_references(VMContext& context)
  {
    size_t* tree_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)tree_obj[0];

    const int line_num = (int)APITools_GetIntValue(context, 2);
    const int line_pos = (int)APITools_GetIntValue(context, 3);

    Method* method = FindMethod(line_num, program);
    if(method) {
      Expression* expression = SearchMethod(line_num, line_pos, method);
      if(expression) {

      }
    }
  }

  //
  // Supporting functions
  //
  Expression* SearchMethod(const int line_num, const int line_pos, Method* method)
  {
    if(method) {
      vector<Statement*> statements = method->GetStatements()->GetStatements();
      for(auto& statement : statements) {
        const int start_line = statement->GetLineNumber();
        const int end_line = statement->GetEndLineNumber();

        if(start_line <= line_num && end_line >= line_num) {
          switch(statement->GetStatementType()) {
          case EMPTY_STMT:
          case SYSTEM_STMT:
            break;

          case DECLARATION_STMT:
            break;

          case METHOD_CALL_STMT:
            break;


          case ADD_ASSIGN_STMT:
            break;

          case SUB_ASSIGN_STMT:
          case MUL_ASSIGN_STMT:
          case DIV_ASSIGN_STMT:
            break;

          case ASSIGN_STMT:
            return SearchAssignment(line_num, line_pos, static_cast<Assignment*>(statement));

          case SIMPLE_STMT:
            break;

          case RETURN_STMT:
            break;

          case LEAVING_STMT:
            break;

          case IF_STMT:
            break;

          case DO_WHILE_STMT:
            break;

          case WHILE_STMT:
            return SearchWhile(line_num, line_pos, static_cast<While*>(statement));

          case FOR_STMT:
            break;

          case BREAK_STMT:
          case CONTINUE_STMT:
            break;

          case SELECT_STMT:
            break;

          case CRITICAL_STMT:
            break;

          default:
#ifdef _DEBUG
            wcerr << L"Undefined statement" << endl;
#endif

            break;
          }
        }
      }
    }

    return nullptr;
  }

  Expression* SearchAssignment(const int line_num, const int line_pos, Assignment* asgn_stmt)
  {
    return nullptr;
  }

  Expression* SearchWhile(const int line_num, const int line_pos, While* while_stmt)
  {
    return nullptr;
  }

  Method* FindMethod(const int line_num, ParsedProgram* program)
  {
    // bundles
    vector<ParsedBundle*> bundles = program->GetBundles();
    for(auto& bundle : bundles) {
      // classes
      vector<Class*> klasses = bundle->GetClasses();
      for(auto& klass : klasses) {
        // methods
        vector<Method*> methods = klass->GetMethods();
        for(auto& method : methods) {
          const int start_line = method->GetLineNumber();
          const int end_line = method->GetEndLineNumber();

          if(start_line <= line_num && end_line > line_num) {
#ifdef _DEBUG
            wcout << L"Method: '" << method->GetParsedName() << "'" << endl;
#endif
            return method;
          }
        }

        // enums
        vector<Enum*> eenums = bundle->GetEnums();
        for(auto& eenum : eenums) {

        }
      }
    }

    return nullptr;
  }

  size_t* FormatErrors(VMContext& context, vector<wstring> error_strings)
  {
    size_t* diagnostics_array = APITools_MakeIntArray(context, (int)error_strings.size());
    size_t* diagnostics_array_ptr = diagnostics_array + 3;

    for(size_t i = 0; i < error_strings.size(); ++i) {
      const wstring error_string = error_strings[i];

      // parse error string
      const size_t file_mid = error_string.find(L":(");
      const wstring file_str = error_string.substr(0, file_mid);

      const size_t msg_mid = error_string.find(L"):");
      const wstring msg_str = error_string.substr(msg_mid + 3, error_string.size() - msg_mid - 3);

      const wstring line_pos_str = error_string.substr(file_mid + 2, msg_mid - file_mid - 2);
      const size_t line_pos_mid = line_pos_str.find(L',');
      const wstring line_str = line_pos_str.substr(0, line_pos_mid);
      const wstring pos_str = line_pos_str.substr(line_pos_mid + 1, line_pos_str.size() - line_pos_mid - 1);

      // create objects
      size_t* diag_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
      diag_obj[0] = (size_t)APITools_CreateStringValue(context, msg_str);
      diag_obj[1] = 1; // error type
      diag_obj[3] = (size_t)APITools_CreateStringValue(context, file_str);
      diag_obj[4] = _wtoi(line_str.c_str());
      diag_obj[5] = _wtoi(pos_str.c_str());
      diag_obj[6] = diag_obj[7] = -1;
      diagnostics_array_ptr[i] = (size_t)diag_obj;
    }

    return diagnostics_array;
  }
}