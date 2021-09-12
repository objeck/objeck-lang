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
    const int names_array_size = APITools_GetArraySize(names_array);

    size_t* texts_array = APITools_GetArray(APITools_GetObjectValue(context, 3));
    const int texts_array_size = APITools_GetArraySize(texts_array);
    
    if(names_array_size > 0 && names_array_size == texts_array_size) {
      vector<pair<wstring,wstring>> texts;
      for(int i = 0; i < texts_array_size; ++i) {
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

    const wstring lib_path = APITools_GetStringValue(context, 1);
    wstring full_lib_path = L"lang.obl";
    if(!lib_path.empty()) {
      full_lib_path += L',' + lib_path;
    }

    // if parsed
    if(prgm_obj[1]) {
      ContextAnalyzer analyzer(program, full_lib_path, false, false);
      if(!analyzer.Analyze()) {
        vector<wstring> error_strings = program->GetErrorStrings();
        size_t* diagnostics_array = FormatErrors(context, error_strings);
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

    const wstring lib_path = APITools_GetStringValue(context, 1);

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

    const vector<ParsedBundle*> bundles = program->GetBundles();
    size_t* bundle_array = APITools_MakeIntArray(context, (int)bundles.size());
    size_t* bundle_array_ptr = bundle_array + 3;

    // bundles
    wstring file_uri;

    for(size_t i = 0; i < bundles.size(); ++i) {
      ParsedBundle* bundle = bundles[i];
      
      if(file_uri.empty()) {
        file_uri = bundle->GetFileName();
      }

      // bundle
      size_t* bundle_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
      const wstring bundle_name = bundle->GetName();
      bundle_symb_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, bundle_name.empty() ? L"Default" : bundle_name);
      bundle_symb_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_NAMESPACE; // namespace type
      bundle_symb_obj[ResultPosition::POS_START_LINE] = bundle->GetLineNumber();
      bundle_symb_obj[ResultPosition::POS_START_POS] = bundle->GetLinePosition();
      bundle_symb_obj[ResultPosition::POS_END_LINE] = bundle->GetEndLineNumber();
      bundle_symb_obj[ResultPosition::POS_END_POS] = bundle->GetEndLinePosition();
      bundle_array_ptr[i] = (size_t)bundle_symb_obj;

      // get classes and enums
      const vector<Class*> klasses = bundle->GetClasses();
      const vector<Enum*> eenums = bundle->GetEnums();

      // classes
      size_t* klass_array = APITools_MakeIntArray(context, (int)(klasses.size() + eenums.size()));
      size_t* klass_array_ptr = klass_array + 3;

      size_t index = 0;
      for(size_t j = 0; j < klasses.size(); ++j) {
        Class* klass = klasses[j];

        size_t* klass_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
        klass_symb_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, klass->GetName());
        klass_symb_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringValue(context, klass->GetFileName());
        klass_symb_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_CLASS; // class type
        klass_symb_obj[ResultPosition::POS_START_LINE] = klass->GetLineNumber();
        klass_symb_obj[ResultPosition::POS_START_POS] = klass->GetLinePosition();
        klass_symb_obj[ResultPosition::POS_END_LINE] = klass->GetEndLineNumber();
        klass_symb_obj[ResultPosition::POS_END_POS] = klass->GetEndLinePosition();
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
          mthd_symb_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_METHOD; // method type
          mthd_symb_obj[ResultPosition::POS_START_LINE] = mthd->GetLineNumber() - 1;
          mthd_symb_obj[ResultPosition::POS_START_POS] = mthd->GetLinePosition();
          mthd_symb_obj[ResultPosition::POS_END_LINE] = mthd->GetEndLineNumber() - 2;
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

      bundle_symb_obj[ResultPosition::POS_CHILDREN] = (size_t)klass_array;
    }

    // file root
    size_t* file_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
    file_symb_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, file_uri);
    file_symb_obj[ResultPosition::POS_CODE] = validated ? 1 : 0;
    file_symb_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_FILE; // file type
    file_symb_obj[ResultPosition::POS_CHILDREN] = (size_t)bundle_array;
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

    const int line_num = (int)APITools_GetIntValue(context, 2);
    const int line_pos = (int)APITools_GetIntValue(context, 3);
    const wstring lib_path = APITools_GetStringValue(context, 4);

    Class* klass = nullptr;
    Method* method = nullptr;
    SymbolTable* table = nullptr;

    if(program->FindMethod(line_num, klass, method, table)) {
      // method level
      if(method) {
        wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }

        ContextAnalyzer analyzer(program, full_lib_path, false, false);
        if(analyzer.Analyze()) {
          wstring found_name; int found_line; int found_start_pos; int found_end_pos; Class* klass = nullptr; Enum* eenum = nullptr;
          if(analyzer.GetDefinition(method, line_num, line_pos, found_name, found_line, found_start_pos, found_end_pos, klass, eenum)) {
            ParseNode* node = nullptr;

            // class
            if(klass) {
              node = klass;
            }
            // enum
            else if(eenum) {
              node = eenum;
            }
            // method
            else {
              node = method;
            }

            size_t* def_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
            def_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, found_name);
            def_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringValue(context, node->GetFileName());
            def_obj[ResultPosition::POS_START_LINE] = def_obj[ResultPosition::POS_END_LINE] = node->GetLineNumber() - 1;
            def_obj[ResultPosition::POS_START_POS] = node->GetLinePosition() - 1;
            def_obj[ResultPosition::POS_END_POS] = node->GetLinePosition() + 80;
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
            def_obj[ResultPosition::POS_START_LINE] = def_obj[ResultPosition::POS_END_LINE] = found_klass->GetLineNumber() - 1;
            def_obj[ResultPosition::POS_START_POS] = found_klass->GetLinePosition() - 1;
            def_obj[ResultPosition::POS_END_POS] = found_klass->GetLinePosition() + 80;
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

    const int line_num = (int)APITools_GetIntValue(context, 2);
    const int line_pos = (int)APITools_GetIntValue(context, 3);
    const wstring lib_path = APITools_GetStringValue(context, 4);

    Class* klass = nullptr;
    Method* method = nullptr;
    SymbolTable* table = nullptr;

    if(program->FindMethod(line_num, klass, method, table)) {
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
            dcrl_obj[ResultPosition::POS_START_LINE] = dcrl_obj[ResultPosition::POS_END_LINE] = found_line - 1;
            dcrl_obj[ResultPosition::POS_START_POS] = found_start_pos - 1;
            dcrl_obj[ResultPosition::POS_END_POS] = found_end_pos - 1;

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

    const int line_num = (int)APITools_GetIntValue(context, 2);
    // const int line_pos = (int)APITools_GetIntValue(context, 3);
    const wstring var_str = APITools_GetStringValue(context, 4);
    const wstring mthd_str = APITools_GetStringValue(context, 5);
    const wstring lib_path = APITools_GetStringValue(context, 6);

    Class* klass = nullptr;
    Method* method = nullptr;
    SymbolTable* table = nullptr;

    if(program->FindMethod(line_num, klass, method, table)) {
      if(method) {
        wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }

        ContextAnalyzer analyzer(program, full_lib_path, false, false);
        if(analyzer.Analyze()) {
          vector<pair<int, wstring>> completions;

          if(analyzer.GetCompletion(method, var_str, mthd_str, completions)) {
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
  // help signature
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_signature_help(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const int line_num = (int)APITools_GetIntValue(context, 2);
    // const int line_pos = (int)APITools_GetIntValue(context, 3);
    const wstring var_str = APITools_GetStringValue(context, 4);
    const wstring mthd_str = APITools_GetStringValue(context, 5);
    const wstring lib_path = APITools_GetStringValue(context, 6);

    Class* klass = nullptr;
    Method* method = nullptr;
    SymbolTable* table = nullptr;

    if(program->FindMethod(line_num, klass, method, table)) {
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

    const int line_num = (int)APITools_GetIntValue(context, 1);
    const int line_pos = (int)APITools_GetIntValue(context, 2);
    const wstring lib_path = APITools_GetStringValue(context, 3);

    Class* klass = nullptr;
    Method* method = nullptr;
    SymbolTable* table = nullptr;

    if(program->FindMethod(line_num, klass, method, table)) {
      if(method) {
        wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }

        ContextAnalyzer analyzer(program, full_lib_path, false, false);
        if(analyzer.Analyze()) {
          vector<Expression*> expressions = analyzer.FindExpressions(method, line_num, line_pos);
          size_t* refs_array = APITools_MakeIntArray(context, (int)expressions.size());
          size_t* refs_array_ptr = refs_array + 3;

          for(size_t i = 0; i < expressions.size(); ++i) {
            Expression* expression = expressions[i];

            size_t* reference_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
            int start_pos = expression->GetLinePosition() - 1;
            int end_pos = start_pos;

            switch(expression->GetExpressionType()) {
            case VAR_EXPR: {
              Variable* variable = static_cast<Variable*>(expression);
              end_pos += (int)variable->GetName().size();
              reference_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, variable->GetName());
            }
              break;

            case METHOD_CALL_EXPR: {
              MethodCall* method_call = static_cast<MethodCall*>(expression);
              start_pos++; end_pos++;
              end_pos += (int)method_call->GetVariableName().size();
              reference_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, method_call->GetMethodName());
            }
              break;

            default:
              break;
            }

            reference_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_VARIABLE; // variable type
            reference_obj[ResultPosition::POS_START_LINE] = reference_obj[ResultPosition::POS_END_LINE] = expression->GetLineNumber() - 1;
            reference_obj[ResultPosition::POS_START_POS] = start_pos - 1;
            reference_obj[ResultPosition::POS_END_POS] = end_pos - 1;
            refs_array_ptr[i] = (size_t)reference_obj;
          }

          prgm_obj[4] = (size_t)refs_array;
        }
      }
      // look for variables across classes
      else {
        wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }

        ContextAnalyzer analyzer(program, full_lib_path, false, false);
        if(analyzer.Analyze()) {
          Declaration* declaration = analyzer.FindDeclaration(klass, line_num, line_pos);
          if(declaration && declaration->GetEntry()) {
            vector<Variable*> expressions = declaration->GetEntry()->GetVariables();
            if(!expressions.empty()) {
              size_t* refs_array = APITools_MakeIntArray(context, (int)expressions.size());
              size_t* refs_array_ptr = refs_array + 3;

              for(size_t i = 0; i < expressions.size(); ++i) {
                Expression* expression = expressions[i];

                size_t* reference_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
                int start_pos = expression->GetLinePosition() - 1;
                int end_pos = start_pos;

                switch(expression->GetExpressionType()) {
                case VAR_EXPR: {
                  Variable* variable = static_cast<Variable*>(expression);
                  end_pos += (int)variable->GetName().size();
                  reference_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, variable->GetName());
                }
                  break;

                case METHOD_CALL_EXPR: {
                  MethodCall* method_call = static_cast<MethodCall*>(expression);
                  start_pos++; end_pos++;
                  end_pos += (int)method_call->GetVariableName().size();
                  reference_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, method_call->GetMethodName());
                }
                  break;

                default:
                  break;
                }

                reference_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_VARIABLE; // variable type
                reference_obj[ResultPosition::POS_START_LINE] = reference_obj[ResultPosition::POS_END_LINE] = expression->GetLineNumber() - 1;
                reference_obj[ResultPosition::POS_START_POS] = start_pos - 1;
                reference_obj[ResultPosition::POS_END_POS] = end_pos - 1;
                refs_array_ptr[i] = (size_t)reference_obj;
              }

              prgm_obj[4] = (size_t)refs_array;
            }
          }
        }
      }
    }
  }

  //
  // Supporting functions
  //
  
  size_t* FormatErrors(VMContext& context, vector<wstring> error_strings)
  {
    const size_t throttle = 10;
    size_t max_results = error_strings.size();
    if(max_results > throttle) {
      max_results = throttle;
    }

    size_t* diagnostics_array = APITools_MakeIntArray(context, (int)max_results);
    size_t* diagnostics_array_ptr = diagnostics_array + 3;

    for(size_t i = 0; i < max_results; ++i) {
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
      
      wchar_t* end;      
      const int line_index = (int)wcstol(line_str.c_str(), &end, 10);
      const int pos_index = (int)wcstol(pos_str.c_str(), &end, 10);
      
      // create objects
      size_t* diag_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
      diag_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringValue(context, msg_str);
      diag_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_ERROR; // error type
      diag_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringValue(context, file_str);
      diag_obj[ResultPosition::POS_START_LINE] = line_index;
      diag_obj[ResultPosition::POS_START_POS] = pos_index;
      diag_obj[ResultPosition::POS_END_LINE] = diag_obj[ResultPosition::POS_END_POS] = -1;
      diagnostics_array_ptr[i] = (size_t)diag_obj;
    }

    return diagnostics_array;
  }
}

