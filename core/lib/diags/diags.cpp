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

using namespace frontend;

// SEH guard: prevents access violations from crashing the LSP server process
#ifdef _WIN32
static void SafeCallDiag(void (*fn)(VMContext&), VMContext& ctx) {
  __try { fn(ctx); } __except(EXCEPTION_EXECUTE_HANDLER) { }
}
#else
static inline void SafeCallDiag(void (*fn)(VMContext&), VMContext& ctx) { fn(ctx); }
#endif

extern "C" {

  //
  // initialize diagnostics environment
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void load_lib(VMContext& context)
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
    const std::wstring src_file(APITools_GetStringValue(context, 2));

    std::vector<std::pair<std::wstring, std::wstring> > programs;

    Parser parser(src_file, false, programs);
    const bool was_parsed = parser.Parse();

    ParsedProgram* progam = parser.GetProgram();
    APITools_SetIntValue(context, 0, (size_t)progam);
    APITools_SetIntValue(context, 1, was_parsed ? 1 : 0);
    APITools_SetIntValue(context, 3, (size_t)parser.GetSymbolTable());
    APITools_SetIntValue(context, 4, HasUserUses(progam));
  }

  //
  // Parse text
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_parse_text(VMContext& context)
  {
    size_t* names_array = APITools_GetArrayAddress(APITools_GetObjectValue(context, 2));
    const size_t names_array_size = APITools_GetArraySize(names_array);

    size_t* texts_array = APITools_GetArrayAddress(APITools_GetObjectValue(context, 3));
    const size_t texts_array_size = APITools_GetArraySize(texts_array);

    if(names_array_size > 0 && names_array_size == texts_array_size) {
      std::vector<std::pair<std::wstring, std::wstring>> texts;
      for(size_t i = 0; i < texts_array_size; ++i) {
        const wchar_t* file_name = APITools_GetStringValue(names_array, i);
        const wchar_t* file_text = APITools_GetStringValue(texts_array, i);

        if(file_name && file_text) {
          texts.push_back(std::make_pair(file_name, file_text));
        }
      }

      Parser parser(L"", false, texts);
      const bool was_parsed = parser.Parse();

      ParsedProgram* progam = parser.GetProgram();
      APITools_SetIntValue(context, 0, (size_t)progam);
      APITools_SetIntValue(context, 1, was_parsed ? 1 : 0);
      APITools_SetIntValue(context, 4, (size_t)parser.GetSymbolTable());
      APITools_SetIntValue(context, 5, HasUserUses(progam));
    }
  }

  //
  // get diagnostics (error and warnings)
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_get_diagnosis_impl(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 0);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const std::wstring uri = APITools_GetStringValue(context, 1);
    const std::wstring lib_path = APITools_GetStringValue(context, 2);

    // if parsed
    if(prgm_obj[1]) {
      std::wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }
        ContextAnalyzer analyzer_local(program, full_lib_path, false);
        ContextAnalyzer* analyzer = analyzer_local.Analyze() ? &analyzer_local : nullptr;
      const bool was_analyzed = (analyzer != nullptr);
      APITools_SetBoolValue(context, 3, was_analyzed);

      const std::vector<std::wstring> warning_strings = program->GetWarningStrings();
      if(!was_analyzed || !warning_strings.empty()) {
        std::vector<std::wstring> error_strings = program->GetErrorStrings();
        size_t* diagnostics_array = FormatErrors(context, error_strings, warning_strings);
        prgm_obj[3] = (size_t)diagnostics_array;
      }
    }
    else {
      std::vector<std::wstring> error_strings = program->GetErrorStrings();
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
  void diag_get_symbols_impl(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 0);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const std::wstring uri = APITools_GetStringValue(context, 1);

    const std::wstring lib_path = APITools_GetStringValue(context, 2);

    // if parsed
    bool was_analyzed = false;
    if(prgm_obj[1]) {
      std::wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }
        ContextAnalyzer analyzer_local(program, full_lib_path, false);
        ContextAnalyzer* analyzer = analyzer_local.Analyze() ? &analyzer_local : nullptr;
      was_analyzed = (analyzer != nullptr);
    }

    // list of bundles for classes
    size_t* bundle_array = nullptr;
    size_t* klass_array = nullptr;

    // bundles
    std::wstring file_uri;
    const std::vector<ParsedBundle*> bundles = program->GetBundles();

    for(size_t i = 0; i < bundles.size(); ++i) {
      ParsedBundle* bundle = bundles[i];

      if(file_uri.empty()) {
        file_uri = bundle->GetFileName();
      }

      // bundle
      size_t* bundle_symb_obj = nullptr;
      const std::wstring bundle_name = bundle->GetName();
      if(!bundle_name.empty()) {
        bundle_array = APITools_MakeIntArray(context, (int)bundles.size());
        size_t* bundle_array_ptr = bundle_array + 3;

        bundle_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
        bundle_symb_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, bundle_name);
        bundle_symb_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_NAMESPACE; // namespace type
        bundle_symb_obj[ResultPosition::POS_START_LINE] = bundle->GetLineNumber();
        bundle_symb_obj[ResultPosition::POS_START_POS] = bundle->GetLinePosition();
        bundle_symb_obj[ResultPosition::POS_END_LINE] = bundle->GetEndLineNumber();
        bundle_symb_obj[ResultPosition::POS_END_POS] = bundle->GetEndLinePosition();

        bundle_array_ptr[i] = (size_t)bundle_symb_obj;
      }

      // get classes and enums
      const std::vector<Class*> klasses = bundle->GetClasses();
      const std::vector<Enum*> eenums = bundle->GetEnums();

      // classes
      klass_array = APITools_MakeIntArray(context, (int)(klasses.size() + eenums.size()));
      size_t* klass_array_ptr = klass_array + 3;

      size_t index = 0;
      for(size_t j = 0; j < klasses.size(); ++j) {
        Class* klass = klasses[j];

        size_t* klass_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
        klass_symb_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, klass->GetName());
        klass_symb_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, klass->GetFileName());
        klass_symb_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_CLASS; // class type
        klass_symb_obj[ResultPosition::POS_START_LINE] = klass->GetLineNumber() - 1;
        klass_symb_obj[ResultPosition::POS_START_POS] = klass->GetLinePosition() - 1;
        klass_symb_obj[ResultPosition::POS_END_LINE] = klass->GetEndLineNumber() - 1;
        klass_symb_obj[ResultPosition::POS_END_POS] = klass->GetEndLinePosition() - 1;
        klass_array_ptr[index++] = (size_t)klass_symb_obj;

        // methods
        std::vector<Method*> mthds = klass->GetMethods();
        size_t* mthds_array = APITools_MakeIntArray(context, (int)mthds.size());
        size_t* mthds_array_ptr = mthds_array + 3;
        for(size_t k = 0; k < mthds.size(); ++k) {
          Method* mthd = mthds[k];

          size_t* mthd_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");

          std::wstring mthd_name = mthd->GetName();
          const size_t mthd_name_index = mthd_name.find_last_of(L':');
          if(mthd_name_index != std::wstring::npos) {
            mthd_name = mthd_name.substr(mthd_name_index + 1, mthd_name.size() - mthd_name_index - 1);
          }
          mthd_symb_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, mthd_name);
          mthd_symb_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, mthd->GetFileName());

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
          mthd_symb_obj[ResultPosition::POS_START_POS] = mthd->GetLinePosition() - 1;
          mthd_symb_obj[ResultPosition::POS_END_LINE] = (size_t)mthd->GetEndLineNumber() - 2;
          mthd_symb_obj[ResultPosition::POS_END_POS] = mthd->GetEndLinePosition();
          mthds_array_ptr[k] = (size_t)mthd_symb_obj;
        }
        klass_symb_obj[ResultPosition::POS_CHILDREN] = (size_t)mthds_array;
      }

      // enums
      for(size_t j = 0; j < eenums.size(); ++j) {
        Enum* eenum = eenums[j];

        std::wstring eenum_name = eenum->GetName();
        const size_t eenum_name_index = eenum_name.find_last_of(L'#');
        if(eenum_name_index != std::wstring::npos) {
          eenum_name = eenum_name.substr(eenum_name_index + 1, eenum_name.size() - eenum_name_index - 1);
        }

        size_t* eenum_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
        eenum_symb_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, eenum_name);
        eenum_symb_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, eenum->GetFileName());
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
    file_symb_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, file_uri);
    file_symb_obj[ResultPosition::POS_CODE] = was_analyzed ? 1 : 0;
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
  void diag_find_definition_impl(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const std::wstring uri = APITools_GetStringValue(context, 2);
    const int line_num = (int)APITools_GetIntValue(context, 3);
    const int line_pos = (int)APITools_GetIntValue(context, 4);

    const std::wstring lib_path = APITools_GetStringValue(context, 5);

    // TODO: check the right file
    Class* klass; Method* method; SymbolTable* table;
    if(program->FindMethodOrClass(uri, line_num, klass, method, table)) {
      // method level
      if(method) {
        std::wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }
        ContextAnalyzer analyzer_local(program, full_lib_path, false);
        ContextAnalyzer* analyzer = analyzer_local.Analyze() ? &analyzer_local : nullptr;
        if(analyzer) {
          std::wstring found_name; int found_line; int found_start_pos; int found_end_pos; Class* klass = nullptr;
          Enum* eenum = nullptr; ; EnumItem* eenum_item = nullptr;
          if(analyzer->GetDefinition(method, line_num, line_pos, found_name, found_line, found_start_pos, found_end_pos, klass, eenum, eenum_item)) {
            ParseNode* node = nullptr;

            // class
            if(klass) {
              node = klass;
            }
            // enum item — only use eenum_item if it was actually found
            else if(eenum && eenum_item) {
              node = eenum_item;
            }
            // method
            else if(method) {
              node = method;
            }

            // Guard against null node (e.g., enum without item, method
            // returned but somehow still null) and invalid line numbers.
            if(node && node->GetLineNumber() > 0) {
              size_t* def_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
              def_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, found_name);
              def_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, node->GetFileName());
              def_obj[ResultPosition::POS_START_LINE] = (size_t)node->GetLineNumber() - 1;
              def_obj[ResultPosition::POS_END_LINE] = def_obj[ResultPosition::POS_START_LINE];
              def_obj[ResultPosition::POS_START_POS] = (size_t)node->GetLinePosition() - 1;
              def_obj[ResultPosition::POS_END_POS] = (size_t)node->GetLinePosition() + 80;
              APITools_SetObjectValue(context, 0, def_obj);
            }
          }
        }
      }
      // class level
      else {
        std::wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }
        ContextAnalyzer analyzer_local(program, full_lib_path, false);
        ContextAnalyzer* analyzer = analyzer_local.Analyze() ? &analyzer_local : nullptr;
        if(analyzer) {
          Class* found_klass = nullptr;
          if(analyzer->GetDefinition(klass, line_num, line_pos, found_klass)) {
            size_t* def_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
            def_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, found_klass->GetName());
            def_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, found_klass->GetFileName());
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
  void diag_find_declaration_impl(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const std::wstring uri = APITools_GetStringValue(context, 2);
    const int line_num = (int)APITools_GetIntValue(context, 3);
    const int line_pos = (int)APITools_GetIntValue(context, 4);

    const std::wstring lib_path = APITools_GetStringValue(context, 5);

    Class* klass; Method* method; SymbolTable* table;
    if(program->FindMethodOrClass(uri, line_num, klass, method, table)) {
      if(method) {
        std::wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }
        ContextAnalyzer analyzer_local(program, full_lib_path, false);
        ContextAnalyzer* analyzer = analyzer_local.Analyze() ? &analyzer_local : nullptr;
        if(analyzer) {
          std::wstring found_name; int found_line; int found_start_pos; int found_end_pos;
          if(analyzer->GetDeclaration(method, line_num, line_pos, found_name, found_line, found_start_pos, found_end_pos)) {
            size_t* dcrl_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
            dcrl_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, found_name);
            dcrl_obj[ResultPosition::POS_START_LINE] = dcrl_obj[ResultPosition::POS_END_LINE] = (size_t)found_line - 1;
            dcrl_obj[ResultPosition::POS_START_POS] = (size_t)found_start_pos - 1;
            dcrl_obj[ResultPosition::POS_END_POS] = (size_t)found_end_pos - 1;

            APITools_SetObjectValue(context, 0, dcrl_obj);
          }
        }
      }
    }
  }

  //
  // completion
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_completion_help_impl(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const std::wstring uri = APITools_GetStringValue(context, 2);
    const int line_num = (int)APITools_GetIntValue(context, 3);
    const int line_pos = (int)APITools_GetIntValue(context, 4);

    const std::wstring var_str = APITools_GetStringValue(context, 5);
    const std::wstring mthd_str = APITools_GetStringValue(context, 6);
    const std::wstring lib_path = APITools_GetStringValue(context, 7);

    Class* klass; Method* method; SymbolTable* table;
    if(program->FindMethodOrClass(uri, line_num, klass, method, table)) {
      if(method) {
        std::wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }
        ContextAnalyzer analyzer_local(program, full_lib_path, false);
        ContextAnalyzer* analyzer = analyzer_local.Analyze() ? &analyzer_local : nullptr;
        if(analyzer) {
          std::vector<std::pair<int, std::wstring>> completions;

          if(analyzer->GetCompletion(program, method, var_str, mthd_str, line_num, line_pos, completions)) {
            size_t* sig_root_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
            sig_root_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, L"Completions");

            size_t* completions_array = APITools_MakeIntArray(context, (int)completions.size());
            size_t* completions_array_ptr = completions_array + 3;

            for(size_t i = 0; i < completions.size(); ++i) {
              std::pair<int, std::wstring> completion = completions[i];
              size_t* completion_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");

              completion_obj[ResultPosition::POS_CODE] = completion.first;
              completion_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, completion.second);

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
  // code action
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_code_action_impl(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    SymbolTableManager* table_mgr = (SymbolTableManager*)prgm_obj[6];

    const std::wstring uri = APITools_GetStringValue(context, 2);
    const int start_line = (int)APITools_GetIntValue(context, 3);
    const int start_char = (int)APITools_GetIntValue(context, 4);
    const std::wstring cls_var_name = APITools_GetStringValue(context, 5);

    size_t* code_action_obj = nullptr;

    if(table_mgr) {
      std::vector<std::wstring> namescopes = table_mgr->GetNamescopes();
      for(size_t i = 0; code_action_obj == nullptr && i < namescopes.size(); ++i) {
        std::wstring namescope = namescopes[i];

        std::vector<SymbolEntry*> entries = table_mgr->GetEntries(namescope);
        for(size_t j = 0; code_action_obj == nullptr && j < entries.size(); ++j) {
          SymbolEntry* entry = entries[j];
          // Guard against null type (can happen for entries created before
          // type resolution, e.g., inferred-type locals from `:=`).
          if(entry == nullptr || entry->GetType() == nullptr) {
            continue;
          }
          if(entry->GetType()->GetType() == CLASS_TYPE) {
            const std::wstring entry_dec_var_name = entry->GetName();
            const size_t entry_var_index = entry_dec_var_name.find_last_of(L':');

            if(entry_var_index != std::wstring::npos) {
              const std::wstring entry_type_name = entry->GetType()->GetName();
              const std::wstring entry_var_name = entry_dec_var_name.substr(entry_var_index + 1);

              // declaration match
              if(entry->GetLineNumber() == start_line + 1 && entry->GetLinePosition() == start_char + 1 && entry_type_name == cls_var_name) {
                code_action_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
                code_action_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, entry_type_name);
                code_action_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, entry_var_name);
                code_action_obj[ResultPosition::POS_START_LINE] = entry->GetType()->GetLineNumber() - 1;
                code_action_obj[ResultPosition::POS_START_POS] = entry->GetType()->GetLinePosition() - 1;
              }
              // variable match
              else if(entry->GetLineNumber() <= start_line + 1 && entry->GetLinePosition() <= start_char + 1) {
                if(entry_var_name == cls_var_name) {
                  code_action_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
                  code_action_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, entry_type_name);
                  code_action_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, entry_var_name);
                  code_action_obj[ResultPosition::POS_START_LINE] = entry->GetType()->GetLineNumber() - 1;
                  code_action_obj[ResultPosition::POS_START_POS] = entry->GetType()->GetLinePosition() - 1;
                }
              }
            }
          }
        }
      }
    }

    APITools_SetObjectValue(context, 0, code_action_obj);
  }

  //
  // hover support
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_hover_impl(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const std::wstring uri = APITools_GetStringValue(context, 2);
    const int line_num = (int)APITools_GetIntValue(context, 3);
    const int line_pos = (int)APITools_GetIntValue(context, 4);

    const std::wstring var_str = APITools_GetStringValue(context, 5);
    const std::wstring mthd_str = APITools_GetStringValue(context, 6);
    std::wstring lib_path = APITools_GetStringValue(context, 7);
    Class* klass = nullptr; Method* method = nullptr; SymbolTable* table = nullptr;
    if(program && program->FindMethodOrClass(uri, line_num, klass, method, table)) {
      if(method) {
        std::wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }
        ContextAnalyzer analyzer_local(program, full_lib_path, false);
        ContextAnalyzer* analyzer = analyzer_local.Analyze() ? &analyzer_local : nullptr;
        if(analyzer) {
          std::wstring found_name; int found_line = 0; int found_start_pos = 0; int found_end_pos = 0;
          Expression* found_expression = nullptr; SymbolEntry* found_entry = nullptr;

          size_t* hover_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
          if(analyzer->GetHover(method, line_num, line_pos, found_name, found_line, found_start_pos, found_end_pos, found_expression, found_entry)) {
            if(found_expression) {
              if(found_expression->GetExpressionType() == METHOD_CALL_EXPR) {
                MethodCall* called_method = static_cast<MethodCall*>(found_expression);

                // variable type
                SymbolEntry* called_method_entry = called_method->GetEntry();
                if(called_method_entry && called_method_entry->GetType()) {
                  hover_obj[ResultPosition::POS_TYPE] = called_method_entry->GetType()->GetType();
                  hover_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, called_method_entry->GetType()->GetName());
                }
                else if(called_method->GetVariable() && called_method->GetVariable()->GetEntry() &&
                        called_method->GetVariable()->GetEntry()->GetType()) {
                  called_method_entry = called_method->GetVariable()->GetEntry();
                  hover_obj[ResultPosition::POS_TYPE] = called_method_entry->GetType()->GetType();
                  hover_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, called_method_entry->GetType()->GetName());
                }

                hover_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, called_method->GetMethodName());
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
              hover_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, L"System.$Byte");
              break;

            case frontend::CHAR_TYPE:
              hover_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, L"System.$Char");
              break;

            case frontend::INT_TYPE:
              hover_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, L"System.$Int");
              break;

            case frontend::FLOAT_TYPE:
              hover_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, L"System.$Float");
              break;

            case frontend::BOOLEAN_TYPE:
              hover_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, L"System.$Bool");
              break;

            default:
              hover_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, found_type->GetName());
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

    const std::wstring uri = APITools_GetStringValue(context, 2);

    const int line_num = (int)APITools_GetIntValue(context, 3);
    const int line_pos = (int)APITools_GetIntValue(context, 4);
    (void)line_pos;

    const std::wstring var_str = APITools_GetStringValue(context, 5);
    const std::wstring mthd_str = APITools_GetStringValue(context, 6);
    const std::wstring lib_path = APITools_GetStringValue(context, 7);

    Class* klass; Method* method; SymbolTable* table;
    if(program->FindMethodOrClass(uri, line_num, klass, method, table)) {
      if(method) {
        std::wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }
        ContextAnalyzer analyzer_local(program, full_lib_path, false);
        ContextAnalyzer* analyzer = analyzer_local.Analyze() ? &analyzer_local : nullptr;
        if(analyzer) {
          std::vector<Method*> found_methods; std::vector<LibraryMethod*> found_lib_methods;
          if(analyzer->GetSignature(method, var_str, mthd_str, found_methods, found_lib_methods)) {
            size_t* sig_root_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
            sig_root_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, mthd_str);
            sig_root_obj[ResultPosition::POS_CODE] = found_methods.empty();

            if(!found_methods.empty()) {
              size_t* signature_array = APITools_MakeIntArray(context, (int)found_methods.size());
              size_t* signature_array_array_ptr = signature_array + 3;

              for(size_t i = 0; i < found_methods.size(); ++i) {
                Method* found_method = found_methods[i];

                size_t* mthd_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
                mthd_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, found_method->GetUserName());

                // TODO: params
                std::vector<frontend::Declaration*> declarations = found_method->GetDeclarations()->GetDeclarations();
                size_t* mthd_parm_array = APITools_MakeIntArray(context, (int)declarations.size());
                size_t* mthd_parm_array_ptr = mthd_parm_array + 3;

                for(size_t j = 0; j < declarations.size(); ++j) {
                  std::wstring type_name; GetTypeName(declarations[j]->GetEntry()->GetType(), type_name);
                  size_t* mthd_parm_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
                  mthd_parm_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, type_name);

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
                mthd_lib_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, found_lib_method->GetUserName());

                // TODO: params
                std::vector<frontend::Type*> declarations = found_lib_method->GetDeclarationTypes();
                size_t* mthd_parm_array = APITools_MakeIntArray(context, (int)declarations.size());
                size_t* mthd_parm_array_ptr = mthd_parm_array + 3;

                for(size_t j = 0; j < declarations.size(); ++j) {
                  std::wstring type_name; GetTypeName(declarations[j], type_name);
                  size_t* mthd_parm_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
                  mthd_parm_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, type_name);

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

    const std::wstring uri = APITools_GetStringValue(context, 1);

    const int line_num = (int)APITools_GetIntValue(context, 2);
    const int line_pos = (int)APITools_GetIntValue(context, 3);
    const std::wstring lib_path = APITools_GetStringValue(context, 4);

    prgm_obj[4] = (size_t)GetExpressionsCalls(context, program, uri, line_num, line_pos, lib_path);
  }

  //
  // find references
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_find_references_impl(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 0);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const std::wstring uri = APITools_GetStringValue(context, 1);

    const int line_num = (int)APITools_GetIntValue(context, 2);
    const int line_pos = (int)APITools_GetIntValue(context, 3);
    const std::wstring lib_path = APITools_GetStringValue(context, 4);

    prgm_obj[4] = (size_t)GetExpressionsCalls(context, program, uri, line_num, line_pos, lib_path);
  }

  //
  // find type definition (go to the type of a symbol)
  //
  void diag_find_type_definition_impl(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const std::wstring uri = APITools_GetStringValue(context, 2);
    const int line_num = (int)APITools_GetIntValue(context, 3);
    const int line_pos = (int)APITools_GetIntValue(context, 4);
    const std::wstring lib_path = APITools_GetStringValue(context, 5);

    Class* klass; Method* method; SymbolTable* table;
    if(program->FindMethodOrClass(uri, line_num, klass, method, table)) {
      if(method) {
        std::wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }
        ContextAnalyzer analyzer_local(program, full_lib_path, false);
        ContextAnalyzer* analyzer = analyzer_local.Analyze() ? &analyzer_local : nullptr;
        if(analyzer) {
          std::wstring found_name; int found_line = 0; int found_start_pos = 0; int found_end_pos = 0;
          Expression* found_expression = nullptr; SymbolEntry* found_entry = nullptr;

          if(analyzer->GetHover(method, line_num, line_pos, found_name, found_line, found_start_pos, found_end_pos, found_expression, found_entry)) {
            // resolve type from expression or entry
            Type* resolved_type = nullptr;

            if(found_expression) {
              if(found_expression->GetExpressionType() == METHOD_CALL_EXPR) {
                MethodCall* mc = static_cast<MethodCall*>(found_expression);
                SymbolEntry* mc_entry = mc->GetEntry();
                if(mc_entry && mc_entry->GetType()) {
                  resolved_type = mc_entry->GetType();
                }
                else if(mc->GetVariable() && mc->GetVariable()->GetEntry() && mc->GetVariable()->GetEntry()->GetType()) {
                  resolved_type = mc->GetVariable()->GetEntry()->GetType();
                }
              }
              else if(found_expression->GetExpressionType() == VAR_EXPR) {
                Variable* var = static_cast<Variable*>(found_expression);
                if(var->GetEntry() && var->GetEntry()->GetType()) {
                  resolved_type = var->GetEntry()->GetType();
                }
              }
            }

            if(!resolved_type && found_entry && found_entry->GetType()) {
              resolved_type = found_entry->GetType();
            }

            // if CLASS_TYPE, search for the class definition in program bundles
            if(resolved_type && resolved_type->GetType() == frontend::CLASS_TYPE) {
              Class* type_class = nullptr;
              const std::wstring type_name = resolved_type->GetName();
              std::vector<ParsedBundle*> bundles = program->GetBundles();
              for(size_t bi = 0; !type_class && bi < bundles.size(); ++bi) {
                std::vector<Class*> classes = bundles[bi]->GetClasses();
                for(size_t ci = 0; ci < classes.size(); ++ci) {
                  if(classes[ci]->GetName() == type_name) {
                    type_class = classes[ci];
                    break;
                  }
                }
              }
              if(type_class && type_class->GetLineNumber() > 0) {
                size_t* def_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
                def_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, type_class->GetName());
                def_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, type_class->GetFileName());
                def_obj[ResultPosition::POS_START_LINE] = (size_t)type_class->GetLineNumber() - 1;
                def_obj[ResultPosition::POS_END_LINE] = def_obj[ResultPosition::POS_START_LINE];
                def_obj[ResultPosition::POS_START_POS] = (size_t)type_class->GetLinePosition() - 1;
                def_obj[ResultPosition::POS_END_POS] = (size_t)type_class->GetLinePosition() + 80;
                APITools_SetObjectValue(context, 0, def_obj);
              }
            }
          }
        }
      }
    }
  }

  //
  // find implementations (classes implementing interface, methods overriding virtual)
  //
  void diag_find_implementation_impl(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const std::wstring uri = APITools_GetStringValue(context, 2);
    const int line_num = (int)APITools_GetIntValue(context, 3);
    const int line_pos = (int)APITools_GetIntValue(context, 4);
    const std::wstring lib_path = APITools_GetStringValue(context, 5);

    Class* klass; Method* method; SymbolTable* table;
    if(program->FindMethodOrClass(uri, line_num, klass, method, table)) {
      std::wstring full_lib_path = L"lang.obl";
      if(!lib_path.empty()) {
        full_lib_path += L',' + lib_path;
      }
      ContextAnalyzer analyzer_local(program, full_lib_path, false);
      ContextAnalyzer* analyzer = analyzer_local.Analyze() ? &analyzer_local : nullptr;
      if(!analyzer) return;

      // resolve the symbol at cursor
      std::wstring target_name;
      bool target_is_class = false;
      bool target_is_method = false;
      std::wstring target_method_name;

      if(method) {
        std::wstring found_name; int found_line; int found_start_pos; int found_end_pos;
        Class* found_klass = nullptr; Enum* eenum = nullptr; EnumItem* eenum_item = nullptr;
        if(analyzer->GetDefinition(method, line_num, line_pos, found_name, found_line, found_start_pos, found_end_pos, found_klass, eenum, eenum_item)) {
          if(found_klass) {
            target_name = found_klass->GetName();
            target_is_class = true;
          }
          else {
            // check if cursor is on a method call
            bool is_var;
            std::vector<Expression*> exprs = FetchRenamedExpressions(method, *analyzer, line_num, line_pos, is_var);
            if(!is_var && !exprs.empty() && exprs[0]->GetExpressionType() == METHOD_CALL_EXPR) {
              MethodCall* mc = static_cast<MethodCall*>(exprs[0]);
              Method* resolved = mc->GetMethod();
              if(resolved && resolved->IsVirtual()) {
                target_method_name = resolved->GetName();
                target_is_method = true;
              }
            }
          }
        }
      }
      else {
        // class level - cursor on class name
        Class* found_klass = nullptr;
        if(analyzer->GetDefinition(klass, line_num, line_pos, found_klass)) {
          if(found_klass) {
            target_name = found_klass->GetName();
            target_is_class = true;
          }
        }
        else {
          target_name = klass->GetName();
          target_is_class = true;
        }
      }

      // collect implementations
      std::vector<ParseNode*> results;

      if(target_is_class) {
        // find all classes that implement this interface or extend this class
        std::vector<ParsedBundle*> bundles = program->GetBundles();
        for(size_t i = 0; i < bundles.size(); ++i) {
          std::vector<Class*> classes = bundles[i]->GetClasses();
          for(size_t j = 0; j < classes.size(); ++j) {
            Class* cls = classes[j];
            if(cls->GetName() == target_name) continue;

            // check interfaces
            const std::vector<std::wstring> iface_names = cls->GetInterfaceNames();
            for(size_t k = 0; k < iface_names.size(); ++k) {
              if(iface_names[k] == target_name) {
                results.push_back(cls);
                break;
              }
            }

            // check parent chain
            if(cls->GetParentName() == target_name) {
              if(std::find(results.begin(), results.end(), cls) == results.end()) {
                results.push_back(cls);
              }
            }
          }
        }
      }
      else if(target_is_method) {
        // find all methods that override this virtual method
        std::vector<ParsedBundle*> bundles = program->GetBundles();
        for(size_t i = 0; i < bundles.size(); ++i) {
          std::vector<Class*> classes = bundles[i]->GetClasses();
          for(size_t j = 0; j < classes.size(); ++j) {
            std::vector<Method*> methods = classes[j]->GetMethods();
            for(size_t k = 0; k < methods.size(); ++k) {
              Method* m = methods[k];
              if(m->GetName() == target_method_name && !m->IsVirtual()) {
                results.push_back(m);
              }
            }
          }
        }
      }

      // format results
      if(!results.empty()) {
        size_t* refs_array = APITools_MakeIntArray(context, (int)results.size());
        size_t* refs_array_ptr = refs_array + 3;

        for(size_t i = 0; i < results.size(); ++i) {
          ParseNode* node = results[i];
          size_t* ref_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
          ref_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, node->GetFileName());
          ref_obj[ResultPosition::POS_START_LINE] = (size_t)node->GetLineNumber() - 1;
          ref_obj[ResultPosition::POS_END_LINE] = ref_obj[ResultPosition::POS_START_LINE];
          ref_obj[ResultPosition::POS_START_POS] = (size_t)node->GetLinePosition() - 1;
          ref_obj[ResultPosition::POS_END_POS] = (size_t)node->GetLinePosition() + 80;
          refs_array_ptr[i] = (size_t)ref_obj;
        }

        APITools_SetObjectValue(context, 0, (size_t*)refs_array);
      }
    }
  }

  //
  // type hierarchy: supertypes - walk the parent chain from the class at cursor
  // outward to the root. Backs typeHierarchy/supertypes.
  //
  void diag_type_hierarchy_super_impl(VMContext& context)
  {
    // Argument layout follows the FindReferences / GetSymbols pattern:
    //   slot 0 = @self (the Analysis object), slots 1..4 = call args.
    // Output is written into the Analysis instance variable @supertypes_results
    // (slot 8 of the Analysis object), NOT back into the local arg array.
    // The local-array output pattern (used by InlayHints, OutgoingCalls, etc.)
    // appears unreliable for Result[] casts — those tests have never returned
    // non-Nil data so the cast was never exercised.
    size_t* prgm_obj = APITools_GetObjectValue(context, 0);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const std::wstring uri = APITools_GetStringValue(context, 1);
    const int line_num = (int)APITools_GetIntValue(context, 2);
    // line_pos (slot 3) and lib_path (slot 4) read but unused — chain walking
    // only needs the parsed AST, not the analyzer.

    Class* klass; Method* method; SymbolTable* table;
    if(program->FindMethodOrClass(uri, line_num, klass, method, table)) {
      // resolve the class at cursor
      Class* target = nullptr;
      if(klass && !method) {
        target = klass;
      }
      else if(method) {
        target = method->GetClass();
      }
      if(!target) return;

      // walk parent chain — match by exact name OR short-suffix, since ParsedBundle
      // stores classes under their fully-qualified names ("Default.Shape") while
      // GetParentName can return the short form ("Shape") as written in source.
      std::vector<ParsedBundle*> bundles = program->GetBundles();
      std::vector<Class*> chain;
      std::wstring parent_name = target->GetParentName();

      while(!parent_name.empty()) {
        Class* parent_cls = nullptr;
        const std::wstring dot_suffix = L"." + parent_name;
        for(size_t i = 0; !parent_cls && i < bundles.size(); ++i) {
          std::vector<Class*> classes = bundles[i]->GetClasses();
          for(size_t j = 0; !parent_cls && j < classes.size(); ++j) {
            const std::wstring cls_name = classes[j]->GetName();
            if(cls_name == parent_name ||
               (cls_name.size() >= dot_suffix.size() &&
                cls_name.compare(cls_name.size() - dot_suffix.size(), dot_suffix.size(), dot_suffix) == 0)) {
              parent_cls = classes[j];
            }
          }
        }
        if(!parent_cls) break;
        if(std::find(chain.begin(), chain.end(), parent_cls) != chain.end()) break;
        chain.push_back(parent_cls);
        parent_name = parent_cls->GetParentName();
      }

      if(!chain.empty()) {
        size_t* refs_array = APITools_MakeIntArray(context, (int)chain.size());
        size_t* refs_array_ptr = refs_array + 3;

        for(size_t i = 0; i < chain.size(); ++i) {
          Class* cls = chain[i];
          size_t* ref_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
          ref_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, cls->GetName());
          ref_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, cls->GetFileName());
          ref_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_CLASS;
          ref_obj[ResultPosition::POS_START_LINE] = (size_t)cls->GetLineNumber() - 1;
          ref_obj[ResultPosition::POS_END_LINE] = (size_t)cls->GetEndLineNumber() - 1;
          ref_obj[ResultPosition::POS_START_POS] = (size_t)cls->GetLinePosition() - 1;
          ref_obj[ResultPosition::POS_END_POS] = (size_t)cls->GetLinePosition() + 80;
          refs_array_ptr[i] = (size_t)ref_obj;
        }

        // Write into the Analysis instance variable @supertypes_results (slot 8).
        // This is the proven pattern (mirrors FindReferences/CodeRename) and
        // produces a properly type-tagged Result[] the wrapper can cast.
        prgm_obj[8] = (size_t)refs_array;
      }
    }
  }

  //
  // prepare call hierarchy - identify the method at cursor
  //
  void diag_prepare_call_hierarchy_impl(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const std::wstring uri = APITools_GetStringValue(context, 2);
    const int line_num = (int)APITools_GetIntValue(context, 3);
    const int line_pos = (int)APITools_GetIntValue(context, 4);
    const std::wstring lib_path = APITools_GetStringValue(context, 5);

    Class* klass; Method* method; SymbolTable* table;
    if(program->FindMethodOrClass(uri, line_num, klass, method, table)) {
      if(method) {
        // check if cursor is on a method call
        std::wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }
        ContextAnalyzer analyzer_local(program, full_lib_path, false);
        ContextAnalyzer* analyzer = analyzer_local.Analyze() ? &analyzer_local : nullptr;
        if(analyzer) {
          Method* target_method = method;

          // check if cursor is on a specific method call within the method
          bool is_var;
          std::vector<Expression*> exprs = FetchRenamedExpressions(method, *analyzer, line_num, line_pos, is_var);
          if(!is_var && !exprs.empty() && exprs[0]->GetExpressionType() == METHOD_CALL_EXPR) {
            MethodCall* mc = static_cast<MethodCall*>(exprs[0]);
            if(mc->GetMethod()) {
              target_method = mc->GetMethod();
            }
          }

          // return method info
          size_t* result_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");

          const std::wstring method_name = target_method->GetName();
          const size_t colon_idx = method_name.find(L':');
          std::wstring short_name = colon_idx != std::wstring::npos ? method_name.substr(colon_idx + 1) : method_name;

          result_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, short_name);
          result_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, target_method->GetFileName());
          result_obj[ResultPosition::POS_START_LINE] = (size_t)target_method->GetLineNumber() - 1;
          result_obj[ResultPosition::POS_END_LINE] = (size_t)target_method->GetEndLineNumber() - 1;
          result_obj[ResultPosition::POS_START_POS] = (size_t)target_method->GetLinePosition() - 1;
          result_obj[ResultPosition::POS_END_POS] = (size_t)target_method->GetLinePosition() + 80;

          // store class name in POS_CODE as a secondary identifier
          if(target_method->GetClass()) {
            result_obj[ResultPosition::POS_TYPE] = (size_t)APITools_CreateStringObject(context, target_method->GetClass()->GetName());
          }

          APITools_SetObjectValue(context, 0, result_obj);
        }
      }
    }
  }

  //
  // incoming calls - find all methods that call the target method
  //
  void diag_incoming_calls_impl(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const std::wstring uri = APITools_GetStringValue(context, 2);
    const int line_num = (int)APITools_GetIntValue(context, 3);
    const int line_pos = (int)APITools_GetIntValue(context, 4);
    const std::wstring lib_path = APITools_GetStringValue(context, 5);

    Class* klass; Method* method; SymbolTable* table;
    if(program->FindMethodOrClass(uri, line_num, klass, method, table)) {
      if(method) {
        std::wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }
        ContextAnalyzer analyzer_local(program, full_lib_path, false);
        ContextAnalyzer* analyzer = analyzer_local.Analyze() ? &analyzer_local : nullptr;
        if(!analyzer) return;

        // resolve target method (may be a call under cursor)
        Method* target_method = method;
        bool is_var;
        std::vector<Expression*> exprs = FetchRenamedExpressions(method, *analyzer, line_num, line_pos, is_var);
        if(!is_var && !exprs.empty() && exprs[0]->GetExpressionType() == METHOD_CALL_EXPR) {
          MethodCall* mc = static_cast<MethodCall*>(exprs[0]);
          if(mc->GetMethod()) {
            target_method = mc->GetMethod();
          }
        }

        // find all callers
        std::vector<Method*> callers;
        std::vector<int> caller_lines;
        std::vector<int> caller_positions;

        std::vector<ParsedBundle*> bundles = program->GetBundles();
        for(size_t i = 0; i < bundles.size(); ++i) {
          std::vector<Class*> classes = bundles[i]->GetClasses();
          for(size_t j = 0; j < classes.size(); ++j) {
            std::vector<Method*> methods = classes[j]->GetMethods();
            for(size_t k = 0; k < methods.size(); ++k) {
              Method* m = methods[k];
              std::vector<Expression*> method_expressions = m->GetExpressions();
              for(size_t l = 0; l < method_expressions.size(); ++l) {
                if(method_expressions[l]->GetExpressionType() == METHOD_CALL_EXPR) {
                  MethodCall* mc = static_cast<MethodCall*>(method_expressions[l]);
                  if(mc->GetMethod() == target_method) {
                    callers.push_back(m);
                    int call_line = static_cast<Expression*>(mc)->GetLineNumber();
                    int call_pos = mc->GetMidLinePosition() > 0 ? mc->GetMidLinePosition() : static_cast<Expression*>(mc)->GetLinePosition();
                    caller_lines.push_back(call_line);
                    caller_positions.push_back(call_pos);
                    break;
                  }
                }
              }
            }
          }
        }

        if(!callers.empty()) {
          size_t* refs_array = APITools_MakeIntArray(context, (int)callers.size());
          size_t* refs_array_ptr = refs_array + 3;

          for(size_t i = 0; i < callers.size(); ++i) {
            Method* caller = callers[i];
            size_t* ref_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");

            const std::wstring caller_name = caller->GetName();
            const size_t colon_idx = caller_name.find(L':');
            std::wstring short_name = colon_idx != std::wstring::npos ? caller_name.substr(colon_idx + 1) : caller_name;

            ref_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, short_name);
            ref_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, caller->GetFileName());
            ref_obj[ResultPosition::POS_START_LINE] = (size_t)caller->GetLineNumber() - 1;
            ref_obj[ResultPosition::POS_END_LINE] = (size_t)caller->GetEndLineNumber() - 1;
            ref_obj[ResultPosition::POS_START_POS] = (size_t)caller->GetLinePosition() - 1;
            ref_obj[ResultPosition::POS_END_POS] = (size_t)caller->GetLinePosition() + 80;
            refs_array_ptr[i] = (size_t)ref_obj;
          }

          APITools_SetObjectValue(context, 0, (size_t*)refs_array);
        }
      }
    }
  }

  //
  // outgoing calls - find all methods called by the target method
  //
  void diag_outgoing_calls_impl(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const std::wstring uri = APITools_GetStringValue(context, 2);
    const int line_num = (int)APITools_GetIntValue(context, 3);
    const int line_pos = (int)APITools_GetIntValue(context, 4);
    const std::wstring lib_path = APITools_GetStringValue(context, 5);

    Class* klass; Method* method; SymbolTable* table;
    if(program->FindMethodOrClass(uri, line_num, klass, method, table)) {
      if(method) {
        std::wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }
        ContextAnalyzer analyzer_local(program, full_lib_path, false);
        ContextAnalyzer* analyzer = analyzer_local.Analyze() ? &analyzer_local : nullptr;
        if(!analyzer) return;

        // resolve target method
        Method* target_method = method;
        bool is_var;
        std::vector<Expression*> exprs = FetchRenamedExpressions(method, *analyzer, line_num, line_pos, is_var);
        if(!is_var && !exprs.empty() && exprs[0]->GetExpressionType() == METHOD_CALL_EXPR) {
          MethodCall* mc = static_cast<MethodCall*>(exprs[0]);
          if(mc->GetMethod()) {
            target_method = mc->GetMethod();
          }
        }

        // collect outgoing calls (unique methods only)
        std::vector<Method*> called_methods;
        std::vector<int> call_lines;
        std::vector<int> call_positions;

        std::vector<Expression*> method_expressions = target_method->GetExpressions();
        for(size_t i = 0; i < method_expressions.size(); ++i) {
          if(method_expressions[i]->GetExpressionType() == METHOD_CALL_EXPR) {
            MethodCall* mc = static_cast<MethodCall*>(method_expressions[i]);
            Method* called = mc->GetMethod();
            if(called && called->GetLineNumber() > 0) {
              if(std::find(called_methods.begin(), called_methods.end(), called) == called_methods.end()) {
                called_methods.push_back(called);
                int cl = static_cast<Expression*>(mc)->GetLineNumber();
                int cp = mc->GetMidLinePosition() > 0 ? mc->GetMidLinePosition() : static_cast<Expression*>(mc)->GetLinePosition();
                call_lines.push_back(cl);
                call_positions.push_back(cp);
              }
            }
          }
        }

        if(!called_methods.empty()) {
          size_t* refs_array = APITools_MakeIntArray(context, (int)called_methods.size());
          size_t* refs_array_ptr = refs_array + 3;

          for(size_t i = 0; i < called_methods.size(); ++i) {
            Method* called = called_methods[i];
            size_t* ref_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");

            const std::wstring called_name = called->GetName();
            const size_t colon_idx = called_name.find(L':');
            std::wstring short_name = colon_idx != std::wstring::npos ? called_name.substr(colon_idx + 1) : called_name;

            ref_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, short_name);
            ref_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, called->GetFileName());
            ref_obj[ResultPosition::POS_START_LINE] = (size_t)called->GetLineNumber() - 1;
            ref_obj[ResultPosition::POS_END_LINE] = (size_t)called->GetEndLineNumber() - 1;
            ref_obj[ResultPosition::POS_START_POS] = (size_t)called->GetLinePosition() - 1;
            ref_obj[ResultPosition::POS_END_POS] = (size_t)called->GetLinePosition() + 80;
            refs_array_ptr[i] = (size_t)ref_obj;
          }

          APITools_SetObjectValue(context, 0, (size_t*)refs_array);
        }
      }
    }
  }

  //
  // inlay hints - parameter names at call sites + type hints for inferred locals
  //
  void diag_inlay_hints_impl(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const std::wstring uri = APITools_GetStringValue(context, 2);
    const int start_line = (int)APITools_GetIntValue(context, 3);
    const int end_line = (int)APITools_GetIntValue(context, 4);
    const std::wstring lib_path = APITools_GetStringValue(context, 5);

    std::wstring full_lib_path = L"lang.obl";
    if(!lib_path.empty()) {
      full_lib_path += L',' + lib_path;
    }
    ContextAnalyzer analyzer_local(program, full_lib_path, false);
    ContextAnalyzer* analyzer = analyzer_local.Analyze() ? &analyzer_local : nullptr;
    if(!analyzer) return;

    struct HintInfo {
      std::wstring text;
      int line;
      int pos;
      int kind; // 1=type, 2=parameter
    };
    std::vector<HintInfo> hints;

    // iterate all methods in all classes looking for those in the visible range
    std::vector<ParsedBundle*> bundles = program->GetBundles();
    for(size_t i = 0; i < bundles.size(); ++i) {
      std::vector<Class*> classes = bundles[i]->GetClasses();
      for(size_t j = 0; j < classes.size(); ++j) {
        Class* cls = classes[j];
        if(cls->GetFileName() != uri) continue;

        std::vector<Method*> methods = cls->GetMethods();
        for(size_t k = 0; k < methods.size(); ++k) {
          Method* m = methods[k];
          int mthd_start = m->GetLineNumber() - 1;
          int mthd_end = m->GetEndLineNumber() - 1;

          // skip methods outside the visible range
          if(mthd_end < start_line || mthd_start > end_line) continue;

          std::vector<Expression*> expressions = m->GetExpressions();
          for(size_t l = 0; l < expressions.size(); ++l) {
            Expression* expr = expressions[l];
            int expr_line = expr->GetLineNumber() - 1;
            if(expr_line < start_line || expr_line > end_line) continue;

            // parameter name hints at method call sites
            if(expr->GetExpressionType() == METHOD_CALL_EXPR) {
              MethodCall* mc = static_cast<MethodCall*>(expr);
              Method* called = mc->GetMethod();
              ExpressionList* call_params = mc->GetCallingParameters();

              if(called && call_params) {
                DeclarationList* decls = called->GetDeclarations();
                if(decls) {
                  std::vector<Declaration*> decl_list = decls->GetDeclarations();
                  std::vector<Expression*> param_exprs = call_params->GetExpressions();

                  for(size_t p = 0; p < param_exprs.size() && p < decl_list.size(); ++p) {
                    if(param_exprs[p] && decl_list[p] && decl_list[p]->GetEntry()) {
                      std::wstring param_name = decl_list[p]->GetEntry()->GetName();
                      // extract short name (after method qualifier)
                      size_t last_colon = param_name.rfind(L':');
                      if(last_colon != std::wstring::npos) {
                        param_name = param_name.substr(last_colon + 1);
                      }

                      if(!param_name.empty() && param_exprs[p]->GetLineNumber() > 0) {
                        HintInfo hint;
                        hint.text = param_name + L":";
                        hint.line = param_exprs[p]->GetLineNumber() - 1;
                        hint.pos = param_exprs[p]->GetLinePosition() - 1;
                        hint.kind = 2; // parameter
                        hints.push_back(hint);
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }

    if(!hints.empty()) {
      size_t* refs_array = APITools_MakeIntArray(context, (int)hints.size());
      size_t* refs_array_ptr = refs_array + 3;

      for(size_t i = 0; i < hints.size(); ++i) {
        size_t* hint_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
        hint_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, hints[i].text);
        hint_obj[ResultPosition::POS_TYPE] = (size_t)hints[i].kind;
        hint_obj[ResultPosition::POS_START_LINE] = (size_t)hints[i].line;
        hint_obj[ResultPosition::POS_START_POS] = (size_t)hints[i].pos;
        hint_obj[ResultPosition::POS_END_LINE] = (size_t)hints[i].line;
        hint_obj[ResultPosition::POS_END_POS] = (size_t)hints[i].pos;
        refs_array_ptr[i] = (size_t)hint_obj;
      }

      APITools_SetObjectValue(context, 0, (size_t*)refs_array);
    }
  }

  //
  // semantic tokens - classify all expressions by type
  //
  void diag_semantic_tokens_impl(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const std::wstring uri = APITools_GetStringValue(context, 2);
    const std::wstring lib_path = APITools_GetStringValue(context, 3);

    std::wstring full_lib_path = L"lang.obl";
    if(!lib_path.empty()) {
      full_lib_path += L',' + lib_path;
    }
    ContextAnalyzer analyzer_local(program, full_lib_path, false);
    ContextAnalyzer* analyzer = analyzer_local.Analyze() ? &analyzer_local : nullptr;
    if(!analyzer) return;

    // LSP semantic token types:
    // 0=namespace, 1=type, 2=class, 3=enum, 4=interface,
    // 5=function, 6=method, 7=property, 8=variable, 9=parameter, 10=enumMember
    // Modifiers: 1=declaration, 2=static, 4=readonly

    struct TokenInfo {
      int line;
      int start_char;
      int length;
      int token_type;
      int token_modifiers;
    };
    std::vector<TokenInfo> tokens;

    std::vector<ParsedBundle*> bundles = program->GetBundles();
    for(size_t i = 0; i < bundles.size(); ++i) {
      std::vector<Class*> classes = bundles[i]->GetClasses();
      for(size_t j = 0; j < classes.size(); ++j) {
        Class* cls = classes[j];
        if(cls->GetFileName() != uri) continue;

        // class/interface declaration token
        if(cls->GetLineNumber() > 0) {
          TokenInfo t;
          t.line = cls->GetLineNumber() - 1;
          t.start_char = cls->GetLinePosition() - 1;

          // extract short class name (after last '.')
          std::wstring cls_name = cls->GetName();
          size_t dot_pos = cls_name.rfind(L'.');
          std::wstring short_name = dot_pos != std::wstring::npos ? cls_name.substr(dot_pos + 1) : cls_name;
          t.length = (int)short_name.size();
          t.token_type = cls->IsInterface() ? 4 : 2; // interface or class
          t.token_modifiers = 1; // declaration
          tokens.push_back(t);
        }

        // method expressions
        std::vector<Method*> methods = cls->GetMethods();
        for(size_t k = 0; k < methods.size(); ++k) {
          Method* m = methods[k];

          // method declaration itself
          if(m->GetMidLineNumber() > 0) {
            TokenInfo t;
            t.line = m->GetMidLineNumber() - 1;
            t.start_char = m->GetMidLinePosition() - 1;

            const std::wstring mname = m->GetName();
            const size_t colon_idx = mname.find(L':');
            std::wstring short_name = colon_idx != std::wstring::npos ? mname.substr(colon_idx + 1) : mname;
            t.length = (int)short_name.size();

            t.token_type = m->IsStatic() ? 5 : 6; // function or method
            t.token_modifiers = 1; // declaration
            if(m->IsStatic()) t.token_modifiers |= 2; // static
            tokens.push_back(t);
          }

          // expressions within method
          std::vector<Expression*> expressions = m->GetExpressions();
          for(size_t l = 0; l < expressions.size(); ++l) {
            Expression* expr = expressions[l];
            if(expr->GetLineNumber() <= 0 || expr->GetLinePosition() <= 0) continue;

            if(expr->GetExpressionType() == VAR_EXPR) {
              Variable* var = static_cast<Variable*>(expr);
              TokenInfo t;
              t.line = expr->GetLineNumber() - 1;
              t.start_char = expr->GetLinePosition() - 1;
              t.length = (int)var->GetName().size();
              t.token_modifiers = 0;

              const std::wstring& vname = var->GetName();
              if(!vname.empty() && vname[0] == L'@') {
                t.token_type = 7; // property (instance/class var)
                SymbolEntry* entry = var->GetEntry();
                if(entry && entry->IsStatic()) {
                  t.token_modifiers |= 2; // static
                }
              }
              else {
                SymbolEntry* entry = var->GetEntry();
                if(entry && entry->IsParameter()) {
                  t.token_type = 9; // parameter
                }
                else {
                  t.token_type = 8; // variable
                }
              }

              tokens.push_back(t);
            }
            else if(expr->GetExpressionType() == METHOD_CALL_EXPR) {
              MethodCall* mc = static_cast<MethodCall*>(expr);
              if(mc->GetCallType() == ENUM_CALL) continue;

              int mid_pos = mc->GetMidLinePosition();
              if(mid_pos <= 0) mid_pos = expr->GetLinePosition();

              TokenInfo t;
              t.line = expr->GetLineNumber() - 1;
              t.start_char = mid_pos - 1;
              t.length = (int)mc->GetMethodName().size();
              t.token_modifiers = 0;

              Method* called = mc->GetMethod();
              if(called && called->IsStatic()) {
                t.token_type = 5; // function (static)
                t.token_modifiers |= 2;
              }
              else {
                t.token_type = 6; // method
              }

              tokens.push_back(t);
            }
          }
        }
      }

      // enums
      std::vector<Enum*> enums = bundles[i]->GetEnums();
      for(size_t j = 0; j < enums.size(); ++j) {
        Enum* en = enums[j];
        if(en->GetFileName() != uri || en->GetLineNumber() <= 0) continue;

        TokenInfo t;
        t.line = en->GetLineNumber() - 1;
        t.start_char = en->GetLinePosition() - 1;

        std::wstring ename = en->GetName();
        size_t dot_pos = ename.rfind(L'.');
        std::wstring short_name = dot_pos != std::wstring::npos ? ename.substr(dot_pos + 1) : ename;
        t.length = (int)short_name.size();
        t.token_type = 3; // enum
        t.token_modifiers = 1; // declaration
        tokens.push_back(t);
      }
    }

    // sort tokens by line then position
    std::sort(tokens.begin(), tokens.end(), [](const TokenInfo& a, const TokenInfo& b) {
      return a.line < b.line || (a.line == b.line && a.start_char < b.start_char);
    });

    // encode as LSP delta format
    if(!tokens.empty()) {
      size_t* refs_array = APITools_MakeIntArray(context, (int)tokens.size());
      size_t* refs_array_ptr = refs_array + 3;

      for(size_t i = 0; i < tokens.size(); ++i) {
        size_t* tok_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
        tok_obj[ResultPosition::POS_START_LINE] = (size_t)tokens[i].line;
        tok_obj[ResultPosition::POS_START_POS] = (size_t)tokens[i].start_char;
        tok_obj[ResultPosition::POS_END_LINE] = (size_t)tokens[i].length;
        tok_obj[ResultPosition::POS_END_POS] = (size_t)tokens[i].token_type;
        tok_obj[ResultPosition::POS_TYPE] = (size_t)tokens[i].token_modifiers;
        refs_array_ptr[i] = (size_t)tok_obj;
      }

      APITools_SetObjectValue(context, 0, (size_t*)refs_array);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_hover(VMContext& context) { SafeCallDiag(diag_hover_impl, context); }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_code_action(VMContext& context) { SafeCallDiag(diag_code_action_impl, context); }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_find_definition(VMContext& context) { SafeCallDiag(diag_find_definition_impl, context); }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_find_declaration(VMContext& context) { SafeCallDiag(diag_find_declaration_impl, context); }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_find_references(VMContext& context) { SafeCallDiag(diag_find_references_impl, context); }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_completion_help(VMContext& context) { SafeCallDiag(diag_completion_help_impl, context); }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_get_symbols(VMContext& context) { SafeCallDiag(diag_get_symbols_impl, context); }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_get_diagnosis(VMContext& context) { SafeCallDiag(diag_get_diagnosis_impl, context); }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_find_type_definition(VMContext& context) { SafeCallDiag(diag_find_type_definition_impl, context); }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_find_implementation(VMContext& context) { SafeCallDiag(diag_find_implementation_impl, context); }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_type_hierarchy_super(VMContext& context) { SafeCallDiag(diag_type_hierarchy_super_impl, context); }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_prepare_call_hierarchy(VMContext& context) { SafeCallDiag(diag_prepare_call_hierarchy_impl, context); }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_incoming_calls(VMContext& context) { SafeCallDiag(diag_incoming_calls_impl, context); }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_outgoing_calls(VMContext& context) { SafeCallDiag(diag_outgoing_calls_impl, context); }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_inlay_hints(VMContext& context) { SafeCallDiag(diag_inlay_hints_impl, context); }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void diag_semantic_tokens(VMContext& context) { SafeCallDiag(diag_semantic_tokens_impl, context); }

}

//
// Supporting functions
//

size_t* FormatErrors(VMContext& context, const std::vector<std::wstring>& error_strings, const std::vector<std::wstring>& warning_strings)
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
    const std::wstring error_string = error_strings[count];

    // parse error string
    const size_t file_mid = error_string.find(L":(");
    const std::wstring file_str = error_string.substr(0, file_mid);

    const size_t msg_mid = error_string.find(L"):");
    const std::wstring msg_str = error_string.substr(msg_mid + 3, error_string.size() - msg_mid - 3);

    const std::wstring line_pos_str = error_string.substr(file_mid + 2, msg_mid - file_mid - 2);
    const size_t line_pos_mid = line_pos_str.find(L',');
    const std::wstring line_str = line_pos_str.substr(0, line_pos_mid);
    const std::wstring pos_str = line_pos_str.substr(line_pos_mid + 1, line_pos_str.size() - line_pos_mid - 1);

    wchar_t* end;
    const int line_index = (int)wcstol(line_str.c_str(), &end, 10);
    const int pos_index = (int)wcstol(pos_str.c_str(), &end, 10);

    // create objects
    size_t* diag_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
    diag_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, msg_str);
    diag_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_ERROR; // error type
    diag_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, file_str);
    diag_obj[ResultPosition::POS_START_LINE] = (size_t)line_index;
    diag_obj[ResultPosition::POS_START_POS] = (size_t)pos_index;
    diag_obj[ResultPosition::POS_END_LINE] = diag_obj[ResultPosition::POS_END_POS] = -1;
    diagnostics_array_ptr[count] = (size_t)diag_obj;
  }

  // process warnings
  for(size_t i = 0; i < warning_strings.size() && count < max_results; ++i, ++count) {
    const std::wstring warning_string = warning_strings[i];

    // parse warning string
    const size_t file_mid = warning_string.find(L":(");
    const std::wstring file_str = warning_string.substr(0, file_mid);

    const size_t msg_mid = warning_string.find(L"):");
    const std::wstring msg_str = warning_string.substr(msg_mid + 3, warning_string.size() - msg_mid - 3);

    const std::wstring line_pos_str = warning_string.substr(file_mid + 2, msg_mid - file_mid - 2);
    const size_t line_pos_mid = line_pos_str.find(L',');
    const std::wstring line_str = line_pos_str.substr(0, line_pos_mid);
    const std::wstring pos_str = line_pos_str.substr(line_pos_mid + 1, line_pos_str.size() - line_pos_mid - 1);

    wchar_t* end;
    const int line_index = (int)wcstol(line_str.c_str(), &end, 10);
    const int pos_index = (int)wcstol(pos_str.c_str(), &end, 10);

    // create objects
    size_t* diag_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
    diag_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, msg_str);
    diag_obj[ResultPosition::POS_TYPE] = ResultType::TYPE_WARN; // warning type
    diag_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, file_str);
    diag_obj[ResultPosition::POS_START_LINE] = (size_t)line_index;
    diag_obj[ResultPosition::POS_START_POS] = (size_t)pos_index;
    diag_obj[ResultPosition::POS_END_LINE] = diag_obj[ResultPosition::POS_END_POS] = -1;
    diagnostics_array_ptr[count] = (size_t)diag_obj;
  }

  return diagnostics_array;
}

size_t* GetExpressionsCalls(VMContext& context, frontend::ParsedProgram* program, const std::wstring uri, const int line_num, const int line_pos, const std::wstring lib_path)
{
  Class* klass = nullptr;
  Method* method = nullptr;
  SymbolTable* table = nullptr;

  if(program->FindMethodOrClass(uri, line_num, klass, method, table)) {
    // within a method
    if(method) {
      std::wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }
        ContextAnalyzer analyzer_local(program, full_lib_path, false);
        ContextAnalyzer* analyzer = analyzer_local.Analyze() ? &analyzer_local : nullptr;
        if(analyzer) {
        // fetch renamed expressions
        bool is_var;
        std::vector<Expression*> expressions = FetchRenamedExpressions(method, *analyzer, line_num, line_pos, is_var);

        // method/function
        if(!is_var && !expressions.empty() && expressions[0]->GetExpressionType() == METHOD_CALL_EXPR) {
          MethodCall* method_call = static_cast<MethodCall*>(expressions[0]);
          Method* local_method = method_call->GetMethod();
          expressions.clear();

          if(local_method) {
            std::vector<ParsedBundle*> bundles = program->GetBundles();
            for(size_t i = 0; i < bundles.size(); ++i) {
              std::vector<Class*> classes = bundles[i]->GetClasses();
              for(size_t j = 0; j < classes.size(); ++j) {
                std::vector<Method*> methods = classes[j]->GetMethods();
                for(size_t k = 0; k < methods.size(); ++k) {
                  // TODO: all method calls (statements and expressions)
                  std::vector<Expression*> method_expressions = methods[k]->GetExpressions();
                  for(size_t l = 0; l < method_expressions.size(); ++l) {
                    if(method_expressions[l]->GetExpressionType() == METHOD_CALL_EXPR) {
                      MethodCall* local_method_call = static_cast<MethodCall*>(method_expressions[l]);
                      if(local_method_call->GetMethod() == local_method) {
                        expressions.push_back(local_method_call);
                      }
                    }
                  }
                }
              }
            }

            if(expressions.empty()) {
              expressions.push_back(method_call);
            }
          }
        }

        // Drop any expressions that don't have a valid source position;
        // these come from synthetic symbol-table entries (e.g. inferred-
        // type locals) and would otherwise produce -1/-2 line numbers in
        // the result, which the editor renders as a bogus jump target.
        {
          std::vector<Expression*> filtered;
          filtered.reserve(expressions.size());
          for(Expression* e : expressions) {
            if(e != nullptr && e->GetLineNumber() > 0 && e->GetLinePosition() > 0) {
              filtered.push_back(e);
            }
          }
          expressions.swap(filtered);
        }

        // format
        if(!expressions.empty()) {
          Method* mthd_dclr = nullptr;
          size_t* refs_array = nullptr;

          const bool skip_expr = expressions.size() == 1 && 
            expressions[0]->GetExpressionType() == METHOD_CALL_EXPR && 
            static_cast<MethodCall*>(expressions[0])->GetVariableName() == L"#";
          if(skip_expr) {
            mthd_dclr = static_cast<MethodCall*>(expressions[0])->GetMethod();
            refs_array = APITools_MakeIntArray(context, (int)expressions.size());
          }
          else if(is_var) {
              refs_array = APITools_MakeIntArray(context, (int)expressions.size());
          }
          else {
            refs_array = APITools_MakeIntArray(context, (int)expressions.size() + 1);
          }
          size_t* refs_array_ptr = refs_array + 3;

          for(size_t i = 0; !skip_expr && i < expressions.size(); ++i) {
            Expression* expression = expressions[i];

            size_t* reference_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
            int start_pos = expression->GetLinePosition();
            int end_pos = start_pos;

            switch(expression->GetExpressionType()) {
            case VAR_EXPR: {
              Variable* variable = static_cast<Variable*>(expression);
              const std::wstring variable_name = variable->GetName();
              end_pos += (int)variable_name.size();

              reference_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, variable->GetName());
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
              reference_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, method_call->GetMethodName());
            }
              break;

            default:
              break;
            }

            reference_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, expression->GetFileName());
            reference_obj[ResultPosition::POS_START_LINE] = reference_obj[ResultPosition::POS_END_LINE] = (size_t)expression->GetLineNumber() - 1;
            reference_obj[ResultPosition::POS_START_POS] = (size_t)start_pos - 1;
            reference_obj[ResultPosition::POS_END_POS] = (size_t)end_pos - 1;
            refs_array_ptr[i] = (size_t)reference_obj;
          }

          // update declaration name (guard: mthd_dclr can be null when
          // skip_expr came from a synthetic call whose GetMethod() == nullptr)
          if(mthd_dclr && (skip_expr || !is_var)) {
            size_t* reference_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");

            int start_pos = mthd_dclr->GetMidLinePosition();

            const std::wstring mthd_dclr_long_name = mthd_dclr->GetName();
            const size_t mthd_dclr_index = mthd_dclr_long_name.find(L':');
            if(mthd_dclr_index != std::wstring::npos) {
              const std::wstring mthd_dclr_name = mthd_dclr_long_name.substr(mthd_dclr_index + 1);
              int end_pos = start_pos + (int)mthd_dclr_name.size();

              reference_obj[ResultPosition::POS_TYPE] = 200;
              reference_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, mthd_dclr->GetName());
              reference_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, mthd_dclr->GetFileName());
              reference_obj[ResultPosition::POS_START_LINE] = reference_obj[ResultPosition::POS_END_LINE] = (size_t)mthd_dclr->GetLineNumber() - 1;
              reference_obj[ResultPosition::POS_START_POS] = (size_t)start_pos - 1;
              reference_obj[ResultPosition::POS_END_POS] = (size_t)end_pos - 1;

              if(skip_expr) {
                refs_array_ptr[0] = (size_t)reference_obj;
              }
              else {
                refs_array_ptr[(int)expressions.size()] = (size_t)reference_obj;
              }
            }
          }

          return refs_array;
        }
      }
    }
    // within a class
    else {
      std::wstring full_lib_path = L"lang.obl";
        if(!lib_path.empty()) {
          full_lib_path += L',' + lib_path;
        }
        ContextAnalyzer analyzer_local(program, full_lib_path, false);
        ContextAnalyzer* analyzer = analyzer_local.Analyze() ? &analyzer_local : nullptr;
        if(analyzer) {
        // fetch renamed expressions
        std::vector<Expression*> expressions = FetchRenamedExpressions(klass, *analyzer, line_num, line_pos);
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
              const std::wstring variable_name = variable->GetName();
              end_pos += (int)variable_name.size();

              reference_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, variable->GetName());
              reference_obj[ResultPosition::POS_TYPE] = 100;
            }
              break;

            case METHOD_CALL_EXPR: {
              MethodCall* method_call = static_cast<MethodCall*>(expression);
              end_pos += (int)method_call->GetVariableName().size();
              reference_obj[ResultPosition::POS_TYPE] = 200;
              reference_obj[ResultPosition::POS_NAME] = (size_t)APITools_CreateStringObject(context, method_call->GetMethodName());
            }
              break;

            default:
              break;
            }

            reference_obj[ResultPosition::POS_DESC] = (size_t)APITools_CreateStringObject(context, expression->GetFileName());
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

void GetTypeName(frontend::Type* type, std::wstring& output)
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

std::vector<frontend::Expression*> FetchRenamedExpressions(frontend::Class* klass, class ContextAnalyzer& analyzer, const int line_num, const int line_pos)
{
  std::vector<Method*> methods = klass->GetMethods();
  if(!methods.empty()) {
    bool is_var;
    return FetchRenamedExpressions(methods[0], analyzer, line_num, line_pos, is_var);
  }

  return std::vector<Expression*>();
}

std::vector<frontend::Expression*> FetchRenamedExpressions(frontend::Method* method, class ContextAnalyzer& analyzer, const int line_num, const int line_pos, bool &is_var)
{
  bool is_cls;
  std::vector<Expression*> expressions = analyzer.FindExpressions(method, line_num, line_pos, is_var, is_cls);

  if(is_cls && !expressions.empty()) {
    std::wstring found_name;
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
    std::vector<Method*> methods = method->GetClass()->GetMethods();
    for(size_t i = 0; i < methods.size(); ++i) {
      std::vector<Expression*> method_expressions = methods[i]->GetExpressions();
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

size_t HasUserUses(frontend::ParsedProgram* program)
{
  std::vector<std::wstring> use_names = program->GetLibUses();
  for(auto& use_name : use_names) {
    if(use_name.rfind(L"System", 0) != std::wstring::npos) {
      return 1;
    }
  }

  return 0;
}
