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

#ifdef _DEBUG
    wcout << L"Parse file: src_file='" << src_file << L"'" << endl;
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

#ifdef _DEBUG
    wcout << L"Parse file: text_size=" << src_text.size() << L"'" << endl;
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
  void diag_get_diagnosis(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const wstring sys_path = APITools_GetStringValue(context, 2);

    wstring full_path = L"lang.obl";
    if(!sys_path.empty()) {
      full_path += L',' + sys_path;
    }

    // if parsed
    if(!prgm_obj[1]) {
      vector<wstring> error_strings = program->GetErrorStrings();
      size_t* diagnostics_array = FormatErrors(context, error_strings);

      // diagnostics
      prgm_obj[3] = (size_t)diagnostics_array;
    }
    else {
      ContextAnalyzer analyzer(program, full_path, false, false);
      if(!analyzer.Analyze()) {
        vector<wstring> error_strings = program->GetErrorStrings();
        size_t* diagnostics_array = FormatErrors(context, error_strings);

        // diagnostics
        prgm_obj[3] = (size_t)diagnostics_array;
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
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const vector<ParsedBundle*> bundles = program->GetBundles();
    size_t* bundle_array = APITools_MakeIntArray(context, (int)bundles.size());
    size_t* bundle_array_ptr = bundle_array + 3;
    
    // bundles
    wstring file_name;
    for(size_t i = 0; i < bundles.size(); ++i) {
      ParsedBundle* bundle = bundles[i];
      file_name = bundle->GetFileName();

      // bundle
      size_t* bundle_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
      const wstring bundle_name = bundle->GetName();
      bundle_symb_obj[0] = (size_t)APITools_CreateStringValue(context, bundle_name.empty() ? L"Default" : bundle_name);
      bundle_symb_obj[1] = DIAG_NAMESPACE; // namespace type
      bundle_symb_obj[4] = bundle->GetLineNumber();
      bundle_symb_obj[5] = bundle->GetLinePosition();
      bundle_symb_obj[6] = bundle->GetEndLineNumber();
      bundle_symb_obj[7] = bundle->GetEndLinePosition();
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
        klass_symb_obj[0] = (size_t)APITools_CreateStringValue(context, klass->GetName());
        klass_symb_obj[1] = DIAG_CLASS; // class type
        klass_symb_obj[4] = klass->GetLineNumber();
        klass_symb_obj[5] = klass->GetLinePosition();
        klass_symb_obj[6] = klass->GetEndLineNumber();
        klass_symb_obj[7] = klass->GetEndLinePosition();
        klass_array_ptr[index++] = (size_t)klass_symb_obj;

        // methods
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

          mthd_symb_obj[1] = DIAG_METHOD; // method type
          mthd_symb_obj[4] = mthd->GetLineNumber();
          mthd_symb_obj[5] = mthd->GetLinePosition();
          mthd_symb_obj[6] = mthd->GetEndLineNumber();
          mthd_symb_obj[7] = mthd->GetEndLinePosition();
          mthds_array_ptr[k] = (size_t)mthd_symb_obj;
        }
        klass_symb_obj[2] = (size_t)mthds_array;
      }

      // enums
      for(size_t j = 0; j < eenums.size(); ++j) {
        Enum* eenum = eenums[j];

        wstring eenum_short_name;
        const wstring eenum_long_name = eenum->GetName();
        size_t eenum_long_name_index = eenum_long_name.find_last_of(L'#');
        if(eenum_long_name_index != wstring::npos) {
          eenum_short_name = eenum_long_name.substr(eenum_long_name_index + 1, eenum_long_name.size() - eenum_long_name_index - 1);
        }
        else {
          eenum_short_name = eenum->GetName();
        }

        size_t* eenum_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
        eenum_symb_obj[0] = (size_t)APITools_CreateStringValue(context, eenum_short_name);
        eenum_symb_obj[1] = DIAG_ENUM; // enum type
        eenum_symb_obj[4] = eenum->GetLineNumber();
        eenum_symb_obj[5] = eenum->GetLinePosition();
        eenum_symb_obj[6] = eenum->GetEndLineNumber();
        eenum_symb_obj[7] = eenum->GetEndLinePosition();
        klass_array_ptr[index++] = (size_t)eenum_symb_obj;
      }

      bundle_symb_obj[2] = (size_t)klass_array;
    }

    // file root
    size_t* file_symb_obj = APITools_CreateObject(context, L"System.Diagnostics.Result");
    file_symb_obj[0] = (size_t)APITools_CreateStringValue(context, file_name);
    file_symb_obj[1] = DIAG_FILE; // file type
    file_symb_obj[2] = (size_t)bundle_array;
    file_symb_obj[4] = file_symb_obj[5] = file_symb_obj[6] = file_symb_obj[7] = -1;

    prgm_obj[2] = (size_t)file_symb_obj;
  }

  //
  // get references
  //
#ifdef _WIN32
    __declspec(dllexport)
#endif
  void diag_find_references(VMContext& context)
  {
    size_t* prgm_obj = APITools_GetObjectValue(context, 1);
    ParsedProgram* program = (ParsedProgram*)prgm_obj[0];

    const int line_num = (int)APITools_GetIntValue(context, 2);
    const int line_pos = (int)APITools_GetIntValue(context, 3);
    const wstring sys_path = APITools_GetStringValue(context, 4);

    SymbolTable* table = nullptr;
    Method* method = program->FindMethod(line_num, table);
    if(method) {
      wstring full_path = L"lang.obl";
      if(!sys_path.empty()) {
        full_path += L',' + sys_path;
      }

      ContextAnalyzer analyzer(program, full_path, false, false);
      if(analyzer.Analyze()) {
        vector<Expression*> expressions = analyzer.GetExpressions(method, line_num, line_pos);
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
            reference_obj[0] = (size_t)APITools_CreateStringValue(context, variable->GetName());
          }
            break;

          case METHOD_CALL_EXPR: {
            MethodCall* method_call = static_cast<MethodCall*>(expression);
            start_pos++; end_pos++;
            end_pos += (int)method_call->GetVariableName().size();
            reference_obj[0] = (size_t)APITools_CreateStringValue(context, method_call->GetMethodName());
          }
            break;
          }
          
          reference_obj[1] = DIAG_VARIABLE; // variable type
          reference_obj[4] = reference_obj[6] = expression->GetLineNumber() - 1;
          reference_obj[5] = start_pos - 1;
          reference_obj[7] = end_pos - 1;          
          refs_array_ptr[i] = (size_t)reference_obj;
        }

        prgm_obj[4] = (size_t)refs_array;
      }
    }
  }

  //
  // Supporting functions
  //
  
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
      diag_obj[1] = DIAG_ERROR; // error type
      diag_obj[3] = (size_t)APITools_CreateStringValue(context, file_str);
      diag_obj[4] = _wtoi(line_str.c_str());
      diag_obj[5] = _wtoi(pos_str.c_str());
      diag_obj[6] = diag_obj[7] = -1;
      diagnostics_array_ptr[i] = (size_t)diag_obj;
    }

    return diagnostics_array;
  }
}