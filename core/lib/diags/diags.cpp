/***************************************************************************
 * Diagnostics support for Objeck
 *
 * Copyright (c) 2021, Randy Hollines
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

#include "../../compiler/parser.h"
#include "../../compiler/context.h"

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

    vector<pair<wstring, wstring> > programs;

    Parser parser(src_file, false, programs);
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
    size_t* names_array = APITools_GetArray(APITools_GetObjectValue(context, 2));
    const long names_array_size = APITools_GetArraySize(names_array);

    size_t* texts_array = APITools_GetArray(APITools_GetObjectValue(context, 3));
    const long texts_array_size = APITools_GetArraySize(texts_array);

    if(names_array_size > 0 && names_array_size == texts_array_size) {
      vector<pair<wstring, wstring>> texts;
      for(long i = 0; i < texts_array_size; ++i) {
        const wchar_t* file_name = APITools_GetStringValue(names_array, i);
        const wchar_t* file_text = APITools_GetStringValue(texts_array, i);

        if(file_name && file_text) {
          texts.push_back(make_pair(file_name, file_text));
        }
      }

      Parser parser(L"", false, texts);
      const bool was_parsed = parser.Parse();

      APITools_SetIntValue(context, 0, (size_t)parser.GetProgram());
      APITools_SetIntValue(context, 1, was_parsed ? 1 : 0);
    }
  }

  //
  // get diagnostics (error and warnings)
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void diag_get_diagnosis(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 0);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const wstring uri = APITools_GetStringValue(context, 1);
    const wstring lib_path = APITools_GetStringValue(context, 2);

    wstring full_lib_path = L"lang.obl";
    if(!lib_path.empty()) {
      full_lib_path += L',' + lib_path;
    }

    // if parsed
    if(prgm_obj[1]) {
      ContextAnalyzer analyzer(program, full_lib_path, false, false);
      const bool analyze_success = analyzer.Analyze();
      const vector<wstring> warning_strings = program->GetWarningStrings();
      if(!analyze_success || !warning_strings.empty()) {
        vector<wstring> error_strings = program->GetErrorStrings();
        size_t* diagnostics_array = FormatErrors(context, error_strings, warning_strings);
        prgm_obj[3] = (size_t)diagnostics_array;
      }
    }
    else {
      vector<wstring> error_strings = program->GetErrorStrings();
      size_t* diagnostics_array = FormatErrors(context, error_strings);
      prgm_obj[3] = (size_t)diagnostics_array;
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
    size_t* prgm_obj = APITools_GetObjectValue(context, 0);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const wstring uri = APITools_GetStringValue(context, 1);

    const wstring lib_path = APITools_GetStringValue(context, 2);
    wstring full_lib_path = L"lang.obl";
    if(!lib_path.empty()) {
      full_lib_path += L',' + lib_path;
    }

    // if parsed
    bool validated = false;
    if(prgm_obj[1]) {
      ContextAnalyzer analyzer(program, full_lib_path, false, false);
      validated = analyzer.Analyze();
    }

    // list of bundles for classes
    size_t* bundle_array = nullptr;
    size_t* klass_array = nullptr;

    // bundles
    wstring file_uri;
    const vector<ParsedBundle*> bundles = program->GetBundles();

    for(size_t i = 0; i < bundles.size(); ++i) {
      ParsedBundle* bundle = bundles[i];

      if(file_uri.empty()) {
        file_uri = bundle->GetFileName();
      }

      // bundle
      size_t* bundle_symb_obj = nullptr;
      const wstring bundle_name = bundle->GetName();
      if(!bundle_name.empty()) {
        bundle_array = APITools_MakeIntArray(context, (int)bundles.size());
        size_t* bundle_array_ptr = bundle_array + 3;

        bundle_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
        bundle_symb_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, bundle_name);
        bundle_symb_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_NAMESPACE; // namespace type
        bundle_symb_obj[ResultPosition::POS_START_LINE] = bundle->GetLineNumber();
        bundle_symb_obj[ResultPosition::POS_START_POS] = bundle->GetLinePosition();
        bundle_symb_obj[ResultPosition::POS_END_LINE] = bundle->GetEndLineNumber();
        bundle_symb_obj[ResultPosition::POS_END_POS] = bundle->GetEndLinePosition();

        bundle_array_ptr[i] = (size_t)bundle_symb_obj;
      }

      // get classes and enums
      const vector<Class*> klasses = bundle->GetClasses();
      const vector<Enum*> eenums = bundle->GetEnums();

      // classes
      klass_array = APITools_MakeIntArray(context, (int)(klasses.size() + eenums.size()));
      size_t* klass_array_ptr = klass_array + 3;

      size_t index = 0;
      for(size_t j = 0; j < klasses.size(); ++j) {
        Class* klass = klasses[j];

        size_t* klass_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
        klass_symb_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, klass->GetName());
        klass_symb_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringValue(context, klass->GetFileName());
        klass_symb_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_CLASS; // class type
        klass_symb_obj[ResultPosition::POS_START_LINE] = klass->GetLineNumber() - 1;
        klass_symb_obj[ResultPosition::POS_START_POS] = klass->GetLinePosition() - 1;
        klass_symb_obj[ResultPosition::POS_END_LINE] = klass->GetEndLineNumber() - 1;
        klass_symb_obj[ResultPosition::POS_END_POS] = klass->GetEndLinePosition() - 1;
        klass_array_ptr[index++] = (size_t)klass_symb_obj;

        // methods
        vector<Method*> mthds = klass->GetMethods();
        size_t* mthds_array = APITools_MakeIntArray(context, (int)mthds.size());
        size_t* mthds_array_ptr = mthds_array + 3;
        for(size_t k = 0; k < mthds.size(); ++k) {
          Method* mthd = mthds[k];

          size_t* mthd_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");

          wstring mthd_name = mthd->GetName();
          const size_t mthd_name_index = mthd_name.find_last_of(L':');
          if(mthd_name_index != wstring::npos) {
            mthd_name = mthd_name.substr(mthd_name_index + 1, mthd_name.size() - mthd_name_index - 1);
          }
          mthd_symb_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, mthd_name);
          mthd_symb_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringValue(context, mthd->GetFileName());

          if(mthd->IsStatic()) {
            mthd_symb_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_FUNC; // function
          }
          else {
            switch(mthd->GetMethodType()) {
            case NEW_PUBLIC_METHOD:
            case NEW_PRIVATE_METHOD:
              mthd_symb_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_CONSTR; // constructor
              break;

            default:
              mthd_symb_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_METHOD; // method
              break;
            }
          }

          mthd_symb_obj[ResultPosition::POS_START_LINE] = (size_t)mthd->GetLineNumber() - 1;
          mthd_symb_obj[ResultPosition::POS_START_POS] = mthd->GetLinePosition();
          mthd_symb_obj[ResultPosition::POS_END_LINE] = (size_t)mthd->GetEndLineNumber() - 2;
          mthd_symb_obj[ResultPosition::POS_END_POS] = mthd->GetEndLinePosition();
          mthds_array_ptr[k] = (size_t)mthd_symb_obj;
        }
        klass_symb_obj[ResultPosition::POS_CHILDREN] = (size_t)mthds_array;
      }

      // enums
      for(size_t j = 0; j < eenums.size(); ++j) {
        Enum* eenum = eenums[j];

        wstring eenum_name = eenum->GetName();
        const size_t eenum_name_index = eenum_name.find_last_of(L'#');
        if(eenum_name_index != wstring::npos) {
          eenum_name = eenum_name.substr(eenum_name_index + 1, eenum_name.size() - eenum_name_index - 1);
        }

        size_t* eenum_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
        eenum_symb_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, eenum_name);
        eenum_symb_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringValue(context, eenum->GetFileName());
        eenum_symb_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_ENUM; // enum type
        eenum_symb_obj[ResultPosition::POS_START_LINE] = eenum->GetLineNumber();
        eenum_symb_obj[ResultPosition::POS_START_POS] = eenum->GetLinePosition();
        eenum_symb_obj[ResultPosition::POS_END_LINE] = eenum->GetEndLineNumber();
        eenum_symb_obj[ResultPosition::POS_END_POS] = eenum->GetEndLinePosition();
        klass_array_ptr[index++] = (size_t)eenum_symb_obj;
      }

      // attach list of bundles or classes
      if(bundle_symb_obj) {
        bundle_symb_obj[ResultPosition::POS_CHILDREN] = (size_t)klass_array;
      }
    }

    // file root
    size_t* file_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
    file_symb_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, file_uri);
    file_symb_obj[ResultPosition::POS_CODE] = validated ? 1 : 0;
    file_symb_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_FILE; // file type
    file_symb_obj[ResultPosition::POS_CHILDREN] = bundle_array ? (size_t)bundle_array : (size_t)klass_array;
    file_symb_obj[ResultPosition::POS_START_LINE] = file_symb_obj[ResultPosition::POS_START_POS] = file_symb_obj[ResultPosition::POS_END_LINE] = file_symb_obj[ResultPosition::POS_END_POS] = -1;

    prgm_obj[2] = (size_t)file_symb_obj;
  }

  //
  // find definition
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void diag_find_definition(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const wstring uri = APITools_GetStringValue(context, 2);
    const int line_num = (int)APITools_GetIntValue(context, 3);
    const int line_pos = (int)APITools_GetIntValue(context, 4);

    const wstring lib_path = APITools_GetStringValue(context, 5);

    Class* klass = nullptr;
    Method* method = nullptr;
    SymbolTable* table = nullptr;

    // TODO: check the right file
    if(program->FindMethodOrClass(uri, line_num, klass, method, table)) {
      // method level
      if(method) {
        wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }

        ContextAnalyzer analyzer(program, full_lib_path, false, false);
        if(analyzer.Analyze()) {
          wstring found_name; int found_line; int found_start_pos; int found_end_pos; Class* klass = nullptr;
          Enum* eenum = nullptr; ; EnumItem* eenum_item = nullptr;
          if(analyzer.GetDefinition(method, line_num, line_pos, found_name, found_line, found_start_pos, found_end_pos, klass, eenum, eenum_item)) {
            ParseNode* node = nullptr;

            // class
            if(klass) {
              node = klass;
            }
            // enum item
            else if(eenum) {
              node = eenum_item;
            }
            /*
                  // enum
                  else if(eenum) {
                    node = eenum;
                  }
            */
            // method
            else {
              node = method;
            }

            size_t* def_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
            def_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, found_name);
            def_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringValue(context, node->GetFileName());
            def_obj[ResultPosition::POS_START_LINE] = (size_t)node->GetLineNumber() - 1;
            def_obj[ResultPosition::POS_END_LINE] = def_obj[ResultPosition::POS_START_LINE];
            def_obj[ResultPosition::POS_START_POS] = (size_t)node->GetLinePosition() - 1;
            def_obj[ResultPosition::POS_END_POS] = (size_t)node->GetLinePosition() + 80;
            APITools_SetObjectValue(context, 0, def_obj);
          }
        }
      }
      // class level
      else {
        wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }

        ContextAnalyzer analyzer(program, full_lib_path, false, false);
        if(analyzer.Analyze()) {
          Class* found_klass = nullptr;
          if(analyzer.GetDefinition(klass, line_num, line_pos, found_klass)) {
            size_t* def_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
            def_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, found_klass->GetName());
            def_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringValue(context, found_klass->GetFileName());
            def_obj[ResultPosition::POS_START_LINE] = (size_t)found_klass->GetLineNumber() - 1;
            def_obj[ResultPosition::POS_END_LINE] = def_obj[ResultPosition::POS_START_LINE];
            def_obj[ResultPosition::POS_START_POS] = (size_t)found_klass->GetLinePosition() - 1;
            def_obj[ResultPosition::POS_END_POS] = (size_t)found_klass->GetLinePosition() + 80;
            APITools_SetObjectValue(context, 0, def_obj);
          }
        }
      }
    }
  }

  //
  // find declaration
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void diag_find_declaration(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const wstring uri = APITools_GetStringValue(context, 2);
    const int line_num = (int)APITools_GetIntValue(context, 3);
    const int line_pos = (int)APITools_GetIntValue(context, 4);

    const wstring lib_path = APITools_GetStringValue(context, 5);

    Class* klass = nullptr;
    Method* method = nullptr;
    SymbolTable* table = nullptr;

    if(program->FindMethodOrClass(uri, line_num, klass, method, table)) {
      if(method) {
        wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }

        ContextAnalyzer analyzer(program, full_lib_path, false, false);
        if(analyzer.Analyze()) {
          wstring found_name; int found_line; int found_start_pos; int found_end_pos;
          if(analyzer.GetDeclaration(method, line_num, line_pos, found_name, found_line, found_start_pos, found_end_pos)) {
            size_t* dcrl_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
            dcrl_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, found_name);
            dcrl_obj[ResultPosition::POS_START_LINE] = dcrl_obj[ResultPosition::POS_END_LINE] = (size_t)found_line - 1;
            dcrl_obj[ResultPosition::POS_START_POS] = (size_t)found_start_pos - 1;
            dcrl_obj[ResultPosition::POS_END_POS] = (size_t)found_end_pos - 1;

            APITools_SetObjectValue(context, 0, dcrl_obj);
          }
        }
      }
      /*
      // TODO: class support
      else {

      }
      */
    }
  }

  //
  // completion
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void diag_completion_help(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const wstring uri = APITools_GetStringValue(context, 2);
    const int line_num = (int)APITools_GetIntValue(context, 3);
    const int line_pos = (int)APITools_GetIntValue(context, 4);

    const wstring var_str = APITools_GetStringValue(context, 5);
    const wstring mthd_str = APITools_GetStringValue(context, 6);
    const wstring lib_path = APITools_GetStringValue(context, 7);

    Class* klass = nullptr;
    Method* method = nullptr;
    SymbolTable* table = nullptr;

    if(program->FindMethodOrClass(uri, line_num, klass, method, table)) {
      if(method) {
        wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }

        ContextAnalyzer analyzer(program, full_lib_path, false, false);
        if(analyzer.Analyze()) {
          vector<pair<int, wstring>> completions;

          if(analyzer.GetCompletion(program, method, var_str, mthd_str, line_num, line_pos, completions)) {
            size_t* sig_root_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
            sig_root_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, L"Completions");

            size_t* completions_array = APITools_MakeIntArray(context, (int)completions.size());
            size_t* completions_array_ptr = completions_array + 3;

            for(size_t i = 0; i < completions.size(); ++i) {
              pair<int, wstring> completion = completions[i];
              size_t* completion_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");

              completion_obj[ResultPosition::POS_CODE] = completion.first;
              completion_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, completion.second);

              completions_array_ptr[i] = (size_t)completion_obj;
            }

            // set result
            sig_root_obj[ResultPosition::POS_CHILDREN] = (size_t)completions_array;
            APITools_SetObjectValue(context, 0, sig_root_obj);
          }
        }
      }
    }
  }

  //
  // hover support
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void diag_hover(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const wstring uri = APITools_GetStringValue(context, 2);
    const int line_num = (int)APITools_GetIntValue(context, 3);
    const int line_pos = (int)APITools_GetIntValue(context, 4);

    const wstring var_str = APITools_GetStringValue(context, 5);
    const wstring mthd_str = APITools_GetStringValue(context, 6);
    wstring lib_path = APITools_GetStringValue(context, 7);

    Class* klass = nullptr;
    Method* method = nullptr;
    SymbolTable* table = nullptr;

    if(program->FindMethodOrClass(uri, line_num, klass, method, table)) {
      if(method) {
        wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }

        ContextAnalyzer analyzer(program, full_lib_path, false, false);
        if(analyzer.Analyze()) {
          wstring found_name; int found_line; int found_start_pos; int found_end_pos; Expression* found_expression;  SymbolEntry* found_entry;

          size_t* hover_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
          if(analyzer.GetHover(method, line_num, line_pos, found_name, found_line, found_start_pos, found_end_pos, found_expression, found_entry)) {
            if(found_expression) {
              if(found_expression->GetExpressionType() == METHOD_CALL_EXPR) {
                MethodCall* called_method = static_cast<MethodCall*>(found_expression);

                // variable type
                SymbolEntry* called_method_entry = called_method->GetEntry();
                if(called_method_entry) {
                  hover_obj[ResultPosition::POS_TYPE] = called_method_entry->GetType()->GetType();
                  hover_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, called_method_entry->GetType()->GetName());
                }
                else if(called_method->GetVariable() && called_method->GetVariable()->GetEntry()) {
                  called_method_entry = called_method->GetVariable()->GetEntry();
                  hover_obj[ResultPosition::POS_TYPE] = called_method_entry->GetType()->GetType();
                  hover_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, called_method_entry->GetType()->GetName());
                }

                hover_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringValue(context, called_method->GetMethodName());
              }
              else if(found_expression->GetExpressionType() == VAR_EXPR) {
                Variable* called_variable = static_cast<Variable*>(found_expression);
                found_entry = called_variable->GetEntry();
              }
            }
          }

          if(found_entry && found_entry->GetType()) {
            Type* found_type = found_entry->GetType();
            hover_obj[ResultPosition::POS_TYPE] = found_type->GetType();

            switch(found_type->GetType()) {
            case frontend::BYTE_TYPE:
              hover_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, L"System.$Byte");
              break;

            case frontend::CHAR_TYPE:
              hover_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, L"System.$Char");
              break;

            case frontend::INT_TYPE:
              hover_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, L"System.$Int");
              break;

            case frontend::FLOAT_TYPE:
              hover_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, L"System.$Float");
              break;

            case frontend::BOOLEAN_TYPE:
              hover_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, L"System.$Bool");
              break;

            default:
              hover_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, found_type->GetName());
              break;
            }
          }

          APITools_SetObjectValue(context, 0, hover_obj);
        }
      }
    }
  }

  //
  // help signature
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void diag_signature_help(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const wstring uri = APITools_GetStringValue(context, 2);

    const int line_num = (int)APITools_GetIntValue(context, 3);
    const int line_pos = (int)APITools_GetIntValue(context, 4);

    const wstring var_str = APITools_GetStringValue(context, 5);
    const wstring mthd_str = APITools_GetStringValue(context, 6);
    const wstring lib_path = APITools_GetStringValue(context, 7);

    Class* klass = nullptr;
    Method* method = nullptr;
    SymbolTable* table = nullptr;

    if(program->FindMethodOrClass(uri, line_num, klass, method, table)) {
      if(method) {
        wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }

        ContextAnalyzer analyzer(program, full_lib_path, false, false);
        if(analyzer.Analyze()) {
          vector<Method*> found_methods; vector<LibraryMethod*> found_lib_methods;
          if(analyzer.GetSignature(method, var_str, mthd_str, found_methods, found_lib_methods)) {
            size_t* sig_root_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
            sig_root_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, mthd_str);
            sig_root_obj[ResultPosition::POS_CODE] = found_methods.empty();

            if(!found_methods.empty()) {
              size_t* signature_array = APITools_MakeIntArray(context, (int)found_methods.size());
              size_t* signature_array_array_ptr = signature_array + 3;

              for(size_t i = 0; i < found_methods.size(); ++i) {
                Method* found_method = found_methods[i];

                size_t* mthd_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
                mthd_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, found_method->GetUserName());

                // TODO: params
                vector<frontend::Declaration*> declarations = found_method->GetDeclarations()->GetDeclarations();
                size_t* mthd_parm_array = APITools_MakeIntArray(context, (int)declarations.size());
                size_t* mthd_parm_array_ptr = mthd_parm_array + 3;

                for(size_t j = 0; j < declarations.size(); ++j) {
                  wstring type_name; GetTypeName(declarations[j]->GetEntry()->GetType(), type_name);
                  size_t* mthd_parm_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
                  mthd_parm_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, type_name);

                  mthd_parm_array_ptr[j] = (size_t)mthd_parm_obj;
                }
                mthd_obj[ResultPosition::POS_CHILDREN] = (size_t)mthd_parm_array;

                signature_array_array_ptr[i] = (size_t)mthd_obj;
              }

              sig_root_obj[ResultPosition::POS_CHILDREN] = (size_t)signature_array;
            }
            else {
              size_t* signature_array = APITools_MakeIntArray(context, (int)found_lib_methods.size());
              size_t* signature_array_array_ptr = signature_array + 3;

              for(size_t i = 0; i < found_lib_methods.size(); ++i) {
                LibraryMethod* found_lib_method = found_lib_methods[i];

                size_t* mthd_lib_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
                mthd_lib_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, found_lib_method->GetUserName());

                // TODO: params
                vector<frontend::Type*> declarations = found_lib_method->GetDeclarationTypes();
                size_t* mthd_parm_array = APITools_MakeIntArray(context, (int)declarations.size());
                size_t* mthd_parm_array_ptr = mthd_parm_array + 3;

                for(size_t j = 0; j < declarations.size(); ++j) {
                  wstring type_name; GetTypeName(declarations[j], type_name);
                  size_t* mthd_parm_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
                  mthd_parm_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, type_name);

                  mthd_parm_array_ptr[j] = (size_t)mthd_parm_obj;
                }
                mthd_lib_obj[ResultPosition::POS_CHILDREN] = (size_t)mthd_parm_array;

                signature_array_array_ptr[i] = (size_t)mthd_lib_obj;
              }

              sig_root_obj[ResultPosition::POS_CHILDREN] = (size_t)signature_array;
            }

            // set result
            APITools_SetObjectValue(context, 0, sig_root_obj);
          }
        }
      }
    }
  }

  //
  // code rename
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void diag_code_rename(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 0);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const wstring uri = APITools_GetStringValue(context, 1);

    const int line_num = (int)APITools_GetIntValue(context, 2);
    const int line_pos = (int)APITools_GetIntValue(context, 3);
    const wstring lib_path = APITools_GetStringValue(context, 4);

    prgm_obj[4] = (size_t)GetExpressionsCalls(context, program, uri, line_num, line_pos, lib_path);
  }

  //
  // find references
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void diag_find_references(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 0);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const wstring uri = APITools_GetStringValue(context, 1);

    const int line_num = (int)APITools_GetIntValue(context, 2);
    const int line_pos = (int)APITools_GetIntValue(context, 3);
    const wstring lib_path = APITools_GetStringValue(context, 4);

    prgm_obj[4] = (size_t)GetExpressionsCalls(context, program, uri, line_num, line_pos, lib_path);
  }
}

//
// Supporting functions
//

size_t* FormatErrors(VMContext& context, const vector<wstring>& error_strings, const vector<wstring>& warning_strings)
{
  const size_t throttle = 10;
  size_t max_results = error_strings.size() + warning_strings.size();
  if(max_results > throttle) {
    max_results = throttle;
  }

  // TODO: report warnings

  size_t* diagnostics_array = APITools_MakeIntArray(context, (int)max_results);
  size_t* diagnostics_array_ptr = diagnostics_array + 3;

  // process errors
  size_t count;
  for(count = 0; count < error_strings.size() && count < max_results; ++count) {
    const wstring error_string = error_strings[count];

    // parse error string
    const size_t file_mid = error_string.find(L":(");
    const wstring file_str = error_string.substr(0, file_mid);

    const size_t msg_mid = error_string.find(L"):");
    const wstring msg_str = error_string.substr(msg_mid + 3, error_string.size() - msg_mid - 3);

    const wstring line_pos_str = error_string.substr(file_mid + 2, msg_mid - file_mid - 2);
    const size_t line_pos_mid = line_pos_str.find(L',');
    const wstring line_str = line_pos_str.substr(0, line_pos_mid);
    const wstring pos_str = line_pos_str.substr(line_pos_mid + 1, line_pos_str.size() - line_pos_mid - 1);

    wchar_t* end;
    const int line_index = (int)wcstol(line_str.c_str(), &end, 10);
    const int pos_index = (int)wcstol(pos_str.c_str(), &end, 10);

    // create objects
    size_t* diag_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
    diag_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, msg_str);
    diag_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_ERROR; // error type
    diag_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringValue(context, file_str);
    diag_obj[ResultPosition::POS_START_LINE] = (size_t)line_index;
    diag_obj[ResultPosition::POS_START_POS] = (size_t)pos_index;
    diag_obj[ResultPosition::POS_END_LINE] = diag_obj[ResultPosition::POS_END_POS] = -1;
    diagnostics_array_ptr[count] = (size_t)diag_obj;
  }

  // process warnings
  for(size_t i = 0; i < warning_strings.size() && count < max_results; ++i, ++count) {
    const wstring warning_string = warning_strings[i];

    // parse warning string
    const size_t file_mid = warning_string.find(L":(");
    const wstring file_str = warning_string.substr(0, file_mid);

    const size_t msg_mid = warning_string.find(L"):");
    const wstring msg_str = warning_string.substr(msg_mid + 3, warning_string.size() - msg_mid - 3);

    const wstring line_pos_str = warning_string.substr(file_mid + 2, msg_mid - file_mid - 2);
    const size_t line_pos_mid = line_pos_str.find(L',');
    const wstring line_str = line_pos_str.substr(0, line_pos_mid);
    const wstring pos_str = line_pos_str.substr(line_pos_mid + 1, line_pos_str.size() - line_pos_mid - 1);

    wchar_t* end;
    const int line_index = (int)wcstol(line_str.c_str(), &end, 10);
    const int pos_index = (int)wcstol(pos_str.c_str(), &end, 10);

    // create objects
    size_t* diag_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
    diag_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, msg_str);
    diag_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_WARN; // warning type
    diag_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringValue(context, file_str);
    diag_obj[ResultPosition::POS_START_LINE] = (size_t)line_index;
    diag_obj[ResultPosition::POS_START_POS] = (size_t)pos_index;
    diag_obj[ResultPosition::POS_END_LINE] = diag_obj[ResultPosition::POS_END_POS] = -1;
    diagnostics_array_ptr[count] = (size_t)diag_obj;
  }

  return diagnostics_array;
}

size_t* GetExpressionsCalls(VMContext& context, frontend::ParsedProgram* program, const wstring uri, const int line_num, const int line_pos, const wstring lib_path)
{
  Class* klass = nullptr;
  Method* method = nullptr;
  SymbolTable* table = nullptr;

  if(program->FindMethodOrClass(uri, line_num, klass, method, table)) {
    // within a method
    if(method) {
      wstring full_lib_path = L"lang.obl";
      if(!lib_path.empty()) {
        full_lib_path += L',' + lib_path;
      }

      ContextAnalyzer analyzer(program, full_lib_path, false, false);
      if(analyzer.Analyze()) {
        // fetch renamed expressions
        bool is_var;
        vector<Expression*> expressions = GetMatchedExpressions(method, analyzer, line_num, line_pos, is_var);

        // method/function
        if(!is_var && !expressions.empty() && expressions[0]->GetExpressionType() == METHOD_CALL_EXPR) {
          Method* search_method = static_cast<MethodCall*>(expressions[0])->GetMethod();
          expressions.clear();

          if(search_method) {
            vector<ParsedBundle*> bundles = program->GetBundles();
            for(size_t i = 0; i < bundles.size(); ++i) {
              vector<Class*> classes = bundles[i]->GetClasses();
              for(size_t j = 0; j < classes.size(); ++j) {
                vector<Method*> methods = classes[j]->GetMethods();
                for(size_t k = 0; k < methods.size(); ++k) {
                  // TODO: all method calls (statements and expressions)
                  vector<Expression*> method_expressions = methods[k]->GetExpressions();
                  for(size_t l = 0; l < method_expressions.size(); ++l) {
                    if(method_expressions[l]->GetExpressionType() == METHOD_CALL_EXPR) {
                      MethodCall* local_method_call = static_cast<MethodCall*>(method_expressions[l]);
                      if(local_method_call->GetMethod() == search_method) {
                        expressions.push_back(local_method_call);
                      }
                    }
                  }
                }
              }
            }
          }
        }

        // format
        if(!expressions.empty()) {
          size_t* refs_array = nullptr;
          if(is_var) {
            refs_array = APITools_MakeIntArray(context, (int)expressions.size());
          }
          else {
            refs_array = APITools_MakeIntArray(context, (int)expressions.size() + 1);
          }
          size_t* refs_array_ptr = refs_array + 3;

          Method* mthd_dclr = nullptr;
          for(size_t i = 0; i < expressions.size(); ++i) {
            Expression* expression = expressions[i];

            size_t* reference_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
            int start_pos = expression->GetLinePosition();
            int end_pos = start_pos;

            switch(expression->GetExpressionType()) {
            case VAR_EXPR: {
              Variable* variable = static_cast<Variable*>(expression);
              const wstring variable_name = variable->GetName();
              end_pos += (int)variable_name.size();

              reference_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, variable->GetName());
              reference_obj[ResultPosition::POS_TYPE] = 100;
            }
              break;

            case METHOD_CALL_EXPR: {
              MethodCall* method_call = static_cast<MethodCall*>(expression);
              if(is_var) {
                end_pos += (int)method_call->GetVariableName().size();
              }
              else {
                mthd_dclr = method_call->GetMethod();
                if(method_call->GetMidLinePosition() < 0) {
                  start_pos = end_pos = expression->GetLinePosition();
                  end_pos += (int)method_call->GetMethodName().size();
                }
                else {
                  start_pos = end_pos = method_call->GetMidLinePosition();
                  end_pos += (int)method_call->GetMethodName().size();
                }
              }

              reference_obj[ResultPosition::POS_TYPE] = 200;
              reference_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, method_call->GetMethodName());
            }
              break;

            default:
              break;
            }

            reference_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringValue(context, expression->GetFileName());
            reference_obj[ResultPosition::POS_START_LINE] = reference_obj[ResultPosition::POS_END_LINE] = (size_t)expression->GetLineNumber() - 1;
            reference_obj[ResultPosition::POS_START_POS] = (size_t)start_pos - 1;
            reference_obj[ResultPosition::POS_END_POS] = (size_t)end_pos - 1;
            refs_array_ptr[i] = (size_t)reference_obj;
          }

          // update declaration name
          if(!is_var && mthd_dclr) {
            size_t* reference_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");

            int start_pos = mthd_dclr->GetMidLinePosition();

            const wstring mthd_dclr_long_name = mthd_dclr->GetName();
            const size_t mthd_dclr_index = mthd_dclr_long_name.find(L':');
            if(mthd_dclr_index != wstring::npos) {
              const wstring mthd_dclr_name = mthd_dclr_long_name.substr(mthd_dclr_index + 1);
              int end_pos = start_pos + (int)mthd_dclr_name.size();

              reference_obj[ResultPosition::POS_TYPE] = 200;
              reference_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, mthd_dclr->GetName());
              reference_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringValue(context, mthd_dclr->GetFileName());
              reference_obj[ResultPosition::POS_START_LINE] = reference_obj[ResultPosition::POS_END_LINE] = (size_t)mthd_dclr->GetLineNumber() - 1;
              reference_obj[ResultPosition::POS_START_POS] = (size_t)start_pos - 1;
              reference_obj[ResultPosition::POS_END_POS] = (size_t)end_pos - 1;

              refs_array_ptr[(int)expressions.size()] = (size_t)reference_obj;
            }
          }

          return refs_array;
        }
      }
    }
    // within a class
    else {
      wstring full_lib_path = L"lang.obl";
      if(!lib_path.empty()) {
        full_lib_path += L',' + lib_path;
      }

      ContextAnalyzer analyzer(program, full_lib_path, false, false);
      if(analyzer.Analyze()) {
        // fetch renamed expressions
        vector<Expression*> expressions = GetMatchedExpressions(klass, analyzer, line_num, line_pos);
        if(!expressions.empty()) {
          // build results array
          size_t* refs_array = APITools_MakeIntArray(context, (int)expressions.size());
          size_t* refs_array_ptr = refs_array + 3;

          for(size_t i = 0; i < expressions.size(); ++i) {
            Expression* expression = expressions[i];

            size_t* reference_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
            int start_pos = expression->GetLinePosition();
            int end_pos = start_pos;

            switch(expression->GetExpressionType()) {
            case VAR_EXPR: {
              Variable* variable = static_cast<Variable*>(expression);
              const wstring variable_name = variable->GetName();
              end_pos += (int)variable_name.size();

              reference_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, variable->GetName());
              reference_obj[ResultPosition::POS_TYPE] = 100;
            }
              break;

            case METHOD_CALL_EXPR: {
              MethodCall* method_call = static_cast<MethodCall*>(expression);
              end_pos += (int)method_call->GetVariableName().size();
              reference_obj[ResultPosition::POS_TYPE] = 200;
              reference_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, method_call->GetMethodName());
            }
              break;

            default:
              break;
            }

            reference_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringValue(context, expression->GetFileName());
            reference_obj[ResultPosition::POS_START_LINE] = reference_obj[ResultPosition::POS_END_LINE] = (size_t)expression->GetLineNumber() - 1;
            reference_obj[ResultPosition::POS_START_POS] = (size_t)start_pos - 1;
            reference_obj[ResultPosition::POS_END_POS] = (size_t)end_pos - 1;
            refs_array_ptr[i] = (size_t)reference_obj;
          }

          return refs_array;
        }
      }
    }
  }

  return nullptr;
}

void GetTypeName(frontend::Type* type, wstring& output)
{
  switch(type->GetType()) {
  case EntryType::NIL_TYPE:
    break;

  case EntryType::BOOLEAN_TYPE:
    output = L"Bool";
    break;

  case EntryType::BYTE_TYPE:
    output = L"Byte";
    break;

  case EntryType::CHAR_TYPE:
    output = L"Char";
    break;

  case EntryType::INT_TYPE:
    output = L"Int";
    break;

  case EntryType::FLOAT_TYPE:
    output = L"Float";
    break;

  case EntryType::CLASS_TYPE:
    output = type->GetName();
    break;

  case EntryType::FUNC_TYPE:
    break;

  case EntryType::ALIAS_TYPE:
    break;

  case EntryType::VAR_TYPE:
    break;

  default:
    output = L"Unknown";
  }
}

vector<frontend::Expression*> GetMatchedExpressions(frontend::Class* klass, class ContextAnalyzer& analyzer, const int line_num, const int line_pos)
{
  vector<Method*> methods = klass->GetMethods();
  if(!methods.empty()) {
    bool is_var;
    return GetMatchedExpressions(methods[0], analyzer, line_num, line_pos, is_var);
  }

  return vector<Expression*>();
}

vector<frontend::Expression*> GetMatchedExpressions(frontend::Method* method, class ContextAnalyzer& analyzer, const int line_num, const int line_pos, bool &is_var)
{
  bool is_cls;
  vector<Expression*> expressions = analyzer.FindExpressions(method, line_num, line_pos, is_var, is_cls);

  if(is_cls && !expressions.empty()) {
    wstring found_name;
    // get found name
    if(expressions[0]->GetExpressionType() == VAR_EXPR) {
      Variable* variable = static_cast<Variable*>(expressions[0]);
      found_name = variable->GetName();
    }
    else if(expressions[0]->GetExpressionType() == METHOD_CALL_EXPR) {
      MethodCall* method_call = static_cast<MethodCall*>(expressions[0]);
      if(method_call->GetEntry()) {
        found_name = method_call->GetVariableName();
      }
      else if(method_call->GetMethod()) {
        found_name = method_call->GetMethodName();
      }
      else if(method_call->GetCallType() == ENUM_CALL) {
        found_name = method_call->GetVariableName();
      }
    }

    // search for matching and unique expressions
    vector<Method*> methods = method->GetClass()->GetMethods();
    for(size_t i = 0; i < methods.size(); ++i) {
      vector<Expression*> method_expressions = methods[i]->GetExpressions();
      for(size_t j = 0; j < method_expressions.size(); ++j) {
        Expression* expression = method_expressions[j];
        // add missing expression
        if(expression->GetExpressionType() == METHOD_CALL_EXPR &&
           find(expressions.begin(), expressions.end(), expression) == expressions.end()) {
          MethodCall* method_call = static_cast<MethodCall*>(expression);
          if(method_call->GetVariableName() == found_name) {
            expressions.push_back(expression);
          }
        }
      }
    }
  }
  
  return expressions;
}
