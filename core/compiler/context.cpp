/***************************************************************************
 * Performs contextual analysis.
 *
 * Copyright (c) 2025, Randy Hollines
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
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRAN.TIES, INCLUDING, BUT NOT
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

#include "context.h"
#include "linker.h"
#include "../shared/instrs.h"

/****************************
 * Emits an error
 ****************************/
void ContextAnalyzer::ProcessError(ParseNode* node, const std::wstring &msg)
{
#ifdef _DEBUG
  GetLogger() << L"\tError: " << node->GetFileName() << L":(" << node->GetLineNumber() << L',' << node->GetLinePosition() << L"): " << msg << std::endl;
#endif

  const std::wstring &str_line_num = ToString(node->GetLineNumber());
  const std::wstring &str_line_pos = ToString(node->GetLinePosition());
  errors.insert(std::pair<int, std::wstring>(node->GetLineNumber(), node->GetFileName()+ L":(" + str_line_num + L',' + str_line_pos + L"): " + msg));
}

/****************************
 * Emits an error
 ****************************/
void ContextAnalyzer::ProcessError(const std::wstring& fn, int ln, int lp, const std::wstring& msg)
{
#ifdef _DEBUG
  GetLogger() << L"\tError: " << fn << L": (" << ln << L',' << lp << L"): " << msg << std::endl;
#endif

  const std::wstring& str_line_num = ToString(ln);
  const std::wstring& str_line_pos = ToString(lp);
  errors.insert(std::pair<int, std::wstring>(ln, fn+ L":(" + str_line_num + L',' + str_line_pos + L"): " + msg));
}

/****************************
 * Emits an error
 ****************************/
void ContextAnalyzer::ProcessError(const std::wstring& fn, const std::wstring& msg)
{
#ifdef _DEBUG
  GetLogger() << L"\tError: " << msg << std::endl;
#endif

  errors.insert(std::pair<int, std::wstring>(1, fn + L":(1,1): " + msg));
}

/****************************
 * Formats possible alternative
 * methods
 ****************************/
void ContextAnalyzer::ProcessErrorAlternativeMethods(std::wstring &message)
{
  if(alt_error_method_names.size() > 0) {
    message += L"\n\tPossible alternative(s):\n";
    for(size_t i = 0; i < alt_error_method_names.size(); ++i) {
      message += L"\t\t" + alt_error_method_names[i] + L'\n';
    }
    alt_error_method_names.clear();
  }
}

/****************************
 * Emits an error
 ****************************/
void ContextAnalyzer::ProcessWarning(ParseNode* node, const std::wstring& msg)
{
#ifdef _DEBUG
  GetLogger() << L"\tWarning: " << node->GetFileName() << L":(" << node->GetLineNumber() << L',' << node->GetLinePosition() << L"): " << msg << std::endl;
#endif

  const std::wstring& str_line_num = ToString(node->GetLineNumber());
  const std::wstring& str_line_pos = ToString(node->GetLinePosition());
  warnings.insert(std::pair<int, std::wstring>(node->GetLineNumber(), node->GetFileName() + L":(" + str_line_num + L',' + str_line_pos + L"): Warning: " + msg));
}

/****************************
 * Check for errors detected
 * during the contextual
 * analysis process.
 ****************************/
bool ContextAnalyzer::CheckErrorsWarnings()
{
  bool status = true;

  // check and process errors
  if(!errors.empty()) {
    std::map<int, std::wstring>::iterator error;
    for(error = errors.begin(); error != errors.end(); ++error) {
#if defined(_DIAG_LIB) || defined(_MODULE)
      error_strings.push_back(error->second);
#else
      std::wcerr << error->second << std::endl;
#endif
    }

#ifdef _DIAG_LIB
    program->SetErrorStrings(error_strings);
#endif

    status = false;
  }

#ifndef _MODULE
  // check and process warnings
  if(!warnings.empty()) {
    std::map<int, std::wstring>::iterator warning;
    for(warning = warnings.begin(); warning != warnings.end(); ++warning) {
#ifdef _DIAG_LIB
      warning_strings.push_back(warning->second);
#else
      std::wcerr << warning->second << std::endl;
#endif
    }

#ifdef _DIAG_LIB
    program->SetWarningStrings(warning_strings);
#endif
  }
#endif

  return status;
}

#ifdef _MODULE
std::vector<std::wstring> ContextAnalyzer::GetErrors()
{
  return error_strings;
}
#endif

/****************************
 * Starts the analysis process
 ****************************/
bool ContextAnalyzer::Analyze(bool is_lib)
{
#ifdef _DEBUG
  GetLogger() << L"\n--------- Contextual Analysis ---------" << std::endl;
#endif
  int class_id = 0;

#ifndef _SYSTEM
  // process libraries classes
  linker->Load(is_lib);
#endif

  // check uses
  const std::wstring file_name = program->GetFileName();
  std::vector<std::wstring> program_uses = program->GetLibUses();
  for(size_t i = 0; i < program_uses.size(); ++i) {
    const std::wstring &name = program_uses[i];
    if(!program->HasBundleName(name) && !linker->HasBundleName(name)) {
      ProcessError(file_name, L"Bundle name '" + name + L"' not defined in program or linked libraries");
    }
  }

  // resolve alias types
  std::vector<Type*>& types = TypeFactory::Instance()->GetTypes();
  for(size_t i = 0; i < types.size(); ++i) {
    Type* type = types[i];
    if(type->GetType() == ALIAS_TYPE) {
      Type* resloved_type = ResolveAlias(type->GetName(), type->GetFileName(), type->GetLineNumber(), type->GetLinePosition());
      if(resloved_type) {
        type->Set(resloved_type);
      }
    }
  }

  // add methods for default parameters
  std::vector<ParsedBundle*> bundles = program->GetBundles();
  for(size_t i = 0; i < bundles.size(); ++i) {
    ParsedBundle* bundle = bundles[i];
    std::vector<Class*> classes = bundle->GetClasses();
    for(size_t j = 0; j < classes.size(); ++j) {
      Class* klass = classes[j];
      std::vector<Method*> methods = klass->GetMethods();
      for(size_t k = 0; k < methods.size(); ++k) {
        AddDefaultParameterMethods(bundle, klass, methods[k]);
      }
    }
  }
  // re-encode method signatures; i.e. fully expand class names
  for(size_t i = 0; i < bundles.size(); ++i) {
    // methods
    ParsedBundle* bundle = bundles[i];
    std::vector<Class*> classes = bundle->GetClasses();
    for(size_t j = 0; j < classes.size(); ++j) {
      Class* klass = classes[j];
      std::vector<Method*> methods = klass->GetMethods();
      for(size_t k = 0; k < methods.size(); ++k) {
        Method* method = methods[k];
        if(!method->IsLambda()) {
          method->EncodeSignature(klass, program, linker);
        }
      }
    }

    // aliases
    std::vector<Alias*> aliases = bundle->GetAliases();
    for(size_t j = 0; j < aliases.size(); ++j) {
      aliases[j]->EncodeSignature(program, linker);
    }
  }

  // associate re-encoded method signatures with methods
  for(size_t i = 0; i < bundles.size(); ++i) {
    bundle = bundles[i];
    std::vector<Class*> classes = bundle->GetClasses();
    for(size_t j = 0; j < classes.size(); ++j) {
      Class* klass = classes[j];
      std::wstring parent_name = klass->GetParentName();
#ifdef _SYSTEM
      if(parent_name.size() == 0 && klass->GetName() != SYSTEM_BASE_NAME) {
#else
      if(parent_name.size() == 0) {
#endif
        parent_name = SYSTEM_BASE_NAME;
        klass->SetParentName(SYSTEM_BASE_NAME);
      }

      if(parent_name.size()) {
        Class* parent = SearchProgramClasses(parent_name);
        if(parent) {
          klass->SetParent(parent);
          parent->AddChild(klass);
        }
        else {
          LibraryClass* lib_parent = linker->SearchClassLibraries(parent_name, program->GetLibUses(klass->GetFileName()));
          if(lib_parent) {
            klass->SetLibraryParent(lib_parent);
            lib_parent->AddChild(klass);
          }
          else {
            ProcessError(klass, L"Attempting to inherent from an undefined class type");
          }
        }
      }
      // associate methods
      classes[j]->AssociateMethods();
    }
  }

  // process bundles
  bundles = program->GetBundles();
  AnalyzeDuplicateClasses(bundles);

  for(size_t i = 0; i < bundles.size(); ++i) {
    bundle = bundles[i];
    symbol_table = bundle->GetSymbolTableManager();

    // process enums
    std::vector<Enum*> enums = bundle->GetEnums();
    for(size_t j = 0; j < enums.size(); ++j) {
      AnalyzeEnum(enums[j], 0);
    }

    // process classes
    std::vector<Class*> classes = bundle->GetClasses();
    for(size_t j = 0; j < classes.size(); ++j) {
      AnalyzeClass(classes[j], class_id++, 0);
    }

    // check for duplicate instance and class level variables
    AnalyzeDuplicateEntries(classes, 0);

    // process class methods
    for(size_t j = 0; j < classes.size(); ++j) {
      AnalyzeMethods(classes[j], 0);
    }
  }

  // check for entry points
  if(!main_found && !is_lib) {
    ProcessError(program->GetFileName(), L"The 'Main(args)' function was not defined");
  }

#ifdef _DEBUG
  assert(!nested_call_depth);
#endif

  return CheckErrorsWarnings();
}

/****************************
 * Analyzes a class
 ****************************/
void ContextAnalyzer::AnalyzeEnum(Enum* eenum, const int depth)
{
#ifdef _DEBUG
  std::wstring msg = L"[enum: name='" + eenum->GetName() + L"']";
  Debug(msg, eenum->GetLineNumber(), depth);
#endif

  if(!HasProgramOrLibraryEnum(eenum->GetName())) {
    ProcessError(eenum, L"Undefined enum: '" + FormatTypeString(eenum->GetName()));
  }

  if(linker->SearchClassLibraries(eenum->GetName(), program->GetLibUses(eenum->GetFileName())) ||
     linker->SearchEnumLibraries(eenum->GetName(), program->GetLibUses(eenum->GetFileName()))) {
    ProcessError(eenum, L"Enum '" + FormatTypeString(eenum->GetName()) + L"' defined in program and shared libraries");
  }
}

/****************************
 * Find duplicate classes
 ****************************/
void ContextAnalyzer::AnalyzeDuplicateClasses(std::vector<ParsedBundle*>& bundles)
{
  for(size_t i = 0; i < bundles.size(); ++i) {
    std::vector<Class*> classes = bundles[i]->GetClasses();
    for(size_t j = 0; j < classes.size(); ++j) {
      Class* klass = classes[j];
      for(size_t k = 0; k < bundles.size(); ++k) {
        if(k != i) {
          if(bundles[k]->GetClass(klass->GetName())) {
            ProcessError(klass, L"Class '" + klass->GetName() + L"' defined in another bundle");
          }
        }
      }
    }
  }
}

/****************************
 * Checks for duplicate instance
 * and class level variables
 ****************************/
void ContextAnalyzer::AnalyzeDuplicateEntries(std::vector<Class*> &classes, const int depth)
{
  for(size_t i = 0; i < classes.size(); ++i) {
    // declarations
    Class* klass = classes[i];
    std::vector<Statement*> statements = klass->GetStatements();
    for(size_t j = 0; j < statements.size(); ++j) {
      Declaration* declaration = static_cast<Declaration*>(statements[j]);
      SymbolEntry* entry = declaration->GetEntry();
      if(entry) {
        // duplicate parent
        if(DuplicateParentEntries(entry, klass)) {
          size_t offset = entry->GetName().find(L':');
          if(offset != std::wstring::npos) {
            ++offset;
            const std::wstring short_name = entry->GetName().substr(offset, entry->GetName().size() - offset);
            ProcessError(declaration, L"Declaration name '" + short_name + L"' defined in a parent class");
          }
          else {
            ProcessError(declaration, L"Internal compiler error: Invalid entry name");
            exit(1);
          }
        }
      }
    }
  }
}

/****************************
 * Expands and validates methods with
 * default parameters
 ****************************/
void ContextAnalyzer::AddDefaultParameterMethods(ParsedBundle* bundle, Class* klass, Method* method)
{
  // declarations
  std::vector<Declaration*> declarations = method->GetDeclarations()->GetDeclarations();
  if(declarations.size() > 0 && declarations[declarations.size() - 1]->GetAssignment()) {
    bool default_params = true;
    for(int i = (int)declarations.size() - 1; i >= 0; --i) {
      if(declarations[i]->GetAssignment()) {
        if(method->IsVirtual()) {
          ProcessError(method, L"Virtual methods and interfaces cannot contain default parameter values");
          return;
        }

        if(!default_params) {
          ProcessError(declarations[0], L"Only trailing parameters may have default values");
          return;
        }
      }
      else {
        default_params = false;
      }
    }

    GenerateParameterMethods(bundle, klass, method);
  }
}

/****************************
 * Generates alternative methods for
 * method with default parameter values
 ****************************/
void ContextAnalyzer::GenerateParameterMethods(ParsedBundle* bundle, Class* klass, Method* method)
{
  // find initial parameter offset
  std::vector<Declaration*> declarations = method->GetDeclarations()->GetDeclarations();
  size_t inital_param_offset = 0;

  if(!inital_param_offset) {
    for(size_t i = 0; i < declarations.size(); ++i) {
      Declaration* declaration = declarations[i];
      if(declaration->GetAssignment()) {
        if(!inital_param_offset) {
          inital_param_offset = i;
        }
      }
    }
  }

  // build alternative methods
  while(inital_param_offset < declarations.size()) {
    Method* alt_method = TreeFactory::Instance()->MakeMethod(method->GetFileName(), method->GetLineNumber(), method->GetLinePosition(), method->GetEndLineNumber(), method->GetEndLinePosition(),
                                                             method->GetName(), method->GetMethodType(), method->IsStatic(), method->IsNative());
    alt_method->SetReturn(method->GetReturn());

    DeclarationList* alt_declarations = TreeFactory::Instance()->MakeDeclarationList();
    StatementList* alt_statements = TreeFactory::Instance()->MakeStatementList();

    bundle->GetSymbolTableManager()->NewParseScope();

    for(size_t i = 0; i < declarations.size(); ++i) {
      Declaration* declaration = declarations[i]->Copy();
      if(i < inital_param_offset) {
        alt_declarations->AddDeclaration(declaration);
        bundle->GetSymbolTableManager()->CurrentParseScope()->AddEntry(declaration->GetEntry());
      }
      else {
        Assignment* assignment = declaration->GetAssignment();
        assignment->GetVariable()->WasAlt();
        assignment->GetExpression()->SetEvalType(declaration->GetEntry()->GetType(), true);
        alt_statements->AddStatement(assignment);
      }
    }
    inital_param_offset++;

    // set statements
    alt_method->SetStatements(alt_statements);
    alt_method->SetDeclarations(alt_declarations);
    alt_method->SetOriginal(method);
    bundle->GetSymbolTableManager()->PreviousParseScope(alt_method->GetParsedName());

    // add method
    if(!klass->AddMethod(alt_method)) {
      ProcessError(method, L"Method or function already overloaded '" + method->GetUserName() + L"'");
    }
  }
}

/****************************
 * Analyzes a class
 ****************************/
void ContextAnalyzer::AnalyzeClass(Class* klass, const int id, const int depth)
{
#ifdef _DEBUG
  std::wstring msg = L"[class: name='" + klass->GetName() + L"'; id=" + ToString(id) +
    L"; virtual=" + (klass->IsVirtual() ? L"true" : L"false") + L"]";
  Debug(msg, klass->GetLineNumber(), depth);
#endif

  current_class = klass;
  current_class->SetCalled(true);

  klass->SetSymbolTable(symbol_table->GetSymbolTable(klass->GetName()));
  if(!HasProgramOrLibraryClass(klass->GetName())) {
    ProcessError(klass, L"Undefined class: '" + klass->GetName() + L"'");
  }

  if(linker->SearchClassLibraries(klass->GetName(), program->GetLibUses(klass->GetFileName())) ||
     linker->SearchEnumLibraries(klass->GetName(), program->GetLibUses(klass->GetFileName()))) {
    ProcessError(klass, L"Class '" + klass->GetName() + L"' defined in shared libraries");
  }

  // check generics
  AnalyzeGenerics(klass, depth);

  // check parent class
  CheckParent(klass, depth);

  // check interfaces
  AnalyzeInterfaces(klass, depth);

  // declarations
  std::vector<Statement*> statements = klass->GetStatements();
  for(size_t i = 0; i < statements.size(); ++i) {
    current_method = nullptr;
    AnalyzeDeclaration(static_cast<Declaration*>(statements[i]), current_class, depth + 1);
  }

#ifdef _DIAG_LIB
  current_class->SetExpressions(diagnostic_expressions);
  diagnostic_expressions.clear();
#endif
}

void ContextAnalyzer::CheckParent(Class* klass, const int depth)
{
  Class* parent_klass = klass->GetParent();
  if(parent_klass && (parent_klass->IsInterface() || parent_klass->HasGenerics())) {
    ProcessError(klass, L"Class '" + klass->GetName() + L"' cannot be derived from a generic or interface");
  }
  else {
    LibraryClass* parent_lib_klass = klass->GetLibraryParent();
    if(parent_lib_klass && parent_lib_klass->IsInterface()) {
      ProcessError(klass, L"Classes cannot be derived from interfaces");
    }
  }
}

/****************************
 * Analyzes methods
 ****************************/
void ContextAnalyzer::AnalyzeMethods(Class* klass, const int depth)
{
#ifdef _DEBUG
  std::wstring msg = L"[class: name='" + klass->GetName() + L"]";
  Debug(msg, klass->GetLineNumber(), depth);
#endif

  current_class = klass;
  current_table = symbol_table->GetSymbolTable(current_class->GetName());

  // methods
  std::vector<Method*> methods = klass->GetMethods();
  for(size_t i = 0; i < methods.size(); ++i) {
    AnalyzeMethod(methods[i], depth + 1);
  }

  // look for parent virtual methods
  if(current_class->GetParent() && current_class->GetParent()->IsVirtual()) {
    if(!AnalyzeVirtualMethods(current_class, current_class->GetParent(), depth)) {
      ProcessError(current_class, L"Not all virtual methods have been implemented for the class/interface: " +
                   current_class->GetParent()->GetName());
    }
  }
  else if(current_class->GetLibraryParent() && current_class->GetLibraryParent()->IsVirtual()) {
    if(!AnalyzeVirtualMethods(current_class, current_class->GetLibraryParent(), depth)) {
      ProcessError(current_class, L"Not all virtual methods have been implemented for the class/interface: " +
                   current_class->GetLibraryParent()->GetName());
    }
  }

  // collect anonymous classes
  if(klass->GetAnonymousCall()) {
    anonymous_classes.push_back(klass);
  }
}

/*****************************************************************
 * Check for generic classes and backing interfaces
 *****************************************************************/
void ContextAnalyzer::AnalyzeGenerics(Class* klass, const int depth)
{
  const std::vector<Class*> generic_classes = klass->GetGenericClasses();
  for(size_t i = 0; i < generic_classes.size(); ++i) {
    Class* generic_class = generic_classes[i];
    // check generic class
    const std::wstring generic_class_name = generic_class->GetName();
    if(HasProgramOrLibraryClass(generic_class_name)) {
      ProcessError(klass, L"Generic reference '" + generic_class_name + L"' previously defined as a class");
    }
    // check backing interface
    if(generic_class->HasGenericInterface()) {
      Type* generic_inf_type = generic_class->GetGenericInterface();
      Class* klass_generic_inf = nullptr; LibraryClass* lib_klass_generic_inf = nullptr; 
      if(GetProgramOrLibraryClass(generic_inf_type, klass_generic_inf, lib_klass_generic_inf)) {
        if(klass_generic_inf) {
          generic_inf_type->SetName(klass_generic_inf->GetName());
        }
        else {
          generic_inf_type->SetName(lib_klass_generic_inf->GetName());
        }
      }
      else {
        const std::wstring generic_inf_name = generic_inf_type->GetName();
        ProcessError(klass, L"Undefined backing generic interface: '" + generic_inf_name + L"'");
      }
    }
  }
}

/****************************
 * Checks for virtual method
 * implementations
 ****************************/
bool ContextAnalyzer::AnalyzeVirtualMethods(Class* impl_class, Class* virtual_class, const int depth)
{
  std::wstring error_msg;

  std::vector<Method*> virtual_class_methods = virtual_class->GetMethods();
  for(size_t i = 0; i < virtual_class_methods.size(); ++i) {
    Method* virtual_method = virtual_class_methods[i];
    if(virtual_method->IsVirtual()) {
      const std::wstring virtual_method_name = virtual_method->GetEncodedName();
      // search for implementation method via signature
      const size_t offset = virtual_method_name.find(':');
      if(offset != std::wstring::npos) {
        const std::wstring encoded_name = impl_class->GetName() + virtual_method_name.substr(offset);
        Method* impl_method = impl_class->GetMethod(encoded_name);
        if(impl_method) {
          AnalyzeVirtualMethod(impl_class, impl_method->GetMethodType(), impl_method->GetReturn(),
                               impl_method->IsStatic(), impl_method->IsVirtual(), virtual_method);
        }
        else {
          error_msg += L"\n\tMissing: '";
          error_msg += virtual_method->GetUserName();
          error_msg += L'\'';
        }
      }
    }
  }
  
  const bool success = error_msg.empty();
  if(!success) {
    ProcessError(impl_class, L"The following virtual methods/functions have not been implemented:" + error_msg);
  }

  return success;
}

/****************************
 * Checks for interface
 * implementations
 ****************************/
void ContextAnalyzer::AnalyzeInterfaces(Class* klass, const int depth)
{
  const std::vector<std::wstring> interface_names = klass->GetInterfaceNames();
  std::vector<Class*> interfaces;
  std::vector<LibraryClass*> lib_interfaces;
  for(size_t i = 0; i < interface_names.size(); ++i) {
    const std::wstring &interface_name = interface_names[i];
    Class* inf_klass = SearchProgramClasses(interface_name);
    if(inf_klass) {
      if(!inf_klass->IsInterface()) {
        ProcessError(klass, L"Expected an interface type");
        return;
      }

      // ensure interface methods are virtual
      std::vector<Method*> methods = inf_klass->GetMethods();
      for(size_t i = 0; i < methods.size(); ++i) {
        if(!methods[i]->IsVirtual()) {
          ProcessError(methods[i], L"Interface method must be defined as 'virtual'");
        }
      }
      // ensure implementation
      if(!AnalyzeVirtualMethods(klass, inf_klass, depth)) {
        ProcessError(klass, L"Not all methods have been implemented for the interface: " + inf_klass->GetName());
      }
      else {
        // add interface
        inf_klass->SetCalled(true);
        inf_klass->AddChild(klass);
        interfaces.push_back(inf_klass);
      }
    }
    else {
      LibraryClass* inf_lib_klass = linker->SearchClassLibraries(interface_name, program->GetLibUses(current_class->GetFileName()));
      if(inf_lib_klass) {
        if(!inf_lib_klass->IsInterface()) {
          ProcessError(klass, L"Expected an interface type");
          return;
        }

        // ensure interface methods are virtual
        std::map<const std::wstring, LibraryMethod*> lib_methods = inf_lib_klass->GetMethods();
        std::map<const std::wstring, LibraryMethod*>::iterator iter;
        for(iter = lib_methods.begin(); iter != lib_methods.end(); ++iter) {
          LibraryMethod* lib_method = iter->second;
          if(!lib_method->IsVirtual()) {
            ProcessError(klass, L"Interface method must be defined as 'virtual'");
          }
        }
        // ensure implementation
        if(!AnalyzeVirtualMethods(klass, inf_lib_klass, depth)) {
          ProcessError(klass, L"Not all methods have been implemented for the interface: '" +
                       inf_lib_klass->GetName() + L"'");
        }
        else {
          // add interface
          inf_lib_klass->SetCalled(true);
          inf_lib_klass->AddChild(klass);
          lib_interfaces.push_back(inf_lib_klass);
        }
      }
      else {
        ProcessError(klass, L"Undefined interface: '" + interface_name + L"'");
      }
    }
  }
  // save interfaces
  klass->SetInterfaces(interfaces);
  klass->SetLibraryInterfaces(lib_interfaces);
}

/****************************
 * Analyzes virtual method, which
 * are made when compiling shared
 * libraries.
 ****************************/
void ContextAnalyzer::AnalyzeVirtualMethod(Class* impl_class, MethodType impl_mthd_type, Type* impl_return,
                                           bool impl_is_static, bool impl_is_virtual, Method* virtual_method)
{
  // check method types
  if(impl_mthd_type != virtual_method->GetMethodType()) {
    ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                 virtual_method->GetClass()->GetName());
  }
  // check method returns
  Type* virtual_return = virtual_method->GetReturn();
  if(impl_return->GetType() != virtual_return->GetType()) {
    ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                 virtual_method->GetClass()->GetName());
  }
  else if(impl_return->GetType() == CLASS_TYPE &&
          impl_return->GetName() != virtual_return->GetName()) {
    Class* impl_cls = SearchProgramClasses(impl_return->GetName());
    Class* virtual_cls = SearchProgramClasses(virtual_return->GetName());
    if(impl_cls && virtual_cls && impl_cls != virtual_cls) {
      LibraryClass* impl_lib_cls = linker->SearchClassLibraries(impl_return->GetName(),
                                                                program->GetLibUses(current_class->GetFileName()));
      LibraryClass* virtual_lib_cls = linker->SearchClassLibraries(virtual_return->GetName(),
                                                                   program->GetLibUses(current_class->GetFileName()));
      if(impl_lib_cls && virtual_lib_cls && impl_lib_cls != virtual_lib_cls) {
        ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                     virtual_method->GetClass()->GetName());
      }
    }
  }
  // check function vs. method
  if(impl_is_static != virtual_method->IsStatic()) {
    ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                 virtual_method->GetClass()->GetName());
  }
}

/****************************
 * Analyzes virtual method, which
 * are made when compiling shared
 * libraries.
 ****************************/
bool ContextAnalyzer::AnalyzeVirtualMethods(Class* impl_class, LibraryClass* lib_virtual_class, const int depth)
{
  std::wstring error_msg;

  std::map<const std::wstring, LibraryMethod*>::iterator iter;
  std::map<const std::wstring, LibraryMethod*> lib_virtual_class_methods = lib_virtual_class->GetMethods();
  for(iter = lib_virtual_class_methods.begin(); iter != lib_virtual_class_methods.end(); ++iter) {
    LibraryMethod* virtual_method = iter->second;
    if(virtual_method->IsVirtual()) {
      const std::wstring virtual_method_name = virtual_method->GetName();
      // search for implementation method via signature
      const size_t offset = virtual_method_name.find(':');
      if(offset != std::wstring::npos) {
        const std::wstring encoded_name = impl_class->GetName() + virtual_method_name.substr(offset);
        Method* impl_method = impl_class->GetMethod(encoded_name);
        if(impl_method) {
          AnalyzeVirtualMethod(impl_class, impl_method->GetMethodType(), impl_method->GetReturn(),
                               impl_method->IsStatic(), impl_method->IsVirtual(), virtual_method);
        }
        else {
          error_msg += L"\n\t'Missing:";
          error_msg += virtual_method->GetUserName();
          error_msg += L'\'';
        }
      }
    }
  }

  const bool success = error_msg.empty();
  if(!success) {
    ProcessError(impl_class, L"The following virtual methods/functions have not been implemented:" + error_msg);
  }
  
  return success;
}

/****************************
 * Analyzes virtual method, which
 * are made when compiling shared
 * libraries.
 ****************************/
void ContextAnalyzer::AnalyzeVirtualMethod(Class* impl_class, MethodType impl_mthd_type, Type* impl_return,
                                           bool impl_is_static, bool impl_is_virtual, LibraryMethod* virtual_method)
{
  // check method types
  if(impl_mthd_type != virtual_method->GetMethodType()) {
    ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                 virtual_method->GetLibraryClass()->GetName());
  }
  // check method returns
  Type* virtual_return = virtual_method->GetReturn();
  if(impl_return->GetType() != virtual_return->GetType()) {
    ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                 virtual_method->GetLibraryClass()->GetName());
  }
  else if(impl_return->GetType() == CLASS_TYPE &&
          impl_return->GetName() != virtual_return->GetName()) {
    Class* impl_cls = SearchProgramClasses(impl_return->GetName());
    Class* virtual_cls = SearchProgramClasses(virtual_return->GetName());
    if(impl_cls && virtual_cls && impl_cls != virtual_cls) {
      LibraryClass* impl_lib_cls = linker->SearchClassLibraries(impl_return->GetName(),
                                                                program->GetLibUses(current_class->GetFileName()));
      LibraryClass* virtual_lib_cls = linker->SearchClassLibraries(virtual_return->GetName(),
                                                                   program->GetLibUses(current_class->GetFileName()));
      if(impl_lib_cls && virtual_lib_cls && impl_lib_cls != virtual_lib_cls) {
        ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                     virtual_method->GetLibraryClass()->GetName());
      }
    }
  }
  // check function vs. method
  if(impl_is_static != virtual_method->IsStatic()) {
    ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                 virtual_method->GetLibraryClass()->GetName());
  }
  // check virtual
  if(impl_is_virtual) {
    ProcessError(impl_class, L"Implementation method cannot be virtual");
  }
}

/****************************
 * Analyzes a method
 ****************************/
void ContextAnalyzer::AnalyzeMethod(Method* method, const int depth)
{
#ifdef _DEBUG
  std::wstring msg = L"(method: name='" + method->GetName() + L"; parsed='" + method->GetParsedName() + L"')";
  Debug(msg, method->GetLineNumber(), depth);
#endif

#ifdef _DIAG_LIB
  diagnostic_expressions.clear();
#endif
  method->SetId();
  current_method = method;
  current_table = symbol_table->GetSymbolTable(method->GetParsedName());
  method->SetSymbolTable(current_table);

  // declarations
  std::vector<Declaration*> declarations = method->GetDeclarations()->GetDeclarations();
  for(size_t i = 0; i < declarations.size(); ++i) {
    Declaration* declaration = declarations[i];
    AnalyzeDeclaration(declaration, current_class, depth + 1);
    declaration->SetParameter();
  }

  // process statements if function/method is not virtual
  if(!method->IsVirtual()) {
    // statements
    std::vector<Statement*> statements = method->GetStatements()->GetStatements();
    for(size_t i = 0; i < statements.size(); ++i) {
      AnalyzeStatement(statements[i], depth + 1);
    }

    // check for parent call
    if((method->GetMethodType() == NEW_PUBLIC_METHOD || method->GetMethodType() == NEW_PRIVATE_METHOD) &&
       (current_class->GetParent() || (current_class->GetLibraryParent() && current_class->GetLibraryParent()->GetName() != SYSTEM_BASE_NAME))) {
      if(statements.size() == 0 || statements.front()->GetStatementType() != METHOD_CALL_STMT) {
        if(!method->IsAlt() && !current_class->IsInterface()) {
          ProcessError(method, L"Parent call required");
        }
      }
      else {
        MethodCall* mthd_call = static_cast<MethodCall*>(statements.front());
        if(mthd_call->GetCallType() != PARENT_CALL && !current_class->IsInterface()) {
          ProcessError(method, L"Parent call required");
        }
      }
    }

#ifndef _SYSTEM
    // check for return
    if(method->GetMethodType() != NEW_PUBLIC_METHOD && method->GetMethodType() != NEW_PRIVATE_METHOD &&
       method->GetReturn() && method->GetReturn()->GetType() != NIL_TYPE) {
      if(!AnalyzeReturnPaths(method->GetStatements(), depth + 1) && !method->IsAlt()) {
        ProcessError(method, L"All method/function paths must return a value");
      }

      AnalyzeDeadReturns(method->GetStatements(), depth + 1);
    }
#endif

    // check program main
    const std::wstring main_str = current_class->GetName() + L":Main:o.System.String*,";
    if(method->GetEncodedName() == main_str) {
      if(main_found) {
        ProcessError(method, L"The 'Main(args)' function has already been defined");
      }
      else if(method->IsStatic()) {
        current_class->SetCalled(true);
        program->SetStart(current_class, method);
        main_found = true;
      }

      if(main_found && (is_lib)) {
        ProcessError(method, L"Libraries and web applications may not define a 'Main(args)' function");
      }
    }
  }

  // check for unreferenced variables
  CheckUnreferencedVariables(method);

#ifdef _DIAG_LIB
  current_method->SetExpressions(diagnostic_expressions);
#endif
}

/****************************
 * Check for unreferenced variables
 ****************************/
void ContextAnalyzer::CheckUnreferencedVariables(Method* method)
{
  std::vector<SymbolEntry*> entries = current_table->GetEntries();
  for(size_t i = 0; i < entries.size(); ++i) {
    SymbolEntry* entry = entries[i];
    if(entry->IsLocal()) {
      // check for unreferenced variables
      std::vector<Variable*> variables = entry->GetVariables();
      if(!entry->IsParameter() && !entry->IsLoaded()) {
        const int start_line = method->GetLineNumber();
        const int end_line = method->GetEndLineNumber();
        for(size_t j = 0; j < variables.size(); ++j) {
          Variable* variable = variables[j];
          // range check required for method overloading (not ideal)
          if(variable->GetLineNumber() >= start_line && variable->GetLineNumber() < end_line) {
            ProcessWarning(variable, L"Variable '" + variable->GetName() + L"' is unreferenced");
          }
        }
      }
    }
  }
}

/****************************
 * Analyzes a lambda function
 ****************************/
void ContextAnalyzer::AnalyzeLambda(Lambda* lambda, const int depth)
{
  // already been checked
  if(lambda->GetMethodCall()) {
    return;
  }

  // by type
  Type* lambda_type = nullptr;
  const std::wstring lambda_name = lambda->GetName();
  bool is_inferred = HasInferredLambdaTypes(lambda_name);

  if(lambda->GetLambdaType()) {
    lambda_type = lambda->GetLambdaType();
  }
  // by name
  else if(!is_inferred) {
    lambda_type = ResolveAlias(lambda_name, lambda);
  }

  if(lambda_type) {
    BuildLambdaFunction(lambda, lambda_type, depth);
  }
  // derived type
  else if(is_inferred) {
    lambda_inferred.first = lambda;
  }
  else {
    ProcessError(lambda, L"Invalid lambda type");
  }
}

Type* ContextAnalyzer::ResolveAlias(const std::wstring& name, const std::wstring& fn, int ln, int lp) {
  Type* alias_type = nullptr;

  std::wstring alias_name;
  const size_t middle = name.find(L'#');
  if(middle != std::wstring::npos) {
    alias_name = name.substr(0, middle);
  }

  std::wstring type_name;
  if(middle + 1 < name.size()) {
    type_name = name.substr(middle + 1);
  }

  Alias* alias = program->GetAlias(alias_name);
  if(alias) {
    alias_type = alias->GetType(type_name);
    if(alias_type) {
      alias_type = TypeFactory::Instance()->MakeType(alias_type);
    }
    else {
      if(name.empty()) {
        ProcessError(fn, ln, lp, L"Invalid alias");
      }
      else {
        ProcessError(fn, ln, lp, L"Undefined alias: '" + FormatTypeString(name) + L"'");
      }
    }
  }
  else {
    LibraryAlias* lib_alias = linker->SearchAliasLibraries(alias_name, program->GetLibUses(fn));
    if(lib_alias) {
      alias_type = lib_alias->GetType(type_name);
      if(alias_type) {
        alias_type = TypeFactory::Instance()->MakeType(alias_type);
      }
      else {
        if(name.empty()) {
          ProcessError(fn, ln, lp, L"Invalid alias");
        }
        else {
          ProcessError(fn, ln, lp, L"Undefined alias: '" + FormatTypeString(name) + L"'");
        }
      }
    }
    else {
      if(name.empty()) {
        ProcessError(fn, ln, lp, L"Invalid alias");
      }
      else {
        ProcessError(fn, ln, lp, L"Undefined alias: '" + FormatTypeString(name) + L"'");
      }
    }
  }  

  if(alias_type && alias_type->GetType() == ALIAS_TYPE) {
    ProcessError(fn, ln, lp, L"Invalid nested alias reference");
    return nullptr;
  }
  
  return alias_type;
}

Method* ContextAnalyzer::DerivedLambdaFunction(std::vector<Method*>& alt_mthds)
{
  if(lambda_inferred.first && lambda_inferred.second && alt_mthds.size() == 1) {
    MethodCall* lambda_inferred_call = lambda_inferred.second;
    Method* alt_mthd = alt_mthds[0];
    std::vector<Declaration*> alt_mthd_types = alt_mthd->GetDeclarations()->GetDeclarations();
    if(alt_mthd_types.size() == 1 && alt_mthd_types[0]->GetEntry() && 
       alt_mthd_types[0]->GetEntry()->GetType()->GetType() == FUNC_TYPE) {
      // set parameters
      std::vector<Type*> inferred_type_params;
      Type* alt_mthd_type = alt_mthd_types[0]->GetEntry()->GetType();
      const std::vector<Type*> func_params = alt_mthd_type->GetFunctionParameters();
      for(size_t i = 0; i < func_params.size(); ++i) {
        inferred_type_params.push_back(ResolveGenericType(func_params[i], lambda_inferred_call, alt_mthd->GetClass(), nullptr));
      }
      // set return
      Type* inferred_type_rtrn = ResolveGenericType(alt_mthd_type->GetFunctionReturn(), lambda_inferred_call, alt_mthd->GetClass(), nullptr);

      Type* inferred_type = TypeFactory::Instance()->MakeType(FUNC_TYPE);
      inferred_type->SetFunctionParameters(inferred_type_params);
      inferred_type->SetFunctionReturn(inferred_type_rtrn);

      // build lambda function
      BuildLambdaFunction(lambda_inferred.first, inferred_type, 0);
      return alt_mthd;
    }
  }

  return nullptr;
}

LibraryMethod* ContextAnalyzer::DerivedLambdaFunction(std::vector<LibraryMethod*>& alt_mthds)
{
  if(lambda_inferred.first && lambda_inferred.second && alt_mthds.size() == 1) {
    MethodCall* lambda_inferred_call = lambda_inferred.second;
    LibraryMethod* alt_mthd = alt_mthds[0];
    std::vector<frontend::Type*> alt_mthd_types = alt_mthd->GetDeclarationTypes();
    if(alt_mthd_types.size() == 1 && alt_mthd_types[0]->GetType() == FUNC_TYPE) {
      // set parameters
      std::vector<Type*> inferred_type_params;
      Type* alt_mthd_type = alt_mthd_types[0];
      const std::vector<Type*> func_params = alt_mthd_type->GetFunctionParameters();
      for(size_t i = 0; i < func_params.size(); ++i) {
        inferred_type_params.push_back(ResolveGenericType(func_params[i], lambda_inferred_call, NULL, alt_mthd->GetLibraryClass()));
      }
      // set return
      Type* inferred_type_rtrn = ResolveGenericType(alt_mthd_type->GetFunctionReturn(), lambda_inferred_call, NULL, alt_mthd->GetLibraryClass());
      
      Type* inferred_type = TypeFactory::Instance()->MakeType(FUNC_TYPE);
      inferred_type->SetFunctionParameters(inferred_type_params);
      inferred_type->SetFunctionReturn(inferred_type_rtrn);

      // build lambda function
      BuildLambdaFunction(lambda_inferred.first, inferred_type, 0);
      return alt_mthd;
    }
  }

  return nullptr;
}

void ContextAnalyzer::BuildLambdaFunction(Lambda* lambda, Type* lambda_type, const int depth)
{
  // set return
  Method* method = lambda->GetMethod();
  current_method->SetAndOr(true);
  method->SetReturn(lambda_type->GetFunctionReturn());

  // update declarations
  std::vector<Type*> types = lambda_type->GetFunctionParameters();
  DeclarationList* declaration_list = method->GetDeclarations();
  std::vector<Declaration*> declarations = declaration_list->GetDeclarations();
  if(types.size() == declarations.size()) {
    // encode lookup
    method->EncodeSignature();

    for(size_t i = 0; i < declarations.size(); ++i) {
      declarations[i]->GetEntry()->SetType(types[i]);
    }

    current_class->AddMethod(method);
    method->EncodeSignature(current_class, program, linker);
    current_class->AssociateMethod(method);

    // check method and restore context
    capture_lambda = lambda;
    capture_method = current_method;
    capture_table = current_table;

    AnalyzeMethod(method, depth + 1);

    current_table = capture_table;
    capture_table = nullptr;

    current_method = capture_method;
    capture_method = nullptr;
    capture_lambda = nullptr;

    const std::wstring full_method_name = method->GetName();
    const size_t offset = full_method_name.find(':');
    if(offset != std::wstring::npos) {
      const std::wstring method_name = full_method_name.substr(offset + 1);

      // create method call
      MethodCall* method_call = TreeFactory::Instance()->MakeMethodCall(method->GetFileName(), method->GetLineNumber(), method->GetLinePosition(), 
                                                                        -1, -1, method->GetEndLineNumber(), method->GetEndLinePosition(),
                                                                        current_class->GetName(), method_name, MapLambdaDeclarations(declaration_list));
      method_call->SetFunctionalReturn(method->GetReturn());
      AnalyzeMethodCall(method_call, depth + 1);
      lambda->SetMethodCall(method_call);
      lambda->SetTypes(method_call->GetEvalType());
    }
    else {
      std::wcerr << L"Internal compiler error: Invalid method name." << std::endl;
      exit(1);
    }
  }
  else {
    ProcessError(lambda, L"Deceleration and parameter size mismatch");
  }
}

/****************************
 * maps lambda decelerations 
 * to parameter std::list
 ****************************/
ExpressionList* ContextAnalyzer::MapLambdaDeclarations(DeclarationList* declarations)
{
  ExpressionList* expressions = TreeFactory::Instance()->MakeExpressionList();

  const std::vector<Declaration*> dclrs = declarations->GetDeclarations();
  for(size_t i = 0; i < dclrs.size(); ++i) {
    std::wstring ident;
    Type* dclr_type = dclrs[i]->GetEntry()->GetType();
    switch(dclr_type->GetType()) {
    case NIL_TYPE:
    case VAR_TYPE:
      break;

    case BOOLEAN_TYPE:
      ident = BOOL_CLASS_ID;
      break;

    case BYTE_TYPE:
      ident = BYTE_CLASS_ID;
      break;

    case CHAR_TYPE:
      ident = CHAR_CLASS_ID;
      break;

    case INT_TYPE:
      ident = INT_CLASS_ID;
      break;

    case  FLOAT_TYPE:
      ident = FLOAT_CLASS_ID;
      break;

    case CLASS_TYPE:
    case FUNC_TYPE:
      ident = dclr_type->GetName();
      break;
        
    case ALIAS_TYPE:
      break;
    }

    if(!ident.empty()) {
      expressions->AddExpression(TreeFactory::Instance()->MakeVariable(dclrs[i]->GetFileName(), dclrs[i]->GetLineNumber(), dclrs[i]->GetLinePosition(), ident));
    }
  }

  return expressions;
}

/****************************
 * Check to determine if lambda 
 * concrete types are inferred
 ****************************/
bool ContextAnalyzer::HasInferredLambdaTypes(const std::wstring lambda_name)
{
  return lambda_inferred.second && lambda_name.empty();
}

void ContextAnalyzer::CheckLambdaInferredTypes(MethodCall* method_call, int depth)
{
  ExpressionList* call_params = method_call->GetCallingParameters();
  const std::vector<Expression*> expressions = call_params->GetExpressions();
  if(expressions.size() == 1 && expressions.at(0)->GetExpressionType() == LAMBDA_EXPR) {
    lambda_inferred.second = method_call;
  }
  else {
    lambda_inferred.first = nullptr;
    lambda_inferred.second = nullptr;
  }
}

/****************************
 * Analyzes method return
 * paths
 ****************************/
bool ContextAnalyzer::AnalyzeReturnPaths(StatementList* statement_list, const int depth)
{
  std::vector<Statement*> statements = statement_list->GetStatements();
  if(statements.size() == 0) {
    ProcessError(current_method, L"All method/function paths must return a value");
  }
  else {
    Statement* last_statement = statements.back();
    switch(last_statement->GetStatementType()) {
    case SELECT_STMT:
      return AnalyzeReturnPaths(static_cast<Select*>(last_statement), depth + 1);

    case IF_STMT:
      return AnalyzeReturnPaths(static_cast<If*>(last_statement), false, depth + 1);

    case RETURN_STMT:
      return true;

    case EMPTY_STMT: {
      std::vector<Statement*>::reverse_iterator rev_iter = statements.rbegin();
      while(rev_iter != statements.rend() && (*rev_iter)->GetStatementType() == EMPTY_STMT) {
        ++rev_iter;
      } 

      if(rev_iter == statements.rend()) {
        return false;
      }

      return (*rev_iter)->GetStatementType() == RETURN_STMT;
    }
      break;

    default:
      if(!current_method->IsAlt()) {
        ProcessError(current_method, L"All method/function paths must return a value");
      }
      break;
    }
  }

  return false;
}

/****************************
 * Detects dead return
 ****************************/
bool ContextAnalyzer::AnalyzeDeadReturns(StatementList* statement_list, const int depth)
{
  size_t count = 0;
  Statement* orig_return_stmt = nullptr;

  std::vector<Statement*> statements = statement_list->GetStatements();
  for(const auto& statement : statements) {
    switch (statement->GetStatementType()) {
    case RETURN_STMT:
      ++count;
      if(!orig_return_stmt) {
        orig_return_stmt = statement;
      }
      break;

    default:
      break;
    }
  }

  if(count > 1) {
    ProcessWarning(orig_return_stmt, L"Code after this statement will not execute");
  }

  return false;
}

bool ContextAnalyzer::AnalyzeReturnPaths(If* if_stmt, bool nested, const int depth)
{
  bool if_ok = false;
  bool if_else_ok = false;
  bool else_ok = false;

  // 'if' statements
  StatementList* if_list = if_stmt->GetIfStatements();
  if(if_list) {
    if_ok = AnalyzeReturnPaths(if_list, depth + 1);
  }

  If* next = if_stmt->GetNext();
  if(next) {
    if_else_ok = AnalyzeReturnPaths(next, true, depth);
  }

  // 'else'
  StatementList* else_list = if_stmt->GetElseStatements();
  if(else_list) {
    else_ok = AnalyzeReturnPaths(else_list, depth + 1);
  }
  else if(!if_else_ok) {
    return false;
  }

  // if and else
  if(!next) {
    return if_ok && (else_ok || if_else_ok);
  }

  // if, else-if and else
  if(if_ok && if_else_ok) {
    return true;
  }

  return false;
}

bool ContextAnalyzer::AnalyzeReturnPaths(Select* select_stmt, const int depth)
{
  std::map<ExpressionList*, StatementList*> statements = select_stmt->GetStatements();
  std::map<int, StatementList*> label_statements;
  for(std::map<ExpressionList*, StatementList*>::iterator iter = statements.begin(); iter != statements.end(); ++iter) {
    if(!AnalyzeReturnPaths(iter->second, depth + 1)) {
      return false;
    }
  }

  StatementList* other_stmts = select_stmt->GetOther();
  if(other_stmts) {
    if(!AnalyzeReturnPaths(other_stmts, depth + 1)) {
      return false;
    }
  }
  else {
    return false;
  }

  return true;
}

/****************************
 * Analyzes a statements
 ****************************/
void ContextAnalyzer::AnalyzeStatements(StatementList* statement_list, const int depth)
{
  current_table->NewScope();
  std::vector<Statement*> statements = statement_list->GetStatements();
  for(size_t i = 0; i < statements.size(); ++i) {
    AnalyzeStatement(statements[i], depth + 1);
  }
  current_table->PreviousScope();
}

/****************************
 * Analyzes a statement
 ****************************/
void ContextAnalyzer::AnalyzeStatement(Statement* statement, const int depth)
{
  if(statement) {
   switch(statement->GetStatementType()) {
    case EMPTY_STMT:
    case SYSTEM_STMT:
      break;

    case DECLARATION_STMT: {
      Declaration* declaration = static_cast<Declaration*>(statement);
      if(declaration->GetChild()) {
        // build stack declarations
        std::stack<Declaration*> declarations;
        while(declaration) {
          declarations.push(declaration);
          declaration = declaration->GetChild();
        }
        // process declarations
        while(!declarations.empty()) {
          AnalyzeDeclaration(declarations.top(), current_class, depth);
          declarations.pop();
        }
      }
      else {
        AnalyzeDeclaration(static_cast<Declaration*>(statement), current_class, depth);
      }
    }
      break;

    case METHOD_CALL_STMT: {
      MethodCall* mthd_call = static_cast<MethodCall*>(statement);
      AnalyzeMethodCall(mthd_call, depth);
      AnalyzeCast(mthd_call, depth + 1);
#ifdef _DIAG_LIB
      diagnostic_expressions.push_back(mthd_call);
#endif
    }
      break;


    case ADD_ASSIGN_STMT:
      AnalyzeAssignment(static_cast<Assignment*>(statement), statement->GetStatementType(), depth);
      break;

    case SUB_ASSIGN_STMT:
    case MUL_ASSIGN_STMT:
    case DIV_ASSIGN_STMT:
      AnalyzeAssignment(static_cast<Assignment*>(statement), statement->GetStatementType(), depth);
      break;

    case ASSIGN_STMT: {
      Assignment* assignment = static_cast<Assignment*>(statement);
      if(assignment->GetChild()) {
        // build stack assignments
        std::stack<Assignment*> assignments;
        while(assignment) {
          assignments.push(assignment);
          assignment = assignment->GetChild();
        }
        // process assignments
        while(!assignments.empty()) {
          AnalyzeAssignment(assignments.top(), statement->GetStatementType(), depth);
          assignments.pop();
        }
      }
      else {
        AnalyzeAssignment(assignment, statement->GetStatementType(), depth);
      }
    }
                    break;

    case SIMPLE_STMT:
      AnalyzeSimpleStatement(static_cast<SimpleStatement*>(statement), depth);
      break;

    case RETURN_STMT:
      AnalyzeReturn(static_cast<Return*>(statement), depth);
      break;

    case LEAVING_STMT:
      AnalyzeLeaving(static_cast<Leaving*>(statement), depth);
      break;

    case IF_STMT:
      AnalyzeIf(static_cast<If*>(statement), depth);
      break;

    case DO_WHILE_STMT:
      AnalyzeDoWhile(static_cast<DoWhile*>(statement), depth);
      break;

    case WHILE_STMT:
      AnalyzeWhile(static_cast<While*>(statement), depth);
      break;

    case FOR_STMT:
      AnalyzeFor(static_cast<For*>(statement), depth);
      break;

    case BREAK_STMT:
    case CONTINUE_STMT:
      if(in_loop <= 0) {
        ProcessError(statement, L"Breaks are only allowed in loops.");
      }
      break;

    case SELECT_STMT:
      current_method->SetAndOr(true);
      AnalyzeSelect(static_cast<Select*>(statement), depth);
      break;

    case CRITICAL_STMT:
      AnalyzeCritical(static_cast<CriticalSection*>(statement), depth);
      break;

    default:
      ProcessError(statement, L"Undefined statement");
      break;
    }
  }
}

/****************************
 * Analyzes an expression
 ****************************/
void ContextAnalyzer::AnalyzeExpression(Expression* expression, const int depth)
{
  if(expression) {
    ++expression_depth;

    StringConcat* str_concat = AnalyzeStringConcat(expression, depth + 1);
    if(str_concat) {
      expression->SetPreviousExpression(str_concat);
    }
    else {
      switch(expression->GetExpressionType()) {
      case LAMBDA_EXPR:
        AnalyzeLambda(static_cast<Lambda*>(expression), depth);
        break;

      case STAT_ARY_EXPR:
        AnalyzeStaticArray(static_cast<StaticArray*>(expression), depth);
        break;

      case CHAR_STR_EXPR:
        AnalyzeCharacterString(static_cast<CharacterString*>(expression), depth + 1);
        break;

      case COND_EXPR:
        AnalyzeConditional(static_cast<Cond*>(expression), depth);
        break;

      case METHOD_CALL_EXPR:
        AnalyzeMethodCall(static_cast<MethodCall*>(expression), depth);
#ifdef _DIAG_LIB
        diagnostic_expressions.push_back(expression);
#endif
        break;

      case NIL_LIT_EXPR:
#ifdef _DEBUG
        Debug(L"nil literal", expression->GetLineNumber(), depth);
#endif
        break;

      case BOOLEAN_LIT_EXPR:
#ifdef _DEBUG
        Debug(L"boolean literal", expression->GetLineNumber(), depth);
#endif
        break;

      case CHAR_LIT_EXPR:
#ifdef _DEBUG
        Debug(L"character literal", expression->GetLineNumber(), depth);
#endif
        break;

      case INT_LIT_EXPR:
#ifdef _DEBUG
        Debug(L"integer literal", expression->GetLineNumber(), depth);
#endif
        break;

      case FLOAT_LIT_EXPR:
#ifdef _DEBUG
        Debug(L"float literal", expression->GetLineNumber(), depth);
#endif
        break;

      case VAR_EXPR:
        AnalyzeVariable(static_cast<Variable*>(expression), depth);
        break;

      case AND_EXPR:
      case OR_EXPR:
        current_method->SetAndOr(true);
        AnalyzeCalculation(static_cast<CalculatedExpression*>(expression), depth + 1);
        break;

      case STR_CONCAT_EXPR:
#ifdef _DEBUG
        Debug(L"string concat", expression->GetLineNumber(), depth);
#endif
        break;

      case BIT_NOT_EXPR: {
        Expression* left = static_cast<CalculatedExpression*>(expression)->GetLeft();
        AnalyzeExpression(left, depth + 1);
        if(left->GetEvalType()) {
          Type* eval_type = left->GetEvalType();
          switch(eval_type->GetType()) {
          case BYTE_TYPE:
          case CHAR_TYPE:
          case INT_TYPE:
            expression->SetEvalType(eval_type, true);
            break;

          default:
            ProcessError(expression, L"Expected Byte, Char, Int or Enum class type");
            break;
          }
        }
      }
        break;

      case EQL_EXPR:
      case NEQL_EXPR:
      case LES_EXPR:
      case GTR_EXPR:
      case LES_EQL_EXPR:
      case GTR_EQL_EXPR:
      case ADD_EXPR:
      case SUB_EXPR:
      case MUL_EXPR:
      case DIV_EXPR:
      case MOD_EXPR:
      case SHL_EXPR:
      case SHR_EXPR:
      case BIT_AND_EXPR:
      case BIT_OR_EXPR:
      case BIT_XOR_EXPR:
        AnalyzeCalculation(static_cast<CalculatedExpression*>(expression), depth + 1);
        break;

      default:
        ProcessError(expression, L"Undefined expression");
        break;
      }
    }

    // check expression method call
    AnalyzeExpressionMethodCall(expression, depth + 1);

    // check cast
    AnalyzeCast(expression, depth + 1);

    --expression_depth;
  }
}

/****************************
 * Analyzes a ternary
 * conditional
 ****************************/
void ContextAnalyzer::AnalyzeConditional(Cond* conditional, const int depth)
{
#ifdef _DEBUG
  Debug(L"conditional expression", conditional->GetLineNumber(), depth);
#endif

  // check expressions
  AnalyzeExpression(conditional->GetCondExpression(), depth + 1);
  Expression* if_conditional = conditional->GetExpression();
  AnalyzeExpression(if_conditional, depth + 1);
  Expression* else_conditional = conditional->GetElseExpression();
  AnalyzeExpression(else_conditional, depth + 1);

  Type* if_type = GetExpressionType(if_conditional, depth + 1);
  Type* else_type = GetExpressionType(else_conditional, depth + 1);

  // validate types
  if(if_type && else_type) {
    if(if_type->GetType() == CLASS_TYPE && else_type->GetType() == CLASS_TYPE) {
      AnalyzeClassCast(if_type, else_conditional, depth + 1);
    }
    else if(if_type->GetType() != else_type->GetType() &&
            !((if_type->GetType() == CLASS_TYPE && else_type->GetType() == NIL_TYPE) ||
            (if_type->GetType() == NIL_TYPE && else_type->GetType() == CLASS_TYPE))) {
      ProcessError(conditional, L"'?' invalid type mismatch");
    }
    // set eval type
    conditional->SetEvalType(if_conditional->GetEvalType(), true);
    current_method->SetAndOr(true);
  }
  else {
    ProcessError(conditional, L"Invalid 'if' statement");
  }
}

/****************************
 * Analyzes a character literal
 ****************************/
void ContextAnalyzer::AnalyzeCharacterString(CharacterString* char_str, const int depth)
{
#ifdef _DEBUG
  Debug(L"character std::string literal", char_str->GetLineNumber(), depth);
#endif

  int var_start = -1;
  int str_start = 0;
  const std::wstring &str = char_str->GetString();

  // empty string segment
  if(str.empty()) {
    if(!char_str->AddSegment(L"")) {
      ProcessError(char_str, L"Invalid character sequence");
    }
  }
  else {
    // process segment
    for(size_t i = 0; i < str.size(); ++i) {
      // variable start
      if(str[i] == L'{' && i + 1 < str.size() && str[i + 1] == L'$') {
        var_start = (int)i;
        const std::wstring token = str.substr(str_start, i - str_start);
        
        if(!char_str->AddSegment(token)) {
          ProcessError(char_str, L"Invalid character sequence");
        }
      }

      // variable end
      if(var_start > -1) {
        if(str[i] == L'}') {
          const std::wstring token = str.substr(static_cast<std::basic_string<wchar_t, std::char_traits<wchar_t>, std::allocator<wchar_t>>::size_type>(var_start) + 2, i - var_start - 2);
          SymbolEntry* entry = GetEntry(token);
          if(entry) {
            AnalyzeCharacterStringVariable(entry, char_str, depth);
          }
          else {
            ProcessError(char_str, L"Undefined variable: '" + token + L"'");
          }
          // update
          var_start = -1;
          str_start = (int)i + 1;
        }
        else if(i + 1 == str.size()) {
          const std::wstring token = str.substr(static_cast<std::basic_string<wchar_t, std::char_traits<wchar_t>, std::allocator<wchar_t>>::size_type>(var_start) + 1, i - var_start);
          SymbolEntry* entry = GetEntry(token);
          if(entry) {
            AnalyzeCharacterStringVariable(entry, char_str, depth);
          }
          else {
            ProcessError(char_str, L"Undefined variable: '" + token + L"'");
          }
          // update
          var_start = -1;
          str_start = (int)i + 1;
        }
      }
      else if(i + 1 == str.size()) {
        var_start = (int)i;
        const std::wstring token = str.substr(str_start, i - str_start + 1);
        
        if(!char_str->AddSegment(token)) {
          ProcessError(char_str, L"Invalid character sequence");
        }
      }
    }
  }

  // tag literal strings
  std::vector<CharacterStringSegment*> segments = char_str->GetSegments();
  for(size_t i = 0; i < segments.size(); ++i) {
    if(segments[i]->GetType() == STRING) {
      int id = program->GetCharStringId(segments[i]->GetString());
      if(id > -1) {
        segments[i]->SetId(id);
      }
      else {
        segments[i]->SetId(char_str_index);
        program->AddCharString(segments[i]->GetString(), char_str_index);
        char_str_index++;
      }
    }
  }

  // create temporary variable for concat of strings and variables
  if(segments.size() > 1) {
    Type* type = TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.String");
    const std::wstring scope_name = current_method->GetName() + L":#_var_concat_#";
    SymbolEntry* entry = current_table->GetEntry(scope_name);
    if(!entry) {
      entry = TreeFactory::Instance()->MakeSymbolEntry(scope_name, type, false, true);
      current_table->AddEntry(entry, true);
    }
    char_str->SetConcat(entry);
  }

#ifndef _SYSTEM
  LibraryClass* lib_klass = linker->SearchClassLibraries(L"System.String", program->GetLibUses(current_class->GetFileName()));
  if(lib_klass) {
    lib_klass->SetCalled(true);
  }
  else {
    ProcessError(char_str, L"Invalid class name='System.String' system bundle 'lang.obl' excluded");
  }
#endif

  char_str->SetProcessed();
}

/****************************
 * Analyzes a static array
 ****************************/
void ContextAnalyzer::AnalyzeStaticArray(StaticArray* array, const int depth)
{
  // TOOD: support for 3d or 4d initialization
  if(array->GetDimension() > 2) {
    ProcessError(array, L"Invalid static array declaration.");
    return;
  }

  if(!array->IsMatchingTypes()) {
    ProcessError(array, L"Array element types do not match.");
    return;
  }

  if(!array->IsMatchingLenghts()) {
    ProcessError(array, L"Array dimension lengths do not match.");
    return;
  }

  Type* left_type;
  if(array->GetCastType()) {
    left_type = TypeFactory::Instance()->MakeType(array->GetCastType());
    EntryType right_type = array->GetType();

    if((left_type->GetType() == FLOAT_TYPE && right_type != FLOAT_TYPE) || (left_type->GetType() == INT_TYPE && right_type != INT_TYPE)) {
      ProcessError(array, L"Invalid array cast");
    }

    array->SetEvalType(left_type, false);
  }
  else {
    left_type = TypeFactory::Instance()->MakeType(array->GetType());
  }

  left_type->SetDimension(array->GetDimension());
  if(left_type->GetType() == CLASS_TYPE) {
    left_type->SetName(L"System.String");
  }
  array->SetEvalType(left_type, false);

  // ensure that element sizes match dimensions
  std::vector<Expression*> all_elements = array->GetAllElements()->GetExpressions();
  switch(array->GetType()) {
  case INT_TYPE: {
    int id = program->GetIntStringId(all_elements);
    if(id > -1) {
      array->SetId(id);
    }
    else {
      array->SetId(int_str_index);
      program->AddIntString(all_elements, int_str_index);
      int_str_index++;
    }
  }
    break;

  case FLOAT_TYPE: {
    int id = program->GetFloatStringId(all_elements);
    if(id > -1) {
      array->SetId(id);
    }
    else {
      array->SetId(float_str_index);
      program->AddFloatString(all_elements, float_str_index);
      float_str_index++;
    }
  }
    break;

  case BOOLEAN_TYPE: {
    int id = program->GetBoolStringId(all_elements);
    if(id > -1) {
      array->SetId(id);
    }
    else {
      array->SetId(bool_str_index);
      program->AddBoolString(all_elements, bool_str_index);
      bool_str_index++;
    }
  }
    break;

  case BYTE_TYPE: {
    int id = program->GetByteStringId(all_elements);
    if(id > -1) {
      array->SetId(id);
    }
    else {
      array->SetId(byte_str_index);
      program->AddByteString(all_elements, byte_str_index);
      byte_str_index++;
    }
  }
    break;

  case CHAR_TYPE: {
    // copy string elements
    std::wstring char_str;
    for(size_t i = 0; i < all_elements.size(); ++i) {
      char_str += static_cast<CharacterLiteral*>(all_elements[i])->GetValue();
    }
    // associate char string
    int id = program->GetCharStringId(char_str);
    if(id > -1) {
      array->SetId(id);
    }
    else {
      array->SetId(char_str_index);
      program->AddCharString(char_str, char_str_index);
      char_str_index++;
    }
  }
    break;

  case CLASS_TYPE:
    for(size_t i = 0; i < all_elements.size(); ++i) {
      AnalyzeCharacterString(static_cast<CharacterString*>(all_elements[i]), depth + 1);
    }
    break;

  default:
    ProcessError(array, L"Invalid type for static array.");
    break;
  }
}

/****************************
 * Analyzes a variable
 ****************************/
void ContextAnalyzer::AnalyzeVariable(Variable* variable, const int depth)
{
  AnalyzeVariable(variable, GetEntry(variable->GetName()), depth);
}

void ContextAnalyzer::AnalyzeVariable(Variable* variable, SymbolEntry* entry, const int depth)
{
  // explicitly defined variable
  if(entry) {
    entry->WasLoaded();
#ifdef _DEBUG
    std::wstring msg = L"variable reference: name='" + variable->GetName() + L"' local=" + (entry->IsLocal() ? L"true" : L"false") + L"' loaded=" + (entry->IsLoaded() ? L"true" : L"false");;
    Debug(msg, variable->GetLineNumber(), depth);
#endif

    const std::wstring &name = variable->GetName();
    if(HasProgramOrLibraryEnum(name) || HasProgramOrLibraryClass(name)) {
      ProcessError(variable, L"Variable '" + name + L"' already used to define a class, enum or function\n\tIf passing a function reference ensure the full signature is provided");
    }

    // associate variable and entry
    if(!variable->GetEvalType()) {
      Type* entry_type = entry->GetType();
      Expression* expression = variable;

      while(expression->GetMethodCall()) {
        AnalyzeExpressionMethodCall(expression, depth + 1);
        expression = expression->GetMethodCall();
      }

      Type* cast_type = expression->GetCastType();
      if(cast_type && cast_type->GetType() == CLASS_TYPE && entry_type && entry_type->GetType() == CLASS_TYPE && !HasProgramOrLibraryEnum(entry_type->GetName())) {
        AnalyzeClassCast(expression->GetCastType(), entry_type, expression, false, depth + 1);
      }

      variable->SetTypes(entry_type);
      variable->SetEntry(entry);
      entry->AddVariable(variable);
    }

    // array parameters
    ExpressionList* indices = variable->GetIndices();
    if(indices) {
      // check dimensions
      if(entry->GetType() && entry->GetType()->GetDimension() == (int)indices->GetExpressions().size()) {
        AnalyzeIndices(indices, depth + 1);
      }
      // note: internal edge case
      else if(!variable->IsInternalVariable()) {
        ProcessError(variable, L"Dimension size mismatch or uninitialized type");
      }
    }

    // static check
    if(InvalidStatic(entry)) {
      ProcessError(variable, L"Cannot reference an instance variable from this context");
    }
  }
  // lambda expressions
  else if(current_method && current_method->IsLambda()) {
    const std::wstring capture_scope_name = capture_method->GetName() + L':' + variable->GetName();
    SymbolEntry* capture_entry = capture_table->GetEntry(capture_scope_name);
    if(capture_entry) {
      if(capture_lambda->HasClosure(capture_entry)) {
        SymbolEntry* copy_entry = capture_lambda->GetClosure(capture_entry);
        variable->SetTypes(copy_entry->GetType());
        variable->SetEntry(copy_entry);
        copy_entry->AddVariable(variable);
      }
      else {
        const std::wstring var_scope_name = current_method->GetName() + L':' + variable->GetName();
        SymbolEntry* copy_entry = TreeFactory::Instance()->MakeSymbolEntry(var_scope_name, capture_entry->GetType(), false, false);
        symbol_table->GetSymbolTable(current_class->GetName())->AddEntry(copy_entry, true);

        variable->SetTypes(copy_entry->GetType());
        variable->SetEntry(copy_entry);
        copy_entry->AddVariable(variable);
        capture_lambda->AddClosure(copy_entry, capture_entry);
      }
    }
  }
  // type inferred variable
  else if(current_method) {
    const std::wstring scope_name = current_method->GetName() + L':' + variable->GetName();
    SymbolEntry* var_entry = TreeFactory::Instance()->MakeSymbolEntry(scope_name, TypeFactory::Instance()->MakeType(VAR_TYPE), false, true);
    current_table->AddEntry(var_entry, true);
    if(variable->IsAlt()) {
      var_entry->WasLoaded();
    }

    // link entry and variable
    variable->SetTypes(var_entry->GetType());
    variable->SetEntry(var_entry);
    var_entry->AddVariable(variable);
  }
  // undefined variable (at class level)
  else {
    ProcessError(variable, L"Undefined variable: '" + variable->GetName() + L"'");
  }

  if(variable->GetPreStatement() && variable->GetPostStatement()) {
    ProcessError(variable, L"Variable cannot have pre and pos operations");
  }
  else if(variable->GetPreStatement() && !variable->IsPreStatementChecked()) {
    OperationAssignment* pre_stmt = variable->GetPreStatement();
    variable->PreStatementChecked();
    AnalyzeAssignment(pre_stmt, pre_stmt->GetStatementType(), depth + 1);
  }
  else if(variable->GetPostStatement() && !variable->IsPostStatementChecked()) {
    OperationAssignment* post_stmt = variable->GetPostStatement();
    variable->PostStatementChecked();
    AnalyzeAssignment(post_stmt, post_stmt->GetStatementType(), depth + 1);
  }
}

/****************************
 * Analyzes an enum call
 ****************************/
void ContextAnalyzer::AnalyzeEnumCall(MethodCall* method_call, bool regress, const int depth) {
  const std::wstring variable_name = method_call->GetVariableName();
  const std::wstring method_name = method_call->GetMethodName();

  //
  // check library enum reference; fully qualified name
  //
  LibraryEnum* lib_eenum = linker->SearchEnumLibraries(variable_name + L"#" + method_name,
                                                       program->GetLibUses(current_class->GetFileName()));
  if(!lib_eenum) {
    lib_eenum = linker->SearchEnumLibraries(variable_name, program->GetLibUses(current_class->GetFileName()));
  }

  if(lib_eenum && method_call->GetMethodCall()) {
    const std::wstring item_name = method_call->GetMethodCall()->GetVariableName();
    ResolveEnumCall(lib_eenum, item_name, method_call);
  }
  else if(lib_eenum) {
    ResolveEnumCall(lib_eenum, method_name, method_call);
  }
  else {
    //
    // check program enum reference
    //
    std::wstring enum_name; std::wstring item_name;
    if(variable_name == current_class->GetName() && method_call->GetMethodCall()) {
      enum_name = method_name;
      item_name = method_call->GetMethodCall()->GetVariableName();
    }
    else {
      enum_name = variable_name;
      item_name = method_name;
    }

    // check fully qualified name
    Enum* eenum = SearchProgramEnums(enum_name + L"#" + item_name);
    if(eenum && method_call->GetMethodCall()) {
      item_name = method_call->GetMethodCall()->GetVariableName();
    }

    // local nested reference
    if(!eenum) {
      eenum = SearchProgramEnums(enum_name);
    }

    // local nested reference
    if(!eenum) {
      // standalone reference
      const size_t result = enum_name.find(L'#');
      if(result != std::wstring::npos) {
        const std::wstring var_name = enum_name.substr(0, result);
        eenum = SearchProgramEnums(var_name);
        if(eenum) {
          item_name = enum_name.substr(result + 1);
          method_call->SetEnumName(var_name, item_name);
        }
        else {
          ParsedBundle* bundle = program->GetBundle(var_name);
          if(bundle) {
            const std::wstring enum_child_name = enum_name.substr(result + 1);
            eenum = bundle->GetEnum(enum_child_name);
          }
        }
      }
      else {
        eenum = SearchProgramEnums(enum_name);
      }
    }

    // inner class enum
    if(!eenum) {
      const std::wstring inner_enum_name = current_class->GetName() + L'#' + enum_name;
      eenum = bundle->GetEnum(inner_enum_name);
    }

    if(eenum) {
      EnumItem* item = eenum->GetItem(item_name);
      if(item) {
        if(method_call->GetMethodCall()) {
          method_call->GetMethodCall()->SetEnumItem(item, eenum->GetName());
          method_call->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, eenum->GetName()), false);
          method_call->GetMethodCall()->SetEvalType(method_call->GetEvalType(), false);
        }
        else {
          method_call->SetEnumItem(item, eenum->GetName());
          method_call->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, eenum->GetName()), false);
        }
      }
      else {
        ProcessError(static_cast<Expression*>(method_call), L"Undefined enum item: '" + item_name + L"'");
      }
    }
    //
    // check '@self' reference
    //
    else if(enum_name == SELF_ID) {
      SymbolEntry* entry = GetEntry(item_name);
      if(entry && !entry->IsLocal() && !entry->IsStatic()) {
        AddMethodParameter(method_call, entry, depth + 1);
      }
      else {
        ProcessError(static_cast<Expression*>(method_call), L"Invalid '@self' reference for variable: '" + item_name + L"'");
      }
    }
    //
    // check '@parent' reference
    //
    else if(enum_name == PARENT_ID) {
      SymbolEntry* entry = GetEntry(item_name, true);
      if(entry && !entry->IsLocal() && !entry->IsStatic()) {
        AddMethodParameter(method_call, entry, depth + 1);
      }
      else {
        ProcessError(static_cast<Expression*>(method_call), L"Invalid '@parent' reference for variable: '" + item_name + L"'");
      }
    }
    //
    // check 'TypeOf(..)
    //
    else if(method_call->GetTypeOf()) {
      if(method_call->GetTypeOf()->GetType() != CLASS_TYPE || (method_call->GetEvalType() && method_call->GetEvalType()->GetType() != CLASS_TYPE)) {
        ProcessError(static_cast<Expression*>(method_call), L"Invalid 'TypeOf' check, only complex classes are supported");
      }

      AnalyzeVariable(method_call->GetVariable(), depth + 1);
      
      Type* type_of = method_call->GetTypeOf();
      if(SearchProgramClasses(type_of->GetName())) {
        Class* klass = SearchProgramClasses(type_of->GetName());
        klass->SetCalled(true);
        type_of->SetName(klass->GetName());
      }
      else if(linker->SearchClassLibraries(type_of->GetName(), program->GetLibUses(current_class->GetFileName()))) {
        LibraryClass* lib_klass = linker->SearchClassLibraries(type_of->GetName(), program->GetLibUses(current_class->GetFileName()));
        lib_klass->SetCalled(true);
        type_of->SetName(lib_klass->GetName());
      }
      else {
        ProcessError(static_cast<Expression*>(method_call), L"Invalid 'TypeOf' check, unknown class '" + type_of->GetName() + L"'");
      }
      method_call->SetEvalType(TypeFactory::Instance()->MakeType(BOOLEAN_TYPE), true);
    }
    else {
      ProcessError(static_cast<Expression*>(method_call), L"Undefined or incompatible enum type: '" + FormatTypeString(enum_name) + L"'");
    }
  }

  // next call
  if(regress) {
    AnalyzeExpressionMethodCall(method_call, depth + 1);
  }
}

/****************************
 * Analyzes a method call
 ****************************/
void ContextAnalyzer::AnalyzeMethodCall(MethodCall* method_call, const int depth)
{
#ifdef _DEBUG
  std::wstring msg = L"method/function call: class=" + method_call->GetVariableName() +
    L"; method=" + method_call->GetMethodName() + L"; call_type=" +
    ToString(method_call->GetCallType());
  Debug(msg, (static_cast<Expression*>(method_call))->GetLineNumber(), depth);
#endif

  //
  // new array call
  //
  if(method_call->GetCallType() == NEW_ARRAY_CALL) {
    AnalyzeNewArrayCall(method_call, depth);
  }
  //
  // enum call
  //
  else if(method_call->GetCallType() == ENUM_CALL) {
    AnalyzeEnumCall(method_call, true, depth);
  }
  //
  // parent call
  //
  else if(method_call->GetCallType() == PARENT_CALL) {
    AnalyzeParentCall(method_call, depth);
  }
  //
  // method/function
  //
  else {
    // track rouge return
    ++nested_call_depth;

    // static check
    const std::wstring variable_name = method_call->GetVariableName();
    SymbolEntry* entry = GetEntry(method_call, variable_name, depth);
    if(entry) {
      entry->WasLoaded();
    }

    if(entry && InvalidStatic(entry) && !capture_lambda) {
      ProcessError(static_cast<Expression*>(method_call), L"Cannot reference an instance variable from this context");
    }
    else if(method_call->GetVariable()) {
      AnalyzeVariable(method_call->GetVariable(), depth + 1);
    }
    else if(capture_lambda) {
      const std::wstring full_class_name = GetProgramOrLibraryClassName(variable_name);
      if(!HasProgramOrLibraryEnum(full_class_name) && !HasProgramOrLibraryClass(full_class_name)) {
        Variable* variable = TreeFactory::Instance()->MakeVariable(static_cast<Expression*>(method_call)->GetFileName(),
                                                                   static_cast<Expression*>(method_call)->GetLineNumber(),
                                                                   static_cast<Expression*>(method_call)->GetLinePosition(),
                                                                   full_class_name);
        AnalyzeVariable(variable, depth + 1);
        method_call->SetVariable(variable);
        entry = GetEntry(method_call, full_class_name, depth);
      }
    }
    
    // local call
    std::wstring encoding;
    Class* klass = AnalyzeProgramMethodCall(method_call, encoding, depth);
    if(klass) {
      if(method_call->IsFunctionDefinition()) {
        AnalyzeFunctionReference(klass, method_call, encoding, depth);
      }
      else if(!method_call->GetMethod() && !method_call->GetLibraryMethod()) {
        AnalyzeMethodCall(klass, method_call, false, encoding, depth);
      }
      
      // check for rouge return
      --nested_call_depth;
      RogueReturn(method_call);
      return;
    }
    // library call
    LibraryClass* lib_klass = AnalyzeLibraryMethodCall(method_call, encoding, depth);
    if(lib_klass) {
      if(method_call->IsFunctionDefinition()) {
        AnalyzeFunctionReference(lib_klass, method_call, encoding, depth);
      }
      else {
        AnalyzeMethodCall(lib_klass, method_call, false, encoding, false, depth);
      }

      // check for rouge return
      --nested_call_depth;
      RogueReturn(method_call);
      return;
    }

    if(entry) {
      if(method_call->GetVariable()) {
        bool is_enum_call = false;
        if(!AnalyzeExpressionMethodCall(method_call->GetVariable(), encoding,
           klass, lib_klass, is_enum_call)) {
          ProcessError(static_cast<Expression*>(method_call), L"Invalid class type or assignment");
        }
      }
      else {
        if(!AnalyzeExpressionMethodCall(entry, encoding, klass, lib_klass)) {
          ProcessError(static_cast<Expression*>(method_call), L"Invalid class type or assignment");
        }
      }

      // check method call
      if(klass) {
        AnalyzeMethodCall(klass, method_call, false, encoding, depth);
      }
      else if(lib_klass) {
        AnalyzeMethodCall(lib_klass, method_call, false, encoding, false, depth);
      }
      else {
        if(!variable_name.empty()) {
          ProcessError(static_cast<Expression*>(method_call), L"Undefined class: '" + variable_name + L"'");
        }
        else {
          ProcessError(static_cast<Expression*>(method_call), L"Undefined class or method call: '" + method_call->GetMethodName() + L"'");
        }
      }
    }
    else {
      if(!variable_name.empty()) {
        ProcessError(static_cast<Expression*>(method_call), L"Undefined class: '" + variable_name + L"'");
      }
      else {
        ProcessError(static_cast<Expression*>(method_call), L"Undefined class or method call: '" + method_call->GetMethodName() + L"'");
      }
    }

    // check for rouge return
    --nested_call_depth;
    RogueReturn(method_call);
  }
}

void ContextAnalyzer::ValidateConcretes(Type* from_concrete_type, Type* to_concrete_type, MethodCall* method_call)
{
  if(from_concrete_type->GetName() != to_concrete_type->GetName()) {
    ProcessError(static_cast<Expression*>(method_call), L"Invalid generic to concrete type mismatch '" + 
                 FormatTypeString(from_concrete_type->GetName()) + 
                 L"' to '" + FormatTypeString(to_concrete_type->GetName()) + L"'");
  }
}

void ContextAnalyzer::ValidateGenericConcreteMapping(const std::vector<Type*> concrete_types, LibraryClass* lib_klass, ParseNode* node)
{
  const std::vector<LibraryClass*> class_generics = lib_klass->GetGenericClasses();
  if(class_generics.size() != concrete_types.size()) {
    ProcessError(node, L"Cannot utilize an unqualified instance of class: '" + lib_klass->GetName() + L"'");
  }
  // check individual types
  if(class_generics.size() == concrete_types.size()) {
    for(size_t i = 0; i < concrete_types.size(); ++i) {
      Type* concrete_type = concrete_types[i];
      LibraryClass* class_generic = class_generics[i];
      if(class_generic->HasGenericInterface()) {
        const std::wstring backing_inf_name = class_generic->GetGenericInterface()->GetName();
        const std::wstring concrete_name = concrete_type->GetName();
        Class* inf_klass = nullptr; LibraryClass* inf_lib_klass = nullptr;
        if(GetProgramOrLibraryClass(concrete_type, inf_klass, inf_lib_klass)) {
          if(!ValidDownCast(backing_inf_name, inf_klass, inf_lib_klass)) {
            ProcessError(node, L"Concrete class: '" + concrete_name +
                         L"' is incompatible with backing class/interface '" + backing_inf_name + L"'");
          }
        }
        else {
          inf_klass = current_class->GetGenericClass(concrete_name);
          if(inf_klass) {
            if(!ValidDownCast(backing_inf_name, inf_klass, inf_lib_klass)) {
              ProcessError(node, L"Concrete class: '" + concrete_name +
                           L"' is incompatible with backing class/interface '" + backing_inf_name + L"'");
            }
          }
          else {
            ProcessError(node, L"Undefined class or interface: '" + FormatTypeString(concrete_name) + L"'");
          }
        }
      }
      else {
        const std::wstring concrete_type_name = concrete_type->GetName();
        Class* generic_klass = nullptr; LibraryClass* generic_lib_klass = nullptr;
        if(!GetProgramOrLibraryClass(concrete_type, generic_klass, generic_lib_klass) &&
           !current_class->GetGenericClass(concrete_type_name)) {
          ProcessError(node, L"Undefined class or interface: '" + FormatTypeString(concrete_type_name) + L"'");
        }
      }
    }
  }
}

void ContextAnalyzer::ValidateGenericConcreteMapping(const std::vector<Type*> concrete_types, Class* klass, ParseNode* node)
{
  const std::vector<Class*> class_generics = klass->GetGenericClasses();
  if(class_generics.size() != concrete_types.size()) {
    ProcessError(node, L"Cannot create an unqualified instance of class: '" + klass->GetName() + L"'");
  }
  // check individual types
  if(class_generics.size() == concrete_types.size()) {
    for(size_t i = 0; i < concrete_types.size(); ++i) {
      Type* concrete_type = concrete_types[i];
      ResolveClassEnumType(concrete_type);

      Class* class_generic = class_generics[i];
      if(class_generic->HasGenericInterface()) {
        const std::wstring backing_inf_name = GetProgramOrLibraryClassName(class_generic->GetGenericInterface()->GetName());
        const std::wstring concrete_name = concrete_type->GetName();
        Class* inf_klass = nullptr; LibraryClass* inf_lib_klass = nullptr;
        if(GetProgramOrLibraryClass(concrete_type, inf_klass, inf_lib_klass)) {
          if(!ValidDownCast(backing_inf_name, inf_klass, inf_lib_klass)) {
            ProcessError(node, L"Concrete class: '" + concrete_name +
                         L"' is incompatible with backing class/interface '" + backing_inf_name + L"'");
          }
        }
        else {
          inf_klass = current_class->GetGenericClass(concrete_name);
          if(inf_klass) {
            if(!ValidDownCast(backing_inf_name, inf_klass, inf_lib_klass)) {
              ProcessError(node, L"Concrete class: '" + concrete_name +
                           L"' is incompatible with backing class/interface '" + backing_inf_name + L"'");
            }
          }
          else {
            ProcessError(node, L"Undefined class or interface: '" + FormatTypeString(concrete_name) + L"'");
          }
        }
      }
      else {
        const std::wstring concrete_type_name = concrete_type->GetName();
        Class* generic_klass = nullptr; LibraryClass* generic_lib_klass = nullptr;
        if(!GetProgramOrLibraryClass(concrete_type, generic_klass, generic_lib_klass) &&
           !current_class->GetGenericClass(concrete_type_name)) {
          ProcessError(node, L"Undefined class or interface: '" + FormatTypeString(concrete_type_name) + L"'");
        }
      }
    }
  }
}

void ContextAnalyzer::ValidateGenericBacking(Type* type, const std::wstring backing_name, Expression * expression)
{
  const std::wstring concrete_name = type->GetName();
  Class* inf_klass = nullptr; LibraryClass* inf_lib_klass = nullptr;
  if(GetProgramOrLibraryClass(type, inf_klass, inf_lib_klass)) {
    if(!ValidDownCast(backing_name, inf_klass, inf_lib_klass) && !ClassEquals(backing_name, inf_klass, inf_lib_klass)) {
      ProcessError(expression, L"Concrete class: '" + concrete_name +
                   L"' is incompatible with backing class/interface '" + backing_name + L"'");
    }
  }
  else if((inf_klass = current_class->GetGenericClass(concrete_name))) {
    if(!ValidDownCast(backing_name, inf_klass, inf_lib_klass) && !ClassEquals(backing_name, inf_klass, inf_lib_klass)) {
      ProcessError(expression, L"Concrete class: '" + concrete_name +
                   L"' is incompatible with backing class/interface '" + backing_name + L"'");
    }
  }
  else if(expression->GetExpressionType() == METHOD_CALL_EXPR) {
    MethodCall* mthd_call = static_cast<MethodCall*>(expression);
    if(mthd_call->GetConcreteTypes().empty() && mthd_call->GetEntry()) {
      std::vector<Type*> concrete_types = mthd_call->GetEntry()->GetType()->GetGenerics();
      std::vector<Type*> concrete_copies;
      for(size_t i = 0; i < concrete_types.size(); ++i) {
        concrete_copies.push_back(TypeFactory::Instance()->MakeType(concrete_types[i]));
      }
      mthd_call->SetConcreteTypes(concrete_copies);
    }
    else {
      ProcessError(expression, L"Undefined class or interface: '" + FormatTypeString(concrete_name) + L"'");
    }
  }
  else {
    ProcessError(expression, L"Undefined class or interface: '" + FormatTypeString(concrete_name) + L"'");
  }
}
/****************************
 * Validates an expression
 * method call
 ****************************/
bool ContextAnalyzer::AnalyzeExpressionMethodCall(Expression* expression, std::wstring &encoding,
                                                  Class* &klass, LibraryClass* &lib_klass,
                                                  bool &is_enum_call)
{
  Type* type;
  // process cast
  if(expression->GetCastType()) {
    if(expression->GetExpressionType() == METHOD_CALL_EXPR && static_cast<MethodCall*>(expression)->GetVariable()) {
      while(expression->GetMethodCall()) {
        AnalyzeExpressionMethodCall(expression->GetMethodCall(), 0);
        expression = expression->GetMethodCall();
      }
      type = expression->GetEvalType();
    }
    else if(expression->GetExpressionType() == VAR_EXPR) {
      if(static_cast<Variable*>(expression)->GetIndices()) {
        ProcessError(expression, L"Unable to make a method call from an indexed array element");
        return false;
      }
      type = expression->GetCastType();
    }
    else {
      type = expression->GetCastType();
    }
  }
  // process non-cast
  else {
    type = expression->GetEvalType();
  }

  if(type) {
    const int dimension = IsScalar(expression, false) ? 0 : type->GetDimension();
    return AnalyzeExpressionMethodCall(type, dimension, encoding, klass, lib_klass, is_enum_call);
  }

  return false;
}

/****************************
 * Validates an expression
 * method call
 ****************************/
bool ContextAnalyzer::AnalyzeExpressionMethodCall(SymbolEntry* entry, std::wstring &encoding,
                                                  Class* &klass, LibraryClass* &lib_klass)
{
  Type* type = entry->GetType();
  if(type) {
    bool is_enum_call = false;
    return AnalyzeExpressionMethodCall(type, type->GetDimension(),
                                       encoding, klass, lib_klass, is_enum_call);
  }

  return false;
}

/****************************
 * Validates an expression
 * method call
 ****************************/
bool ContextAnalyzer::AnalyzeExpressionMethodCall(Type* type, const int dimension,
                                                  std::wstring &encoding, Class* &klass,
                                                  LibraryClass* &lib_klass, bool& is_enum_call)
{
  switch(type->GetType()) {
  case BOOLEAN_TYPE:
    klass = program->GetClass(BOOL_CLASS_ID);
    lib_klass = linker->SearchClassLibraries(BOOL_CLASS_ID, program->GetLibUses(current_class->GetFileName()));
    encoding = L"l";
    break;

  case VAR_TYPE:
  case NIL_TYPE:
    return false;

  case BYTE_TYPE:
    klass = program->GetClass(BYTE_CLASS_ID);
    lib_klass = linker->SearchClassLibraries(BYTE_CLASS_ID, program->GetLibUses(current_class->GetFileName()));
    encoding = L"b";
    break;

  case CHAR_TYPE:
    klass = program->GetClass(CHAR_CLASS_ID);
    lib_klass = linker->SearchClassLibraries(CHAR_CLASS_ID, program->GetLibUses(current_class->GetFileName()));
    encoding = L"c";
    break;

  case INT_TYPE:
    klass = program->GetClass(INT_CLASS_ID);
    lib_klass = linker->SearchClassLibraries(INT_CLASS_ID, program->GetLibUses(current_class->GetFileName()));
    encoding = L"i";
    break;

  case FLOAT_TYPE:
    klass = program->GetClass(FLOAT_CLASS_ID);
    lib_klass = linker->SearchClassLibraries(FLOAT_CLASS_ID, program->GetLibUses(current_class->GetFileName()));
    encoding = L"f";
    break;

  case CLASS_TYPE: {
    if(dimension > 0 && type->GetDimension() > 0) {
      klass = program->GetClass(BASE_ARRAY_CLASS_ID);
      lib_klass = linker->SearchClassLibraries(BASE_ARRAY_CLASS_ID, program->GetLibUses(current_class->GetFileName()));
      encoding = L"o.System.Base";
    }
    else {
      const std::wstring &cls_name = type->GetName();
      klass = SearchProgramClasses(cls_name);
      lib_klass = linker->SearchClassLibraries(cls_name, program->GetLibUses(current_class->GetFileName()));

      if(!klass && !lib_klass) {
        if(HasProgramOrLibraryEnum(cls_name)) {
          klass = program->GetClass(INT_CLASS_ID);
          lib_klass = linker->SearchClassLibraries(INT_CLASS_ID, program->GetLibUses(current_class->GetFileName()));
          encoding = L"i,";
          is_enum_call = true;
        }
      }
    }
  }
    break;

  default:
    return false;
  }

  // dimension
  for(int i = 0; i < dimension; ++i) {
    encoding += L'*';
  }

  if(type->GetType() != CLASS_TYPE) {
    encoding += L",";
  }

  return true;
}

/****************************
 * Analyzes a new array method
 * call
 ****************************/
void ContextAnalyzer::AnalyzeNewArrayCall(MethodCall* method_call, const int depth)
{
  Class* generic_class = current_class->GetGenericClass(method_call->GetEvalType()->GetName());
  if(generic_class && generic_class->HasGenericInterface() && method_call->GetEvalType()) {
    const int dimension = method_call->GetEvalType()->GetDimension();
    method_call->SetEvalType(generic_class->GetGenericInterface(), false);
    method_call->GetEvalType()->SetDimension(dimension);
  }

  // get parameters
  ExpressionList* call_params = method_call->GetCallingParameters();
  AnalyzeExpressions(call_params, depth + 1);
  // check indexes
  std::vector<Expression*> expressions = call_params->GetExpressions();
  if(expressions.size() == 0) {
    ProcessError(static_cast<Expression*>(method_call), L"Empty array index");
  }
  
  // TODO: check for dimension size of 1, looking at type
  else if(method_call->GetEvalType() && expressions.size() == 1 && (expressions[0]->GetExpressionType() == VAR_EXPR || expressions[0]->GetExpressionType() == STAT_ARY_EXPR) && expressions[0]->GetEvalType() && expressions[0]->GetEvalType()->GetDimension() == 1) {
    Type* left_type = expressions[0]->GetEvalType();
    Type* right_type = method_call->GetEvalType();

    if(left_type->GetType() != right_type->GetType()) {
      ProcessError(static_cast<Expression*>(method_call), L"Invalid array parameter type");
    }
  }
  else {
    // validate array parameters
    for(size_t i = 0; i < expressions.size(); ++i) {
      Expression* expression = expressions[i];
      AnalyzeExpression(expression, depth + 1);
      Type* type = GetExpressionType(expression, depth + 1);
      if(type) {
        switch(type->GetType()) {
        case BYTE_TYPE:
        case CHAR_TYPE:
        case INT_TYPE:
          break;

        case CLASS_TYPE:
          if(!IsEnumExpression(expression)) {
            ProcessError(expression, L"Array index type must be an Integer, Char, Byte or Enum");
          }
          break;

        default:
          ProcessError(expression, L"Array index type must be an Integer, Char, Byte or Enum");
          break;
        }
      }
    }
  }

  // generic array type
  if(method_call->HasConcreteTypes() && method_call->GetEvalType()) {
    Class* generic_klass = nullptr; LibraryClass* generic_lib_klass = nullptr;
    if(GetProgramOrLibraryClass(method_call->GetEvalType(), generic_klass, generic_lib_klass)) {
      const std::vector<Type*> concrete_types = GetConcreteTypes(method_call);
      if(generic_klass) {
        const std::vector<Class*> generic_classes = generic_klass->GetGenericClasses();
        if(concrete_types.size() == generic_classes.size()) {
          method_call->GetEvalType()->SetGenerics(concrete_types);
        }
        else {
          if(concrete_types.empty()) {
            for(const auto& generic_class : generic_classes) {
              const std::wstring generic_name = generic_class->GetName();
              if(!HasProgramOrLibraryClass(generic_name)) {
                ProcessError(static_cast<Expression*>(method_call), L"Invalid concrete type");
              }
            }
          }
          else {
            ProcessError(static_cast<Expression*>(method_call), L"Generic to concrete size mismatch");
          }
        }
      }
      else {
        const std::vector<LibraryClass*> generic_classes = generic_lib_klass->GetGenericClasses();
        if(concrete_types.size() == generic_classes.size()) {
          method_call->GetEvalType()->SetGenerics(concrete_types);
        }
        else {
          if(concrete_types.empty()) {
            for(const auto& generic_class : generic_classes) {
              const std::wstring generic_name = generic_class->GetName();
              if(!HasProgramOrLibraryClass(generic_name)) {
                ProcessError(static_cast<Expression*>(method_call), L"Invalid concrete type");
              }
            }
          }
          else {
            ProcessError(static_cast<Expression*>(method_call), L"Generic to concrete size mismatch");
          }
        }
      }
    }
  }
}

/*********************************
 * Analyzes a parent method call
 *********************************/
void ContextAnalyzer::AnalyzeParentCall(MethodCall* method_call, const int depth)
{
  // get parameters
  ExpressionList* call_params = method_call->GetCallingParameters();
  AnalyzeExpressions(call_params, depth + 1);

  Class* parent = current_class->GetParent();
  if(parent) {
    std::wstring encoding;
    AnalyzeMethodCall(parent, method_call, false, encoding, depth);
  }
  else {
    LibraryClass* lib_parent = current_class->GetLibraryParent();
    if(lib_parent) {
      std::wstring encoding;
      AnalyzeMethodCall(lib_parent, method_call, false, encoding, true, depth);
    }
    else {
      ProcessError(static_cast<Expression*>(method_call), L"Class has no parent");
    }
  }
}

/****************************
 * Analyzes a method call
 ****************************/
void ContextAnalyzer::AnalyzeExpressionMethodCall(Expression* expression, const int depth)
{
  MethodCall* method_call = expression->GetMethodCall();
  if(method_call) {
    if(method_call->GetCallType() == ENUM_CALL) {
      if(expression->GetExpressionType() == METHOD_CALL_EXPR) {
        AnalyzeEnumCall(static_cast<MethodCall*>(expression), false, depth + 1);
      }
      else {
        ProcessError(static_cast<Expression*>(method_call), L"Invalid enum reference");
      }
    }
    else {
      // track rouge return
      ++nested_call_depth;

      std::wstring encoding;
      Class* klass = nullptr;
      LibraryClass* lib_klass = nullptr;

      // check expression class
      bool is_enum_call = false;
      if(!AnalyzeExpressionMethodCall(expression, encoding, klass, lib_klass, is_enum_call) && !AnalyzeExpressionMethodCall(method_call, encoding, klass, lib_klass, is_enum_call)) {
          ProcessError(static_cast<Expression*>(method_call), L"Invalid class type or assignment");
      }
      method_call->SetEnumCall(is_enum_call);

      // check methods
      if(klass) {
        AnalyzeMethodCall(klass, method_call, true, encoding, depth);
      }
      else if(lib_klass) {
        AnalyzeMethodCall(lib_klass, method_call, true, encoding, false, depth);
      }
      else {
        if(expression->GetEvalType()) {
          std::wstring error_msg;

          if(!expression->GetEvalType()->GetName().empty()) {
            error_msg = L"Undefined class reference: '" + expression->GetEvalType()->GetName();
          }
          else if(expression->GetExpressionType() == METHOD_CALL_EXPR) {
            MethodCall* cls_method = static_cast<MethodCall*>(expression);

            std::wstring cls_name;
            if(cls_method->GetMethod()) {
              cls_name = cls_method->GetMethod()->GetClass()->GetName() + L"->";
            }
            else if(cls_method->GetLibraryMethod()) {
              cls_name = cls_method->GetLibraryMethod()->GetLibraryClass()->GetName() + L"->";
            }

            error_msg = L"Undefined function/method call: '" + cls_name + cls_method->GetMethodName() + L"(..)'";
          }
          ProcessError(static_cast<Expression*>(method_call), error_msg + L"\n\tIf external reference to generic ensure it has been typed");
        }
        else {
          ProcessError(static_cast<Expression*>(method_call), L"Undefined class reference.\n\tIf external reference to generic ensure it has been typed");
        }
      }

      // check for rouge return
      --nested_call_depth;
      RogueReturn(method_call);
    }
  }
}

void ContextAnalyzer::RogueReturn(MethodCall* method_call)
{
  // test if call is mapped to method
  if(!nested_call_depth && !in_assignment && !in_return && !expression_depth && method_call) {
    // get the last method call
    while(method_call->GetMethodCall()) {
      method_call = method_call->GetMethodCall();
    }

    if(method_call->GetCallType() == ENUM_CALL) {
      method_call->SetRougeReturn(instructions::INT_TYPE);
      return;
    }

    Type* rtrn = nullptr;
    if(method_call->GetMethod()) {
      rtrn = method_call->GetMethod()->GetReturn();
    }
    else if(method_call->GetLibraryMethod()) {
      rtrn = method_call->GetLibraryMethod()->GetReturn();
    }
    else if(method_call->IsFunctionalCall()) {
      rtrn = method_call->GetFunctionalEntry()->GetType()->GetFunctionReturn();
    }

    if(rtrn && rtrn->GetType() != frontend::NIL_TYPE) {
      if(rtrn->GetDimension() > 0) {
        method_call->SetRougeReturn(instructions::INT_TYPE);
        return;
      }
      else if(method_call->GetCallType() != NEW_INST_CALL) {
        switch(rtrn->GetType()) {
        case frontend::BOOLEAN_TYPE:
        case frontend::BYTE_TYPE:
        case frontend::CHAR_TYPE:
        case frontend::INT_TYPE:
        case frontend::CLASS_TYPE:
          method_call->SetRougeReturn(instructions::INT_TYPE);
          return;

        case frontend::FLOAT_TYPE:
          method_call->SetRougeReturn(instructions::FLOAT_TYPE);
          return;

        case frontend::FUNC_TYPE:
          method_call->SetRougeReturn(instructions::FUNC_TYPE);
          return;

        default:
          break;
        }
      }

      method_call->SetRougeReturn(instructions::NIL_TYPE);
    }
  }
}

/*********************************
 * Analyzes a method call.  This
 * is method call within the source
 * program.
 *********************************/
Class* ContextAnalyzer::AnalyzeProgramMethodCall(MethodCall* method_call, std::wstring &encoding, const int depth)
{
  Class* klass = nullptr;

  // method within the same class
  const std::wstring variable_name = method_call->GetVariableName();
  if(method_call->GetMethodName().size() == 0) {
    klass = SearchProgramClasses(current_class->GetName());
  }
  else {
    // external method
    SymbolEntry* entry = GetEntry(method_call, variable_name, depth);
    if(entry && entry->GetType() && entry->GetType()->GetType() == CLASS_TYPE) {
      if(entry->GetType()->GetDimension() > 0 &&
        (!method_call->GetVariable() ||
         !method_call->GetVariable()->GetIndices())) {
        klass = program->GetClass(BASE_ARRAY_CLASS_ID);
        encoding = L"o.System.Base";
        for(int i = 0; i < entry->GetType()->GetDimension(); ++i) {
          encoding += L"*";
        }
        encoding += L",";

      }
      else if(method_call->GetVariable() && method_call->GetVariable()->GetCastType() &&
              method_call->GetVariable()->GetCastType()->GetType() == CLASS_TYPE) {
        klass = SearchProgramClasses(method_call->GetVariable()->GetCastType()->GetName());
      }
      else {
        klass = SearchProgramClasses(entry->GetType()->GetName());
      }
    }
    // static method call
    if(!klass) {
      klass = SearchProgramClasses(variable_name);
    }
  }

  if(method_call->GetCastType() && method_call->GetCastType()->GetType() == CLASS_TYPE) {
    AnalyzeVariableCast(method_call->GetCastType(), method_call);
  }

  return klass;
}

/*********************************
 * Analyzes a method call.  This
 * is method call within a linked
 * library
 *********************************/
LibraryClass* ContextAnalyzer::AnalyzeLibraryMethodCall(MethodCall* method_call, std::wstring &encoding, const int depth)
{
  LibraryClass* klass = nullptr;
  const std::wstring variable_name = method_call->GetVariableName();

  // external method
  SymbolEntry* entry = GetEntry(method_call, variable_name, depth);
  if(entry && entry->GetType() && entry->GetType()->GetType() == CLASS_TYPE) {
    // array type
    if(entry->GetType()->GetDimension() > 0 &&
      (!method_call->GetVariable() ||
       !method_call->GetVariable()->GetIndices())) {

      klass = linker->SearchClassLibraries(BASE_ARRAY_CLASS_ID, program->GetLibUses(current_class->GetFileName()));
      encoding = L"o.System.Base";
      for(int i = 0; i < entry->GetType()->GetDimension(); ++i) {
        encoding += L"*";
      }
      encoding += L",";
    }
    // cast type
    else if(method_call->GetVariable() && method_call->GetVariable()->GetCastType() &&
            method_call->GetVariable()->GetCastType()->GetType() == CLASS_TYPE) {
      klass = linker->SearchClassLibraries(method_call->GetVariable()->GetCastType()->GetName(),
                                           program->GetLibUses(current_class->GetFileName()));
      method_call->SetTypes(entry->GetType());
    }
    else {
      klass = linker->SearchClassLibraries(entry->GetType()->GetName(), program->GetLibUses(current_class->GetFileName()));
      if(!klass && current_class->HasGenerics()) {
        Class* generic_class = current_class->GetGenericClass(entry->GetType()->GetName());
        if(generic_class) {
          return linker->SearchClassLibraries(L"System.Base", program->GetLibUses(current_class->GetFileName()));
        }
      }
    }
  }
  // static method call
  if(!klass) {
    klass = linker->SearchClassLibraries(variable_name, program->GetLibUses(current_class->GetFileName()));
  }

  if(method_call->GetCastType() && method_call->GetCastType()->GetType() == CLASS_TYPE) {
    AnalyzeVariableCast(method_call->GetCastType(), method_call);
  }

  return klass;
}

/*********************************
 * Resolve method call parameter
 *********************************/
int ContextAnalyzer::MatchCallingParameter(Expression* calling_param, Type* method_type, Class* klass, 
                                           LibraryClass* lib_klass, const int depth)
{
  // get calling type
  Type* calling_type = GetExpressionType(calling_param, depth + 1);

  // determine if there's a mapping from calling type to method type
  if(calling_type && method_type) {
    // processing an array
    if(!IsScalar(calling_param)) {
      if(calling_type->GetType() == method_type->GetType()) {
        // class/enum arrays
        if(calling_type->GetType() == CLASS_TYPE &&
           IsClassEnumParameterMatch(calling_type, method_type) &&
           calling_type->GetDimension() == method_type->GetDimension()) {
          return 0;
        }
        // basic arrays
        else if(calling_type->GetDimension() == method_type->GetDimension()) {
          return 0;
        }
      }

      return -1;
    }
    else {
      // look for an exact match
      if(calling_type->GetType() != CLASS_TYPE && method_type->GetType() != CLASS_TYPE &&
         calling_type->GetType() != FUNC_TYPE && method_type->GetType() != FUNC_TYPE &&
         method_type->GetDimension() == 0 && calling_type->GetType() == method_type->GetType()) {
        return 0;
      }

      // looks for a relative match
      if(method_type->GetDimension() == 0) {
        switch(calling_type->GetType()) {
        case NIL_TYPE:
          if(method_type->GetType() == CLASS_TYPE) {
            return 1;
          }
          return -1;

        case BOOLEAN_TYPE:
          return method_type->GetType() == BOOLEAN_TYPE ? 0 : -1;

        case BYTE_TYPE:
        case CHAR_TYPE:
        case INT_TYPE:
        case FLOAT_TYPE:
          switch(method_type->GetType()) {
          case BYTE_TYPE:
          case CHAR_TYPE:
          case INT_TYPE:
          case FLOAT_TYPE:
            return 1;

          default:
            return -1;
          }

        case CLASS_TYPE: {
          if(method_type->GetType() == CLASS_TYPE) {
            // calculate exact match
            if(IsClassEnumParameterMatch(calling_type, method_type)) {
              if(calling_type->HasGenerics() || method_type->HasGenerics()) {
                if(CheckGenericEqualTypes(calling_type, method_type, calling_param, true)) {
                  return 0;
                }
                return -1;
              }
              return 0;
            }
            // calculate relative match
            const std::wstring &from_klass_name = calling_type->GetName();
            Class* from_klass = SearchProgramClasses(from_klass_name);
            LibraryClass* from_lib_klass = linker->SearchClassLibraries(from_klass_name, program->GetLibUses(current_class->GetFileName()));

            const std::wstring &to_klass_name = method_type->GetName();
            Class* to_klass = SearchProgramClasses(to_klass_name);
            if(to_klass) {
              return ValidDownCast(to_klass->GetName(), from_klass, from_lib_klass) ? 1 : -1;
            }

            LibraryClass* to_lib_klass = linker->SearchClassLibraries(to_klass_name, program->GetLibUses(current_class->GetFileName()));
            if(to_lib_klass) {
              return ValidDownCast(to_lib_klass->GetName(), from_klass, from_lib_klass) ? 1 : -1;
            }
          }
          else if(method_type->GetType() == INT_TYPE) {
            // program
            if(program->GetEnum(calling_type->GetName()) ||
               linker->SearchEnumLibraries(calling_type->GetName(), program->GetLibUses())) {
              return 1;
            }
          }

          return -1;
        }

        case FUNC_TYPE: {
          const std::wstring calling_type_name = calling_type->GetName();
          std::wstring method_type_name = method_type->GetName();
          if(method_type_name.size() == 0) {
            AnalyzeVariableFunctionParameters(method_type, calling_param);
            method_type_name = L"m." + EncodeFunctionType(method_type->GetFunctionParameters(),
                                                          method_type->GetFunctionReturn());
            method_type->SetName(method_type_name);
          }

          return calling_type_name == method_type_name ? 0 : -1;
        }

        case ALIAS_TYPE:
        case VAR_TYPE:
          return -1;
        }
      }
    }
  }

  return -1;
}

/****************************
 * Resolves method calls
 ****************************/
Method* ContextAnalyzer::ResolveMethodCall(Class* klass, MethodCall* method_call, const int depth)
{
  const std::wstring method_name = method_call->GetMethodName();
  ExpressionList* calling_params = method_call->GetCallingParameters();
  std::vector<Expression*> expr_params = calling_params->GetExpressions();
  std::vector<Method*> candidates = klass->GetAllUnqualifiedMethods(method_name);

  // save all valid candidates
  std::vector<MethodCallSelection*> matches;
  for(size_t i = 0; i < candidates.size(); ++i) {
    // match parameter sizes
    Method* candidate = candidates[i];
    std::vector<Declaration*> method_parms = candidate->GetDeclarations()->GetDeclarations();

    if(expr_params.size() == method_parms.size()) {
      // box and unbox parameters
      std::vector<Expression*> boxed_resolved_params;
      for(size_t j = 0; j < expr_params.size(); ++j) {
        // cannot be set to method, need to preserve test against other selections
        Expression* expr_param = expr_params[j];
        Type* expr_type = expr_param->GetEvalType();
        Type* method_type = ResolveGenericType(method_parms[j]->GetEntry()->GetType(), method_call, klass, nullptr, false);

        Expression* boxed_param = BoxExpression(method_type, expr_param, depth);
        if(boxed_param) {
          boxed_resolved_params.push_back(boxed_param);
        }
        else if((boxed_param = UnboxingExpression(expr_type, expr_param, false, depth))) {
          boxed_resolved_params.push_back(boxed_param);
        }
        // add default
        if(!boxed_param) {
          boxed_resolved_params.push_back(expr_param);
        }
      }

#ifdef _DEBUG
      assert(boxed_resolved_params.size() == expr_params.size());
#endif

      MethodCallSelection* match = new MethodCallSelection(candidate, boxed_resolved_params);
      for(size_t j = 0; j < boxed_resolved_params.size(); ++j) {
        Type* method_type = ResolveGenericType(method_parms[j]->GetEntry()->GetType(), method_call, klass, nullptr, false);
        // add parameter match
        const int compare = MatchCallingParameter(boxed_resolved_params[j], method_type, klass, nullptr, depth);
        match->AddParameterMatch(compare);
      }
      matches.push_back(match);
    }
  }

  // evaluate matches
  MethodCallSelector selector(method_call, matches);
  Method* method = selector.GetSelection();
  if(method) {
#ifdef _DIAG_LIB
    // associate method call with method for diagnostics
    method->AddMethodCall(method_call);
#endif

    // check casts on final candidate
    std::vector<Declaration*> method_parms = method->GetDeclarations()->GetDeclarations();
    for(size_t j = 0; j < expr_params.size(); ++j) {
      Expression* expression = expr_params[j];
      while(expression->GetMethodCall()) {
        AnalyzeExpressionMethodCall(expression, depth + 1);
        expression = expression->GetMethodCall();
      }
      // erase/resolve type
      Type* left = ResolveGenericType(method_parms[j]->GetEntry()->GetType(), method_call, klass, nullptr, false);
      AnalyzeRightCast(left, expression, IsScalar(expression), depth + 1);
    }
  }
  else {
    std::vector<Method*> alt_mthds = selector.GetAlternativeMethods();
    Method* derived_method = DerivedLambdaFunction(alt_mthds);
    if(derived_method) {
      return derived_method;
    }
    else if(alt_mthds.size()) {
      alt_error_method_names = selector.GetAlternativeMethodNames();
    }
  }

  return method;
}

/****************************
 * Analyzes a method call.  This
 * is method call within the source
 * program.
 ****************************/
void ContextAnalyzer::AnalyzeMethodCall(Class* klass, MethodCall* method_call, bool is_expr, std::wstring &encoding, const int depth)
 {
#ifdef _DEBUG
  GetLogger() << L"Checking program class call: |" << klass->GetName() << L':' 
    << (method_call->GetMethodName().size() > 0 ? method_call->GetMethodName() : method_call->GetVariableName())
    << L"|" << std::endl;
#endif

  // calling parameters
  ExpressionList* call_params = method_call->GetCallingParameters();

  // lambda inferred type
  CheckLambdaInferredTypes(method_call, depth + 1);

  AnalyzeExpressions(call_params, depth + 1);

  // note: find system based methods and call with function parameters (i.e. $Int, $Float)
  Method* method = ResolveMethodCall(klass, method_call, depth);
  if(!method) {
    const std::wstring encoded_name = klass->GetName() + L':' + method_call->GetMethodName() + L':' + encoding +
      EncodeMethodCall(method_call->GetCallingParameters(), depth);
    method = klass->GetMethod(encoded_name);
  }

  if(!method) {
    // check static classes
    const std::vector<Type*> static_uses = program->GetStaticUses(current_class->GetFileName());
    if(!static_uses.empty()) {
      for(auto& static_use : static_uses) {
        std::wstring class_name;

        switch(static_use->GetType()) {
        case BYTE_TYPE:
          class_name = L"System.$Byte";
          break;

        case CHAR_TYPE:
          class_name = L"System.$Char";
          break;

        case INT_TYPE:
          class_name = L"System.$Int";
          break;

        case FLOAT_TYPE:
          class_name = L"System.$Float";
          break;

        case BOOLEAN_TYPE:
          class_name = L"System.$Bool";
          break;

        case CLASS_TYPE:
          class_name = static_use->GetName();
          break;

        default:
          break;
        }

        Class* static_klass = nullptr; LibraryClass* static_lib_klass = nullptr;
        if(GetProgramOrLibraryClass(class_name, static_klass, static_lib_klass)) {
          method_call->SetOriginalClass(klass);
          std::wstring encoding;

          // program class
          if(static_klass) {
            if(!use_static_check) {
              use_static_check = true;
              method_call->SetVariableName(static_klass->GetName());
              AnalyzeMethodCall(static_klass, method_call, is_expr, encoding, depth + 1);
              use_static_check = false;
            }
            
            // found, exit
            if(method_call->GetMethod()) {
              return;
            }
          }
          // library class
          else {
            if(!use_static_check) {
              use_static_check = true;

              method_call->SetVariableName(static_lib_klass->GetName());
              const std::wstring encoded_name = static_lib_klass->GetName() + L':' + method_call->GetMethodName() + L':' +
                encoding + EncodeMethodCall(method_call->GetCallingParameters(), depth);
              LibraryMethod* method = static_lib_klass->GetMethod(encoded_name);
              if(method) {
                AnalyzeMethodCall(static_lib_klass, method_call, is_expr, encoding, true, depth + 1); 
              }

              use_static_check = false;
            }

            // found, exit
            if(method_call->GetLibraryMethod()) {
              return;
            }
          }
        }
        else {
          ProcessError(program->GetFileName(), L"Undefined static class: '" + class_name + L"'");
        }
      }
    }

    // proceed with class resolution 
    if(klass->GetParent()) {
      Class* parent = klass->GetParent();
      method_call->SetOriginalClass(klass);
      std::wstring encoding;
      AnalyzeMethodCall(parent, method_call, is_expr, encoding, depth + 1);
      return;
    }
    else if(klass->GetLibraryParent()) {
      LibraryClass* lib_parent = klass->GetLibraryParent();
      method_call->SetOriginalClass(klass);
      std::wstring encoding;
      AnalyzeMethodCall(lib_parent, method_call, is_expr, encoding, true, depth + 1);
      return;
    }
    else {
      AnalyzeVariableFunctionCall(method_call, depth + 1);
      return;
    }
  }

  // found program method
  if(method) {
    // look for implicit casts
    std::vector<Declaration*> mthd_params = method->GetDeclarations()->GetDeclarations();
    std::vector<Expression*> expressions = call_params->GetExpressions();

#ifndef _SYSTEM
    if(mthd_params.size() != expressions.size()) {
      ProcessError(static_cast<Expression*>(method_call), L"Invalid method call context");
      return;
    }
#endif

    for(size_t i = 0; i < mthd_params.size(); ++i) {
      AnalyzeDeclaration(mthd_params[i], klass, depth + 1);
    }

    Expression* expression;
    for(size_t i = 0; i < expressions.size(); ++i) {
      expression = expressions[i];
      // find eval type
      while(expression->GetMethodCall()) {
        AnalyzeExpressionMethodCall(expression, depth + 1);
        expression = expression->GetMethodCall();
      }
      // check cast
      if(mthd_params[i]->GetEntry()) {
        if(expression->GetExpressionType() == METHOD_CALL_EXPR && expression->GetEvalType() &&
           expression->GetEvalType()->GetType() == NIL_TYPE) {
          ProcessError(static_cast<Expression*>(method_call), L"Invalid operation with 'Nil' value");
        }
        // check generic parameters for call
        Type* left = ResolveGenericType(mthd_params[i]->GetEntry()->GetType(), method_call, klass, nullptr, false);
        AnalyzeRightCast(left, expression->GetEvalType(), expression, IsScalar(expression), depth + 1);
      }
    }

    // public/private check
    if(method->GetClass() != current_method->GetClass()) {
      const bool check_call_access = method->IsStatic() && method->GetMethodType() == PRIVATE_METHOD;
      if(check_call_access || (!method->IsStatic() && (method->GetMethodType() == PRIVATE_METHOD || method->GetMethodType() == NEW_PRIVATE_METHOD))) {
        bool found = false;
        Class* method_class = method->GetClass();
        Class* parent = current_method->GetClass()->GetParent();
        while(parent && !found) {
          if(method_class == parent) {
            found = true;
          }
          // update
          parent = parent->GetParent();
        }

        if(!found) {
          ProcessError(static_cast<Expression*>(method_call), L"Cannot reference private method/function '" + method->GetUserName() + L"' from this context");
        }
      }
    }
    
    // check private class scope
    const std::wstring bundle_name = klass->GetBundleName();
    if(!klass->IsPublic() && current_class->GetBundleName() != bundle_name) {
      ProcessError(static_cast<Expression*>(method_call), L"Cannot access private class '" + klass->GetName() + L"' from this bundle scope");
    }

    // static check
    if(!is_expr && InvalidStatic(method_call, method)) {
      ProcessError(static_cast<Expression*>(method_call), L"Cannot reference an instance method from this context");
    }
    
    // cannot create an instance of a virtual class
    if((method->GetMethodType() == NEW_PUBLIC_METHOD || method->GetMethodType() == NEW_PRIVATE_METHOD) &&
       klass->IsVirtual() && current_class->GetParent() != klass) {
      ProcessError(static_cast<Expression*>(method_call), L"Cannot create an instance of a virtual class or interface, check the constructor");
    }
    
    // associate method
    klass->SetCalled(true);
    method_call->SetOriginalClass(klass);
    method_call->SetMethod(method);
    
    // map Generic to concrete types
    const bool is_new = method->GetMethodType() == NEW_PUBLIC_METHOD || method->GetMethodType() == NEW_PRIVATE_METHOD;
    const bool same_cls_return = ClassEquals(method->GetReturn()->GetName(), klass, nullptr);
    if((is_new || same_cls_return) && klass->HasGenerics()) {
      const std::vector<Class*> class_generics = klass->GetGenericClasses();      
      std::vector<Type*> concrete_types = GetConcreteTypes(method_call);
      if(class_generics.size() != concrete_types.size()) {
        ProcessError(static_cast<Expression*>(method_call), L"Cannot create an unqualified instance of class: '" + klass->GetName() + L"'");
      }
      // check individual types
      if(class_generics.size() == concrete_types.size()) {
        for(size_t i = 0; i < concrete_types.size(); ++i) {
          Type* concrete_type = concrete_types[i];
          Class* class_generic = class_generics[i];
          if(class_generic->HasGenericInterface()) {
            Type* backing_type = class_generic->GetGenericInterface();
            // backing type
            ResolveClassEnumType(backing_type);
            const std::wstring backing_name = backing_type->GetName();
            // concrete type
            ResolveClassEnumType(concrete_type);
            // validate backing
            ValidateGenericBacking(concrete_type, backing_name, static_cast<Expression*>(method_call));
          }
        }
      }
      method_call->GetEvalType()->SetGenerics(concrete_types);
    }

    // resolve generic to concrete, if needed
    Type* eval_type = method_call->GetEvalType();
    if(klass->HasGenerics()) {
      eval_type = ResolveGenericType(eval_type, method_call, klass, nullptr, true);
      method_call->SetEvalType(eval_type, false);
    }

    if(eval_type->GetType() == CLASS_TYPE && !ResolveClassEnumType(eval_type, klass)) {
      ProcessError(static_cast<Expression*>(method_call), L"Undefined class or enum: '" +
                   FormatTypeString(eval_type->GetName()) + L"'");
    }

    // set subsequent call type
    if(method_call->GetMethodCall()) {
      Type* expr_type = ResolveGenericType(method->GetReturn(), method_call, klass, nullptr, true);
      method_call->GetMethodCall()->SetEvalType(expr_type, false);
    }

    // enum check
    if(method_call->GetMethodCall() && method_call->GetMethodCall()->GetCallType() == ENUM_CALL) {
      ProcessError(static_cast<Expression*>(method_call), L"Invalid enum reference");
    }

    // next call
    use_static_check = false;
    AnalyzeExpressionMethodCall(method_call, depth + 1);
  }
  else {
    const std::wstring &mthd_name = method_call->GetMethodName();
    const std::wstring &var_name = method_call->GetVariableName();

    if(mthd_name.size() > 0) {
      std::wstring message = L"Undefined function/method call: '" +
        mthd_name + L"(..)'\n\tEnsure calling parameters are properly casted\n\tConsider first class and static use function calls";
      ProcessErrorAlternativeMethods(message);
      ProcessError(static_cast<Expression*>(method_call), message);
    }
    else {
      std::wstring message = L"Undefined function/method call: '" +
        var_name + L"(..)'\n\tEnsure calling parameters are properly casted\n\tConsider first class and static use function calls";
      ProcessErrorAlternativeMethods(message);
      ProcessError(static_cast<Expression*>(method_call), message);
    }
  }
}

/****************************
 * Resolves library method calls
 ****************************/
LibraryMethod* ContextAnalyzer::ResolveMethodCall(LibraryClass* klass, MethodCall* method_call, const int depth)
{
  const std::wstring &method_name = method_call->GetMethodName();
  ExpressionList* calling_params = method_call->GetCallingParameters();
  std::vector<Expression*> expr_params = calling_params->GetExpressions();
  std::vector<LibraryMethod*> candidates = klass->GetUnqualifiedMethods(method_name);

  // save all valid candidates
  std::vector<LibraryMethodCallSelection*> matches;
  for(size_t i = 0; i < candidates.size(); ++i) {
    // match parameter sizes
    LibraryMethod* candidate = candidates[i];
    std::vector<Type*> method_parms = candidate->GetDeclarationTypes();
    if(expr_params.size() == method_parms.size()) {
      // box and unbox parameters
      std::vector<Expression*> boxed_resolved_params;
      for(size_t j = 0; j < expr_params.size(); ++j) {
        Expression* expr_param = expr_params[j];
        Type* expr_type = expr_param->GetEvalType();
        Type* method_type = ResolveGenericType(method_parms[j], method_call, nullptr, klass, false);

        Expression* boxed_param = BoxExpression(method_type, expr_param, depth);
        if(boxed_param) {
          boxed_resolved_params.push_back(boxed_param);
        }
        else if((boxed_param = UnboxingExpression(expr_type, expr_param, false, depth))) {
          boxed_resolved_params.push_back(boxed_param);
        }
        // add default
        if(!boxed_param) {
          boxed_resolved_params.push_back(expr_param);
        }
      }

#ifdef _DEBUG
      assert(boxed_resolved_params.size() == expr_params.size());
#endif

      LibraryMethodCallSelection* match = new LibraryMethodCallSelection(candidate, boxed_resolved_params);
      for(size_t j = 0; j < boxed_resolved_params.size(); ++j) {
        Type* method_type = ResolveGenericType(method_parms[j], method_call, nullptr, klass, false);
        const int compare = MatchCallingParameter(boxed_resolved_params[j], method_type, nullptr, klass, depth);
        match->AddParameterMatch(compare);
      }
      matches.push_back(match);
    }
  }

  // evaluate matches
  LibraryMethodCallSelector selector(method_call, matches);
  LibraryMethod* lib_method = selector.GetSelection();
  if(lib_method) {
    // check casts on final candidate
    std::vector<Type*> method_parms = lib_method->GetDeclarationTypes();
    for(size_t j = 0; j < expr_params.size(); ++j) {
      Expression* expression = expr_params[j];
      while(expression->GetMethodCall()) {
        AnalyzeExpressionMethodCall(expression, depth + 1);
        if(expression->GetExpressionType() == METHOD_CALL_EXPR && expression->GetEvalType() &&
           expression->GetEvalType()->GetType() == NIL_TYPE) {
          ProcessError(static_cast<Expression*>(method_call), L"Invalid operation with 'Nil' value");
        }
        expression = expression->GetMethodCall();
      }
      // map generic to concrete type, if needed
      Type* left = ResolveGenericType(method_parms[j], method_call, nullptr, klass, false);
      AnalyzeRightCast(left, expression, IsScalar(expression), depth + 1);
    }

    // public/private check
    if(!lib_method->IsStatic() && (lib_method->GetMethodType() == PRIVATE_METHOD || lib_method->GetMethodType() == NEW_PRIVATE_METHOD)) {
      ProcessError(static_cast<Expression*>(method_call), L"Cannot reference a private method from this context");
    }
  }
  else {
    std::vector<LibraryMethod*> alt_mthds = selector.GetAlternativeMethods();
    LibraryMethod* derived_method = DerivedLambdaFunction(alt_mthds);
    if(derived_method) {
      return derived_method;
    }
    else if(alt_mthds.size()) {
      alt_error_method_names = selector.GetAlternativeMethodNames();
    }
  }

  return lib_method;
}

/****************************
 * Analyzes a method call.  This
 * is method call within a linked
 * library
 ****************************/
void ContextAnalyzer::AnalyzeMethodCall(LibraryClass* klass, MethodCall* method_call, bool is_expr, 
                                        std::wstring &encoding, bool is_parent, const int depth)
{
#ifdef _DEBUG
  GetLogger() << L"Checking library encoded name: |" << klass->GetName() << L':' << method_call->GetMethodName() << L"|" << std::endl;
#endif

  ExpressionList* call_params = method_call->GetCallingParameters();

  // lambda inferred type
  CheckLambdaInferredTypes(method_call, depth + 1);
  
  AnalyzeExpressions(call_params, depth + 1);
  LibraryMethod* lib_method = ResolveMethodCall(klass, method_call, depth);
  if(!lib_method) {
    LibraryClass* parent = linker->SearchClassLibraries(klass->GetParentName(), program->GetLibUses(current_class->GetFileName()));
    while(!lib_method && parent) {
      lib_method = ResolveMethodCall(parent, method_call, depth);
      parent = linker->SearchClassLibraries(parent->GetParentName(), program->GetLibUses(current_class->GetFileName()));
    }
  }

  // note: last resort to find system based methods i.e. $Int, $Float, etc.
  if(!lib_method) {
    std::wstring encoded_name = klass->GetName() + L':' + method_call->GetMethodName() + L':' +
      encoding + EncodeMethodCall(method_call->GetCallingParameters(), depth);
    if(*encoded_name.rbegin() == L'*') {
      encoded_name.push_back(L',');
    }
    lib_method = klass->GetMethod(encoded_name);
  }

  // check private class scope
  const std::wstring bundle_name = klass->GetBundleName();
  if(!klass->IsPublic() && current_class && current_class->GetBundleName() != bundle_name) {
    ProcessError(static_cast<Expression*>(method_call), L"Cannot access private class '" + klass->GetName() + L"' from this bundle scope");
  }

  method_call->SetOriginalLibraryClass(klass);
  AnalyzeMethodCall(lib_method, method_call, klass->IsVirtual() && !is_parent, is_expr, depth);
}

/****************************
 * Analyzes a method call.  This
 * is method call within a linked
 * library
 ****************************/
void ContextAnalyzer::AnalyzeMethodCall(LibraryMethod* lib_method, MethodCall* method_call, bool is_virtual, bool is_expr, const int depth)
{
  if(lib_method) {
    ExpressionList* call_params = method_call->GetCallingParameters();
    std::vector<Expression*> expressions = call_params->GetExpressions();

    for(size_t i = 0; i < expressions.size(); ++i) {
      Expression* expression = expressions[i];
      if(expression->GetExpressionType() == METHOD_CALL_EXPR && expression->GetEvalType() &&
         expression->GetEvalType()->GetType() == NIL_TYPE) {
        ProcessError(static_cast<Expression*>(method_call), L"Invalid operation with 'Nil' value");
      }
    }

    // public/private check
    if(lib_method->IsStatic() && lib_method->GetMethodType() == PRIVATE_METHOD) {
      ProcessError(static_cast<Expression*>(method_call), L"Cannot reference a private function from this context");
    }
    else if(!lib_method->IsStatic() && (lib_method->GetMethodType() == PRIVATE_METHOD || lib_method->GetMethodType() == NEW_PRIVATE_METHOD) &&
            !lib_method->GetLibraryClass()->GetParentName().empty()) {
      if(method_call->GetPreviousExpression()) {
        Expression* pre_expr = method_call->GetPreviousExpression();
        while(pre_expr->GetPreviousExpression()) {
          pre_expr = pre_expr->GetPreviousExpression();
        }
        switch(pre_expr->GetExpressionType()) {
        case METHOD_CALL_EXPR: {
          MethodCall* prev_method_call = static_cast<MethodCall*>(pre_expr);
          if(prev_method_call->GetCallType() != NEW_INST_CALL && prev_method_call->GetLibraryMethod() &&
             !prev_method_call->GetLibraryMethod()->IsStatic() && !prev_method_call->GetEntry() && 
             !prev_method_call->GetVariable()) {
            ProcessError(static_cast<Expression*>(method_call), L"Cannot reference a method from this context");
          }
        }
          break;

        case CHAR_LIT_EXPR:
        case INT_LIT_EXPR:
        case FLOAT_LIT_EXPR:
        case BOOLEAN_LIT_EXPR:
        case CHAR_STR_EXPR:
        case STAT_ARY_EXPR:
        case VAR_EXPR:
          break;

        default:
          ProcessError(static_cast<Expression*>(method_call), L"Cannot reference a method from this context");
          break;
        }
      }
      else if(!method_call->GetEntry() && !method_call->GetVariable()) {
        ProcessError(static_cast<Expression*>(method_call), L"Cannot reference a method from this context");
      }
    }
    
    // static check
    if(InvalidStatic(method_call, lib_method)) {
      ProcessError(static_cast<Expression*>(method_call), L"Cannot reference an instance method from this context");
    }
    
    // cannot create an instance of a virtual class
    if((lib_method->GetMethodType() == NEW_PUBLIC_METHOD || lib_method->GetMethodType() == NEW_PRIVATE_METHOD) && is_virtual) {
      ProcessError(static_cast<Expression*>(method_call), L"Cannot create an instance of a virtual class or interface, check the constructor");
    }

    // cannot call an unimplemented function
    if(lib_method->IsVirtual() && lib_method->IsStatic()) {
       ProcessError(static_cast<Expression*>(method_call), L"Cannot call an unimplemented virtual function");
    }

    // associate method
    lib_method->GetLibraryClass()->SetCalled(true);
    method_call->SetLibraryMethod(lib_method);

    if(method_call->GetMethodCall()) {
      method_call->GetMethodCall()->SetEvalType(lib_method->GetReturn(), false);
    }

    // enum check
    if(method_call->GetMethodCall() && method_call->GetMethodCall()->GetCallType() == ENUM_CALL) {
      ProcessError(static_cast<Expression*>(method_call), L"Invalid enum reference");
    }

    if(lib_method->GetReturn()->GetType() == NIL_TYPE && method_call->GetCastType()) {
      ProcessError(static_cast<Expression*>(method_call), L"Cannot cast a Nil return value");
    }
    
    // map Generic to concrete types
    LibraryClass* lib_klass = lib_method->GetLibraryClass();
    const bool is_new = lib_method->GetMethodType() == NEW_PUBLIC_METHOD || lib_method->GetMethodType() == NEW_PRIVATE_METHOD;
    const bool same_cls_return = ClassEquals(lib_method->GetReturn()->GetName(), nullptr, lib_klass);
    if((is_new || same_cls_return) && lib_klass->HasGenerics()) {
      const std::vector<LibraryClass*> class_generics = lib_klass->GetGenericClasses();
      const std::vector<Type*> concrete_types = GetConcreteTypes(method_call);
      if(class_generics.size() != concrete_types.size()) {
        ProcessError(static_cast<Expression*>(method_call), L"Cannot create an unqualified instance of class: '" + lib_method->GetUserName() + L"'");
      }
      // check individual types
      if(class_generics.size() == concrete_types.size()) {
        for(size_t i = 0; i < concrete_types.size(); ++i) {
          Type* concrete_type = concrete_types[i];
          LibraryClass* class_generic = class_generics[i];
          if(class_generic->HasGenericInterface()) {
            Type* backing_type = class_generic->GetGenericInterface();
            // backing type
            ResolveClassEnumType(backing_type);
            const std::wstring backing_name = backing_type->GetName();
            // concrete type
            ResolveClassEnumType(concrete_type);
            // validate backing
            ValidateGenericBacking(concrete_type, backing_name, static_cast<Expression*>(method_call));
          }
          else if(concrete_types.size() == 1) {
            // concrete type
            ResolveClassEnumType(concrete_type);
            const std::wstring concrete_name = concrete_type->GetName();
            // validate backing
            ValidateGenericBacking(concrete_type, concrete_name, static_cast<Expression*>(method_call));
          }
        }
      }
      method_call->GetEvalType()->SetGenerics(concrete_types);
    }

    // resolve generic to concrete, if needed
    Type* eval_type = method_call->GetEvalType();
    if(lib_method->GetLibraryClass()->HasGenerics()) {
      eval_type = ResolveGenericType(eval_type, method_call, nullptr, lib_klass, true);
      method_call->SetEvalType(eval_type, false);
    }
    else if(lib_method->GetReturn()->HasGenerics()) {
      const std::vector<Type*> concrete_types = method_call->GetConcreteTypes();
      const std::vector<Type*> generic_types = lib_method->GetReturn()->GetGenerics();
      if(concrete_types.size() == generic_types.size()) {
        for(size_t i = 0; i < concrete_types.size(); ++i) {
          Type* concrete_type = concrete_types[i];
          ResolveClassEnumType(concrete_type);

          Type* generic_type = generic_types[i];
          ResolveClassEnumType(generic_type);

          ValidateConcretes(concrete_type, generic_type, method_call);
        }
      }
      else {
        if(concrete_types.empty()) {
          for(const auto& generic_type : generic_types) {
            if(!ResolveClassEnumType(generic_type)) {
              ProcessError(static_cast<Expression*>(method_call), L"Invalid concrete type");
            }
          }
        }
        else {
          ProcessError(static_cast<Expression*>(method_call), L"Generic to concrete size mismatch");
        }
      }
    }

    const std::vector<Type*> concrete_types = method_call->GetConcreteTypes();
    if(method_call->GetCallType() != NEW_INST_CALL && concrete_types.size() > 0 && concrete_types[0]->GetGenerics().size() == 1) {
      Type* first_type = concrete_types[0];
      ResolveClassEnumType(first_type);
      method_call->SetEvalType(first_type, true);
    }

    // next call
    use_static_check = false;
    AnalyzeExpressionMethodCall(method_call, depth + 1);
  }
  else {
    AnalyzeVariableFunctionCall(method_call, depth + 1);
  }
}

/********************************
 * Analyzes a dynamic function
 * call
 ********************************/
void ContextAnalyzer::AnalyzeVariableFunctionCall(MethodCall* method_call, const int depth)
{
  // dynamic function call that is not bound to a class/function until runtime
  SymbolEntry* entry = GetEntry(method_call->GetMethodName());
  if(entry && entry->GetType() && entry->GetType()->GetType() == FUNC_TYPE) {
    entry->WasLoaded();

    // track rouge return
    ++nested_call_depth;
    
    // generate parameter strings
    Type* type = entry->GetType();
    AnalyzeVariableFunctionParameters(type, static_cast<Expression*>(method_call));

    // get calling and function parameters
    const std::vector<Type*> func_params = type->GetFunctionParameters();
    std::vector<Expression*> calling_params = method_call->GetCallingParameters()->GetExpressions();
    if(func_params.size() != calling_params.size()) {
      ProcessError(static_cast<Expression*>(method_call), L"Function call parameter size mismatch");
      return;
    }

    // check parameters
    std::wstring dyn_func_params_str;
    ExpressionList* boxed_resolved_params = TreeFactory::Instance()->MakeExpressionList();
    for(size_t i = 0; i < func_params.size(); ++i) {
      Type* func_param = func_params[i];
      Expression* calling_param = calling_params[i];

      // check for boxing/unboxing
      Expression* boxed_param = BoxExpression(func_param, calling_param, depth + 1);
      if(boxed_param) {
        boxed_resolved_params->AddExpression(boxed_param);
      }
      else if((boxed_param = UnboxingExpression(func_param, calling_param, false, depth + 1))) {
        boxed_resolved_params->AddExpression(boxed_param);
      }
      // add default
      if(!boxed_param) {
        boxed_resolved_params->AddExpression(calling_param);
      }

      // encode parameter
      dyn_func_params_str += EncodeType(func_param);
      for(int j = 0; j < type->GetDimension(); ++j) {
        dyn_func_params_str += L'*';
      }
      dyn_func_params_str += L',';
    }
    
    // method call parameters
    type->SetFunctionParameterCount((int)method_call->GetCallingParameters()->GetExpressions().size());
    AnalyzeExpressions(boxed_resolved_params, depth + 1);

    // check parameters again dynamic definition
    const std::wstring call_params_str = EncodeMethodCall(boxed_resolved_params, depth);
    if(dyn_func_params_str != call_params_str) {
      ProcessError(static_cast<Expression*>(method_call), L"Undefined function/method call: '" + method_call->GetMethodName() +
                   L"(..)'\n\tEnsure calling parameters are properly casted\n\tConsider first class and static use function calls");
    }
    // reset calling parameters
    method_call->SetCallingParameters(boxed_resolved_params);

    // set entry reference and return type
    method_call->SetFunctionalCall(entry);
    method_call->SetEvalType(type->GetFunctionReturn(), true);
    if(method_call->GetMethodCall()) {
      method_call->GetMethodCall()->SetEvalType(type->GetFunctionReturn(), false);
    }

    // next call
    AnalyzeExpressionMethodCall(method_call, depth + 1);
    
    // check for rouge return
    --nested_call_depth;
    RogueReturn(method_call);
  }
  else if(!use_static_check) {
    const std::wstring &mthd_name = method_call->GetMethodName();
    const std::wstring &var_name = method_call->GetVariableName();

    if(mthd_name.size() > 0) {
      std::wstring message = L"Undefined function/method call: '" + mthd_name +
        L"(..)'\n\tEnsure calling parameters are properly casted\n\tConsider first class and static use function calls";
      ProcessErrorAlternativeMethods(message);
      ProcessError(static_cast<Expression*>(method_call), message);
    }
    else {
      std::wstring message = L"Undefined function/method call: '" + var_name +
        L"(..)'\n\tEnsure calling parameters are properly casted\n\tConsider first class and static use function calls";
      ProcessErrorAlternativeMethods(message);
      ProcessError(static_cast<Expression*>(method_call), message);
    }
  }
}

/********************************
 * Analyzes a function reference
 ********************************/
void ContextAnalyzer::AnalyzeFunctionReference(Class* klass, MethodCall* method_call,
                                               std::wstring &encoding, const int depth)
{
  const std::wstring func_encoding = EncodeFunctionReference(method_call->GetCallingParameters(), depth);
  const std::wstring encoded_name = klass->GetName() + L':' + method_call->GetMethodName() +
    L':' + encoding + func_encoding;

  Method* method = klass->GetMethod(encoded_name);
  if(method) {
    const std::wstring func_type_id = L"m.(" + func_encoding + L")~" + method->GetEncodedReturn();
    
    Type* type = TypeParser::ParseType(func_type_id);
    type->SetFunctionParameterCount((int)method_call->GetCallingParameters()->GetExpressions().size());
    type->SetFunctionReturn(method->GetReturn());
    method_call->SetEvalType(type, true);

    if(!method->IsStatic()) {
      ProcessError(static_cast<Expression*>(method_call), L"References to methods are not allowed, only functions");
    }

    if(method->IsVirtual()) {
      ProcessError(static_cast<Expression*>(method_call), L"References to methods cannot be virtual");
    }

    // check return type
    Type* rtrn_type = method_call->GetFunctionalReturn();
    if(rtrn_type->GetType() != method->GetReturn()->GetType()) {
      ProcessError(static_cast<Expression*>(method_call), L"Mismatch function return types");
    }
    else if(rtrn_type->GetType() == CLASS_TYPE) {
      if(ResolveClassEnumType(rtrn_type)) {
        const std::wstring rtrn_encoded_name = L"o." + rtrn_type->GetName();
        if(rtrn_encoded_name != method->GetEncodedReturn()) {
          ProcessError(static_cast<Expression*>(method_call), L"Mismatch function return types");
        }
      }
      else {
        ProcessError(static_cast<Expression*>(method_call),
                     L"Undefined class or enum: '" + FormatTypeString(rtrn_type->GetName()) + L"'");
      }
    }
    method->GetClass()->SetCalled(true);
    method_call->SetOriginalClass(klass);
    method_call->SetMethod(method, false);
  }
  else {
    const std::wstring &mthd_name = method_call->GetMethodName();
    const std::wstring &var_name = method_call->GetVariableName();

    if(mthd_name.size() > 0) {
      ProcessError(static_cast<Expression*>(method_call), L"Undefined function/method call: '" +
                   mthd_name + L"(..)'\n\tEnsure calling parameters are properly casted\n\tConsider first class and static use function calls");
    }
    else {
      ProcessError(static_cast<Expression*>(method_call), L"Undefined function/method call: '" +
                   var_name + L"(..)'\n\tEnsure calling parameters are properly casted\n\tConsider first class and static use function calls");
    }
  }
}

/****************************
 * Checks a function reference
 ****************************/
void ContextAnalyzer::AnalyzeFunctionReference(LibraryClass* klass, MethodCall* method_call,
                                               std::wstring &encoding, const int depth)
{
  const std::wstring func_encoding = EncodeFunctionReference(method_call->GetCallingParameters(), depth);
  const std::wstring encoded_name = klass->GetName() + L':' + method_call->GetMethodName() + L':' + encoding + func_encoding;

  LibraryMethod* method = klass->GetMethod(encoded_name);
  if(method) {
    const std::wstring func_type_id = L'(' + func_encoding + L")~" + method->GetEncodedReturn();
    Type* type = TypeParser::ParseType(func_type_id);
    type->SetFunctionParameterCount((int)method_call->GetCallingParameters()->GetExpressions().size());
    type->SetFunctionReturn(method->GetReturn());
    method_call->SetEvalType(type, true);

    if(!method->IsStatic()) {
      ProcessError(static_cast<Expression*>(method_call), L"References to methods are not allowed, only functions");
    }

    if(method->IsVirtual()) {
      ProcessError(static_cast<Expression*>(method_call), L"References to methods cannot be virtual");
    }

    // check return type
    Type* rtrn_type = method_call->GetFunctionalReturn();
    if(rtrn_type->GetType() != method->GetReturn()->GetType()) {
      ProcessError(static_cast<Expression*>(method_call), L"Mismatch function return types");
    }
    else if(rtrn_type->GetType() == CLASS_TYPE) {
      if(ResolveClassEnumType(rtrn_type)) {
        const std::wstring rtrn_encoded_name = L"o." + rtrn_type->GetName();
        if(rtrn_encoded_name != method->GetEncodedReturn()) {
          ProcessError(static_cast<Expression*>(method_call), L"Mismatch function return types");
        }
      }
      else {
        ProcessError(static_cast<Expression*>(method_call),
                     L"Undefined class or enum: '" + FormatTypeString(rtrn_type->GetName()) + L"'");
      }
    }
    method->GetLibraryClass()->SetCalled(true);
    method_call->SetOriginalLibraryClass(klass);
    method_call->SetLibraryMethod(method, false);
  }
  else {
    const std::wstring &mthd_name = method_call->GetMethodName();
    const std::wstring &var_name = method_call->GetVariableName();

    if(mthd_name.size() > 0) {
      ProcessError(static_cast<Expression*>(method_call), L"Undefined function/method call: '" +
                   mthd_name + L"(..)'\n\tEnsure calling parameters are properly casted\n\tConsider first class and static use function calls");
    }
    else {
      ProcessError(static_cast<Expression*>(method_call), L"Undefined function/method call: '" +
                   var_name + L"(..)'\n\tEnsure calling parameters are properly casted\n\tConsider first class and static use function calls");
    }
  }
}

/****************************
 * Analyzes a cast
 ****************************/
void ContextAnalyzer::AnalyzeCast(Expression* expression, const int depth)
{
  // type cast
  if(expression->GetCastType()) {
    // get cast and root types
    Type* cast_type = expression->GetCastType();
    ResolveClassEnumType(cast_type);

    Type* root_type = expression->GetBaseType();
    if(!root_type) {
      root_type = expression->GetEvalType();
    }

    if(root_type && root_type->GetType() == VAR_TYPE) {
      ProcessError(expression, L"Cannot cast an uninitialized type");
    }

    // cannot cast across different dimensions
    if(root_type && expression->GetExpressionType() == VAR_EXPR &&
       !static_cast<Variable*>(expression)->GetIndices() &&
       cast_type->GetDimension() != root_type->GetDimension()) {
      ProcessError(expression, L"Dimension size mismatch");
    }

    AnalyzeRightCast(cast_type, root_type, expression, IsScalar(expression), depth + 1);
  }
  // 'TypeOf(..)' check
  else if(expression->GetTypeOf()) {
    if(expression->GetTypeOf()->GetType() != CLASS_TYPE) {
      ProcessError(expression, L"Invalid 'TypeOf' check, only allowed for complex classes");
    }

    if(expression->GetExpressionType() == METHOD_CALL_EXPR && 
       (static_cast<MethodCall*>(expression)->GetCallType() == NEW_INST_CALL || static_cast<MethodCall*>(expression)->GetCallType() == NEW_ARRAY_CALL)) {
      ProcessError(expression, L"Invalid 'TypeOf' check during object instantiation");
    }

    Type* type_of = expression->GetTypeOf();
    if(SearchProgramClasses(type_of->GetName())) {
      Class* klass = SearchProgramClasses(type_of->GetName());
      klass->SetCalled(true);
      type_of->SetName(klass->GetName());
    }
    else if(linker->SearchClassLibraries(type_of->GetName(), program->GetLibUses(current_class->GetFileName()))) {
      LibraryClass* lib_klass = linker->SearchClassLibraries(type_of->GetName(), program->GetLibUses(current_class->GetFileName()));
      lib_klass->SetCalled(true);
      type_of->SetName(lib_klass->GetName());
    }
    else {
      ProcessError(expression, L"Invalid 'TypeOf' check, unknown class '" + type_of->GetName() + L"'");
    }
    expression->SetEvalType(TypeFactory::Instance()->MakeType(BOOLEAN_TYPE), true);
  }
}

/****************************
 * Analyzes array indices
 ****************************/
void ContextAnalyzer::AnalyzeIndices(ExpressionList* indices, const int depth)
{
  AnalyzeExpressions(indices, depth + 1);

  std::vector<Expression*> unboxed_expressions;
  std::vector<Expression*> expressions = indices->GetExpressions();

  for(size_t i = 0; i < expressions.size(); ++i) {
    Expression* expression = expressions[i];    
    AnalyzeExpression(expression, depth + 1);
    Type* eval_type = expression->GetEvalType();
    if(eval_type) {
      switch(eval_type->GetType()) {
      case BYTE_TYPE:
      case CHAR_TYPE:
      case INT_TYPE:
        unboxed_expressions.push_back(expression);
        break;

      case CLASS_TYPE:
        if(IsEnumExpression(expression)) {
          unboxed_expressions.push_back(expression);
        }
        else {
          Expression* unboxed_expresion = UnboxingExpression(eval_type, expression, true, depth);
          if(unboxed_expresion) {
            unboxed_expressions.push_back(unboxed_expresion);
          }
          else {
            ProcessError(expression, L"Expected Byte, Char, Int or Enum class type");
          }
        }
        break;

      default:
        ProcessError(expression, L"Expected Byte, Char, Int or Enum class type");
        break;
      }
    }
  }

  // reset expressions
  indices->SetExpressions(unboxed_expressions);
}

/****************************
 * Analyzes a simple statement
 ****************************/
void ContextAnalyzer::AnalyzeSimpleStatement(SimpleStatement* simple, const int depth)
{
  Expression* expression = simple->GetExpression();
  if(expression) {
    AnalyzeExpression(expression, depth + 1);
    AnalyzeExpressionMethodCall(expression, depth);
    
    // ensure it's a valid statement
    if(!expression->GetMethodCall()) {
      ProcessError(expression, L"Invalid statement");
    }
  }
}

/****************************
 * Analyzes a 'if' statement
 ****************************/
void ContextAnalyzer::AnalyzeIf(If* if_stmt, const int depth)
{
#ifdef _DEBUG
  Debug(L"if/else-if/else", if_stmt->GetLineNumber(), depth);
#endif

  // expression
  Expression* expression = if_stmt->GetExpression();
  if(expression) {
    AnalyzeExpression(expression, depth + 1);
    if(!IsBooleanExpression(expression)) {
      ProcessError(expression, L"Expected Bool expression");
    }
  }
  // 'if' statements
  AnalyzeStatements(if_stmt->GetIfStatements(), depth + 1);

  If* next = if_stmt->GetNext();
  if(next) {
    AnalyzeIf(next, depth);
  }

  // 'else'
  StatementList* else_list = if_stmt->GetElseStatements();
  if(else_list) {
    AnalyzeStatements(else_list, depth + 1);
  }
}

/****************************
 * Analyzes a 'select' statement
 ****************************/
void ContextAnalyzer::AnalyzeSelect(Select* select_stmt, const int depth)
{
  // expression
  Expression* expression = select_stmt->GetAssignment()->GetExpression();
  if(expression) {
    AnalyzeExpression(expression, depth + 1);
    const bool is_int_expr = IsIntegerExpression(expression);

#ifndef _SYSTEM
    const bool is_str_expr = IsStringExpression(expression);
    if(!is_int_expr && !is_str_expr) {
      ProcessError(expression, L"Expected integer or string expression");
    }

    if(is_str_expr) {
      if(expression->GetExpressionType() == VAR_EXPR) {
        Variable* var_expr = static_cast<Variable*>(expression);
        expression = TreeFactory::Instance()->MakeMethodCall(select_stmt->GetFileName(), select_stmt->GetLineNumber(),
                                                             select_stmt->GetLinePosition(), select_stmt->GetEndLineNumber(),
                                                             select_stmt->GetEndLinePosition(), -1, -1, var_expr, L"HashID",
                                                             TreeFactory::Instance()->MakeExpressionList());
        AnalyzeExpression(expression, depth + 1);
        select_stmt->GetAssignment()->SetExpression(expression);
      }
      else {
        const CharacterString* char_str_expr = static_cast<CharacterString*>(expression);
        expression = TreeFactory::Instance()->MakeIntegerLiteral(select_stmt->GetFileName(), select_stmt->GetLineNumber(), 
                                                                 select_stmt->GetLinePosition(), HashString(char_str_expr->GetString().c_str()));
        select_stmt->GetAssignment()->SetExpression(expression);
      }
    }
#else
    if(!is_int_expr) {
      ProcessError(expression, L"Expected integer expression");
    }
#endif
  }

  // labels and expressions
  std::map<ExpressionList*, StatementList*> statements = select_stmt->GetStatements();
  if(statements.size() < 1) {
    ProcessError(select_stmt, L"Select statement must have at least one label");
  }
  
  std::map<ExpressionList*, StatementList*>::iterator iter;
  // duplicate value vector
  INT64_VALUE value = 0;
  std::map<INT64_VALUE, StatementList*> label_statements;
  for(iter = statements.begin(); iter != statements.end(); ++iter) {
    // expressions
    ExpressionList* expressions = iter->first;
    AnalyzeExpressions(expressions, depth + 1);
    // check expression type
    std::vector<Expression*> expression_list = expressions->GetExpressions();
    for(size_t i = 0; i < expression_list.size(); ++i) {
      Expression* expression = expression_list[i];
      if(expression) {
        switch(expression->GetExpressionType()) {
        case CHAR_STR_EXPR: {
          const CharacterString* char_str_expr = static_cast<CharacterString*>(expression);
          value = HashString(char_str_expr->GetString().c_str());
          if(DuplicateCaseItem(label_statements, value)) {
            ProcessError(expression, L"Duplicate select hash value for '" + char_str_expr->GetString() + L'\'');
          }
        }
          break;

        case CHAR_LIT_EXPR:
          value = static_cast<CharacterLiteral*>(expression)->GetValue();
          if(DuplicateCaseItem(label_statements, value)) {
            ProcessError(expression, L"Duplicate select value '" + ToString(value) + L'\'');
          }
          break;

        case INT_LIT_EXPR:
          value = static_cast<IntegerLiteral*>(expression)->GetValue();
          if(DuplicateCaseItem(label_statements, value)) {
            ProcessError(expression, L"Duplicate select value '" + ToString(value) + L'\'');
          }
          break;

        case METHOD_CALL_EXPR: {
          // get method call
          MethodCall* mthd_call = static_cast<MethodCall*>(expression);
          if(mthd_call->GetMethodCall()) {
            mthd_call = mthd_call->GetMethodCall();
          }
          // check type
          if(mthd_call->GetEnumItem()) {
            value = mthd_call->GetEnumItem()->GetId();
            if(DuplicateCaseItem(label_statements, value)) {
              ProcessError(expression, L"Duplicate select value '" + ToString(value) + L'\'');
            }
          }
          else if(mthd_call->GetLibraryEnumItem()) {
            value = mthd_call->GetLibraryEnumItem()->GetId();
            if(DuplicateCaseItem(label_statements, value)) {
              ProcessError(expression, L"Duplicate select value '" + ToString(value) + L'\'');
            }
          }
          else {
            ProcessError(expression, L"Expected integer literal or enum item");
          }
        }
          break;

        default:
          ProcessError(expression, L"Expected integer literal or enum item");
          break;
        }
      }

      // statements get associated here and validated below
      label_statements.insert(std::pair<INT64_VALUE, StatementList*>(value, iter->second));
    }
  }
  select_stmt->SetLabelStatements(label_statements);

  // process statements (in parse order)
  std::vector<StatementList*> statement_lists = select_stmt->GetStatementLists();
  for(size_t i = 0; i < statement_lists.size(); ++i) {
    AnalyzeStatements(statement_lists[i], depth + 1);
  }
}

/****************************
 * Analyzes a critical section
 ****************************/
void ContextAnalyzer::AnalyzeCritical(CriticalSection* mutex, const int depth)
{
  Variable* variable = mutex->GetVariable();
  if(variable) {
    AnalyzeVariable(variable, depth + 1);
    if(variable->GetEvalType() && variable->GetEvalType()->GetType() == CLASS_TYPE) {
      if(variable->GetEvalType()->GetName() != L"System.Concurrency.ThreadMutex") {
        ProcessError(mutex, L"Expected ThreadMutex type");
      }
    }
    else {
      ProcessError(mutex, L"Expected ThreadMutex type");
    }
    AnalyzeStatements(mutex->GetStatements(), depth + 1);
  }
}

/****************************
 * Analyzes a 'for' statement
 ****************************/
void ContextAnalyzer::AnalyzeFor(For* for_stmt, const int depth)
{
  current_table->NewScope();
  
  bool is_range = false;
  CalculatedExpression* cond_expr = static_cast<CalculatedExpression*>(for_stmt->GetExpression());
  
  if(cond_expr->GetRight()->GetExpressionType() == METHOD_CALL_EXPR) {
    MethodCall* mthd_call_expr = static_cast<MethodCall*>(cond_expr->GetRight());
    const std::wstring cond_expr_name = mthd_call_expr->GetVariableName();
    if(IsRangeName(cond_expr_name)) {
      AnalyzeMethodCall(mthd_call_expr, depth + 1);
      is_range = true;
    }
    else {
      SymbolEntry* cond_expr_entry = current_table->GetEntry(current_method->GetName() + L':' + cond_expr_name);
      if(cond_expr_entry && cond_expr_entry->GetType() && cond_expr_entry->GetType()->GetType() == CLASS_TYPE && IsRangeName(cond_expr_entry->GetType()->GetName())) {
        Variable* variable = TreeFactory::Instance()->MakeVariable(for_stmt->GetFileName(), for_stmt->GetLineNumber(), for_stmt->GetLinePosition(), cond_expr_name);
        cond_expr_entry->WasLoaded();
        variable->SetEntry(cond_expr_entry);
        variable->SetEvalType(cond_expr_entry->GetType(), true);
        cond_expr->SetRight(variable);
        is_range = true;

        Variable* range_var = static_cast<Variable*>(cond_expr->GetLeft());
        SymbolEntry* range_entry = current_table->GetEntry(current_method->GetName() + L':' + range_var->GetName());
        if(range_entry && cond_expr_entry && cond_expr_entry->GetType()) {
          const std::wstring range_name = cond_expr_entry->GetType()->GetName();
          if(range_name == L"System.CharRange") {
            range_entry->SetType(TypeFactory::Instance()->MakeType(CHAR_TYPE));
          }
          else if(range_name == L"System.IntRange") {
            range_entry->SetType(TypeFactory::Instance()->MakeType(INT_TYPE));
          }
          else if(range_name == L"System.FloatRange") {
            range_entry->SetType(TypeFactory::Instance()->MakeType(FLOAT_TYPE));
          }
        }
      }
    }
  }

  // range expression
  if(is_range) {
    Variable* var_expr = static_cast<Variable*>(cond_expr->GetLeft());
    const std::wstring index_name = current_method->GetName() + L":#" + var_expr->GetName() + L"_range";
    SymbolEntry* index_entry = current_table->GetEntry(index_name);
    for_stmt->SetRangeEntry(index_entry);
    for_stmt->SetRange(true);
  }
  // pre-expressions
  else {
    std::vector<Statement*> pre_statements = for_stmt->GetPreStatements()->GetStatements();
    for(size_t i = 0; i < pre_statements.size(); ++i) {
      AnalyzeStatement(pre_statements[i], depth + 1);
    }

    // conditional expression
    Expression* expression = for_stmt->GetExpression();
    if(expression) {
      AnalyzeExpression(expression, depth + 1);

      if(!IsBooleanExpression(expression)) {
        ProcessError(expression, L"Expected Bool expression");
      }

      switch(expression->GetExpressionType()) {
      case AND_EXPR:
      case OR_EXPR:
      case EQL_EXPR:
      case NEQL_EXPR:
      case LES_EXPR:
      case GTR_EXPR:
      case LES_EQL_EXPR:
      case GTR_EQL_EXPR:
      {
        CalculatedExpression* calc_expr = static_cast<CalculatedExpression*>(expression);
        Expression* right_expr = calc_expr->GetRight();
        if(right_expr && right_expr->GetExpressionType() == VAR_EXPR && right_expr->GetEvalType()) {
          Variable* var_expr = static_cast<Variable*>(right_expr);
          if(var_expr->GetIndices() && right_expr->GetEvalType() &&
             (int)(var_expr->GetIndices()->GetExpressions().size()) != right_expr->GetEvalType()->GetDimension()) {
            ProcessError(expression, L"Dimension size mismatch");
          }
        }
      }
      break;

      default:
        break;
      }
    }

    // update expression
    const std::vector<Statement*> update_stmts = for_stmt->GetUpdateStatements()->GetStatements();
    for(const auto& update_stmt : update_stmts) {
      AnalyzeStatement(update_stmt, depth + 1);
    }
  }
  
  // statements
  in_loop++;
  StatementList* statements = for_stmt->GetStatements();

  // create bound variable and add assignment statement
  if(for_stmt->IsBoundAssignment()) {
    MethodCall* mthd_call_expr = nullptr;
    if(for_stmt->GetExpression()->GetExpressionType() == LES_EXPR) {
      mthd_call_expr = static_cast<MethodCall*>(cond_expr->GetRight());
    }
    else {
      std::vector<Statement*> pre_statements = for_stmt->GetPreStatements()->GetStatements();
      if(!pre_statements.empty()) {
        Assignment* assign = static_cast<Declaration*>(pre_statements.back())->GetAssignment();
        CalculatedExpression* cond_expr = static_cast<CalculatedExpression*>(assign->GetExpression());
        mthd_call_expr = static_cast<MethodCall*>(cond_expr->GetLeft());
      }
    }

    if(mthd_call_expr) {
      SymbolEntry* mthd_call_entry = mthd_call_expr->GetEntry();
      if(!mthd_call_entry && mthd_call_expr->GetVariable()) {
        mthd_call_entry = mthd_call_expr->GetVariable()->GetEntry();
      }
      
      Expression* right_expr = nullptr;
      if(mthd_call_entry && mthd_call_entry->GetType()) {
        // array variable
        if(mthd_call_entry->GetType()->GetDimension() > 0) {
          const std::wstring& entry_name = mthd_call_entry->GetName();
          const size_t start = entry_name.rfind(':');
          if(start != std::wstring::npos) {
            const std::wstring& var_name = entry_name.substr(start + 1);
            Variable* variable = TreeFactory::Instance()->MakeVariable(for_stmt->GetFileName(), for_stmt->GetLineNumber(), for_stmt->GetLinePosition(), var_name);

            ExpressionList* indices = TreeFactory::Instance()->MakeExpressionList();
            indices->AddExpression(static_cast<CalculatedExpression*>(for_stmt->GetExpression())->GetLeft());
            variable->SetIndices(indices);
          
            // bind new variable
            AnalyzeVariable(variable, mthd_call_entry, depth + 1);
            right_expr = variable;
          }
        }
        // object instance
        else if(mthd_call_entry->GetType()->GetType() == CLASS_TYPE) {
          const std::wstring& entry_name = mthd_call_entry->GetName();
          const size_t start = entry_name.rfind(':');
          if(start != std::wstring::npos) {
            const std::wstring& var_name = entry_name.substr(start + 1);
            const std::wstring ident = L"Get";

            ExpressionList* params = TreeFactory::Instance()->MakeExpressionList();
            params->AddExpression(static_cast<CalculatedExpression*>(for_stmt->GetExpression())->GetLeft());
            MethodCall* right_mthd_call = TreeFactory::Instance()->MakeMethodCall(for_stmt->GetFileName(), for_stmt->GetLineNumber(), 
                                                                                  for_stmt->GetLinePosition(), for_stmt->GetEndLineNumber(), 
                                                                                  for_stmt->GetEndLinePosition(), -1, -1, var_name, ident, params);
            
            // bind new method call
            AnalyzeMethodCall(right_mthd_call, depth);
            right_expr = right_mthd_call;
          }
        }
      }

      // update bound assignment
      if(right_expr && right_expr->GetExpressionType() == VAR_EXPR) {
        // note: special case for 'each' loop
        Variable* right_var = static_cast<Variable*>(right_expr);
        const std::wstring right_var_name = right_var->GetName();
        if(!right_var_name.empty() && right_var_name.front() == L'#' && EndsWith(right_var_name, L"_size")) {
          right_var->SetInternalVariable(true);
        }
      }

      if(right_expr) {
        Assignment* assignment = for_stmt->GetBoundAssignment();
        assignment->SetExpression(right_expr);
        statements->PrependStatement(assignment);
      }
      else {
        ProcessError(for_stmt, L"Invalid class type or assignment");
      }
    }
    else {
      ProcessError(for_stmt, L"Expected class or array type");
    }
  }
  AnalyzeStatements(statements, depth + 1);
  in_loop--;

  current_table->PreviousScope();
}

/****************************
 * Analyzes a 'do/while' statement
 ****************************/
void ContextAnalyzer::AnalyzeDoWhile(DoWhile* do_while_stmt, const int depth)
{
#ifdef _DEBUG
  Debug(L"do/while", do_while_stmt->GetLineNumber(), depth);
#endif

  // 'do/while' statements
  current_table->NewScope();
  in_loop++;
  std::vector<Statement*> statements = do_while_stmt->GetStatements()->GetStatements();
  for(size_t i = 0; i < statements.size(); ++i) {
    AnalyzeStatement(statements[i], depth + 2);
  }
  in_loop--;

  // expression
  Expression* expression = do_while_stmt->GetExpression();
  if(expression) {
    AnalyzeExpression(expression, depth + 1);
    if(!IsBooleanExpression(expression)) {
      ProcessError(expression, L"Expected Bool expression");
    }
  }
  current_table->PreviousScope();
}

/****************************
 * Analyzes a 'while' statement
 ****************************/
void ContextAnalyzer::AnalyzeWhile(While* while_stmt, const int depth)
{
#ifdef _DEBUG
  Debug(L"while", while_stmt->GetLineNumber(), depth);
#endif

  // expression
  Expression* expression = while_stmt->GetExpression();
  if(expression) {
    AnalyzeExpression(expression, depth + 1);
    if(!IsBooleanExpression(expression)) {
      ProcessError(expression, L"Expected Bool expression");
    }
  }

  // 'while' statements
  in_loop++;
  AnalyzeStatements(while_stmt->GetStatements(), depth + 1);
  in_loop--;
}

/****************************
 * Analyzes a return statement
 ****************************/
void ContextAnalyzer::AnalyzeReturn(Return* rtrn, const int depth)
{
#ifdef _DEBUG
  Debug(L"return", rtrn->GetLineNumber(), depth);
#endif

  in_return = true;

  Type* mthd_type = current_method->GetReturn();
  Expression* expression = rtrn->GetExpression();
  if(expression) {
    AnalyzeExpression(expression, depth + 1);
    while(expression->GetMethodCall()) {
      AnalyzeExpressionMethodCall(expression, depth + 1);
      expression = expression->GetMethodCall();
    }

    bool is_nil_lambda_expr = false;
    if(expression->GetExpressionType() == METHOD_CALL_EXPR && expression->GetEvalType() &&
       expression->GetEvalType()->GetType() == NIL_TYPE) {
      if(capture_lambda) {
        is_nil_lambda_expr = true;
      }
      else {
        ProcessError(expression, L"Invalid operation with 'Nil' value");
      }
    }

    MethodCall* boxed_call = BoxUnboxingReturn(mthd_type, expression, depth);
    if(boxed_call) {
      AnalyzeExpression(boxed_call, depth + 1);
      rtrn->SetExpression(boxed_call);
      expression = boxed_call;
    }
    
    if(is_nil_lambda_expr && expression->GetExpressionType() == METHOD_CALL_EXPR) {
      MethodCall* mthd_call = static_cast<MethodCall*>(expression);
      if(mthd_call->GetMethod()) {
        if(mthd_call->GetMethod()->GetReturn()->GetType() == NIL_TYPE && mthd_type->GetType() != NIL_TYPE) {
          ProcessError(rtrn, L"Method call returns no value, value expected");
        }
      }
      else if(mthd_call->GetLibraryMethod()) {
        if(mthd_call->GetLibraryMethod()->GetReturn()->GetType() == NIL_TYPE && mthd_type->GetType() != NIL_TYPE) {
          ProcessError(rtrn, L"Method call returns no value, value expected");
        }
      }
    }
    else {
      Expression* box_expression = AnalyzeRightCast(mthd_type, expression, (IsScalar(expression) && mthd_type->GetDimension() == 0), depth + 1);
      if(box_expression) {
        AnalyzeExpression(box_expression, depth + 1);
        rtrn->SetExpression(box_expression);
        expression = box_expression;
      }
    }

    ValidateConcrete(expression->GetEvalType(), mthd_type, expression, depth);

    if(mthd_type->GetType() == CLASS_TYPE && !ResolveClassEnumType(mthd_type)) {
       ProcessError(rtrn, L"Undefined class or enum: '" + FormatTypeString(mthd_type->GetName()) + L"'\n\tIf generic ensure concrete types are properly defined.");
    }
  }
  else if(mthd_type->GetType() != NIL_TYPE && current_method->GetMethodType() != NEW_PUBLIC_METHOD && current_method->GetMethodType() != NEW_PRIVATE_METHOD) {
    ProcessError(rtrn, L"Invalid return statement");
  }

  in_return = false;
}

void ContextAnalyzer::ValidateConcrete(Type* cls_type, Type* concrete_type, ParseNode* node, const int depth)
{
  if(!cls_type || !concrete_type) {
    return;
  }

  const std::wstring concrete_type_name = concrete_type->GetName();
  Class* concrete_klass = nullptr; LibraryClass* concrete_lib_klass = nullptr;
  if(!GetProgramOrLibraryClass(concrete_type, concrete_klass, concrete_lib_klass)) {
    concrete_klass = current_class->GetGenericClass(concrete_type_name);
  }

  if(concrete_klass || concrete_lib_klass) {
    const bool is_concrete_inf = ((concrete_klass && concrete_klass->IsInterface()) ||
      (concrete_lib_klass && concrete_lib_klass->IsInterface())) ? true : false;

    if(!is_concrete_inf) {
      const std::wstring cls_type_name = cls_type->GetName();
      Class* dclr_klass = nullptr; LibraryClass* dclr_lib_klass = nullptr;
      if(!GetProgramOrLibraryClass(cls_type, dclr_klass, dclr_lib_klass)) {
        dclr_klass = current_class->GetGenericClass(cls_type_name);
      }

      if(dclr_klass && dclr_klass->HasGenerics()) {
        const std::vector<Type*> concrete_types = concrete_type->GetGenerics();
        if(concrete_types.empty()) {
          ProcessError(node, L"Generic to concrete size mismatch");
        }
        else {
          ValidateGenericConcreteMapping(concrete_types, dclr_klass, node);
        }
      }
      else if(dclr_lib_klass && dclr_lib_klass->HasGenerics()) {
        const std::vector<Type*> concrete_types = concrete_type->GetGenerics();
        if(concrete_types.empty()) {
          ProcessError(node, L"Generic to concrete size mismatch");
        }
        else {
          ValidateGenericConcreteMapping(concrete_types, dclr_lib_klass, node);
        }
      }
    }
  }
}

/****************************
 * Analyzes a return statement
 ****************************/
void ContextAnalyzer::AnalyzeLeaving(Leaving* leaving_stmt, const int depth)
{
#ifdef _DEBUG
  Debug(L"leaving", leaving_stmt->GetLineNumber(), depth);
#endif

  const int level = current_table->GetDepth();
  if(level == 1) {
    AnalyzeStatements(leaving_stmt->GetStatements(), depth + 1);
    if(current_method->GetLeaving()) {
      ProcessError(leaving_stmt, L"Method or function may have only one 'leaving' block defined");
    }
    else {
      current_method->SetLeaving(leaving_stmt);
    }
  }
  else {
    ProcessError(leaving_stmt, L"Method or function 'leaving' block must be a top level statement");
  }
}

/****************************
 * Analyzes an assignment statement
 ****************************/
void ContextAnalyzer::AnalyzeAssignment(Assignment* assignment, StatementType type, const int depth)
{
#ifdef _DEBUG
  Debug(L"assignment", assignment->GetLineNumber(), depth);
#endif

  in_assignment = true;

  Variable* variable = assignment->GetVariable();
  if(variable) {
    AnalyzeVariable(variable, depth + 1);
  }

  // get last expression for assignment
  Expression* expression = assignment->GetExpression();
  if(expression) {
    AnalyzeExpression(expression, depth + 1);
    if(expression->GetPreviousExpression() && expression->GetPreviousExpression()->GetExpressionType() == STR_CONCAT_EXPR) {
      expression = expression->GetPreviousExpression();
      assignment->SetExpression(expression);
    }

    if(expression->GetExpressionType() == LAMBDA_EXPR) {
      expression = static_cast<Lambda*>(expression)->GetMethodCall();
      if(!expression) {
        in_assignment = false;
        return;
      }
    }

    while(expression->GetMethodCall()) {
      AnalyzeExpressionMethodCall(expression, depth + 1);
      expression = expression->GetMethodCall();
    }

    // if uninitialized variable, bind and update entry
    if(variable->GetEvalType() && variable->GetEvalType()->GetType() == VAR_TYPE) {
      if(variable->GetIndices()) {
        ProcessError(expression, L"Invalid operation using Var type");
      }

      SymbolEntry* entry = variable->GetEntry();
      if(entry) {
        if(expression->GetCastType()) {
          Type* to_type = expression->GetCastType();
          AnalyzeVariableCast(to_type, expression);
          variable->SetTypes(to_type);
          entry->SetType(to_type);
        }
        else {
          Type* to_type = expression->GetEvalType();
          AnalyzeVariableCast(to_type, expression);
          variable->SetTypes(to_type);
          entry->SetType(to_type);
        }
        // set variable to scalar type if we're dereferencing an array variable
        if(expression->GetExpressionType() == VAR_EXPR) {
          Variable* expr_variable = static_cast<Variable*>(expression);
          if(entry->GetType() && expr_variable->GetIndices()) {
            variable->GetBaseType()->SetDimension(0);
            variable->GetEvalType()->SetDimension(0);
            entry->GetType()->SetDimension(0);
          }
        }
      }
    }
    // handle enum reference, update entry
    else if(variable->GetEvalType() && variable->GetEvalType()->GetType() == CLASS_TYPE &&
            expression->GetExpressionType() == METHOD_CALL_EXPR && static_cast<MethodCall*>(expression)->GetEnumItem()) {
      SymbolEntry* to_entry = variable->GetEntry();
      if(to_entry) {
        Type* to_type = to_entry->GetType();
        Expression* box_expression = BoxExpression(to_type, expression, depth);
        if(box_expression) {
          expression = box_expression;
          assignment->SetExpression(box_expression);
        }
        else {
          Type* from_type = expression->GetEvalType();
          AnalyzeClassCast(to_type, from_type, expression, false, depth);
          variable->SetTypes(from_type);
          to_entry->SetType(from_type);
        }
      }
    }

    // handle generics, update entry
    if(expression->GetEvalType() && expression->GetEvalType()->HasGenerics() && variable->GetEntry() && variable->GetEntry()->GetType()) {
      const std::vector<Type*> var_types = variable->GetEntry()->GetType()->GetGenerics();
      const std::vector <Type*> expr_types = expression->GetEvalType()->GetGenerics();

      if(var_types.size() == expr_types.size()) {
        for(size_t i = 0; i < var_types.size(); ++i) {
          // resolve variable type
          Type* var_type = var_types[i];
          ResolveClassEnumType(var_type);

          // resolve expression type
          Type* expr_type = expr_types[i];
          ResolveClassEnumType(expr_type);

          // match expression types
          if(var_type->GetName() != expr_type->GetName()) {
            ProcessError(variable, L"Generic type mismatch for class '" + variable->GetEvalType()->GetName() +
                         L"' between generic types: '" + FormatTypeString(var_type->GetName()) +
                         L"' and '" + FormatTypeString(expr_type->GetName()) + L"'");
          }
        }
      }
      else {
        ProcessError(variable, L"Generic size mismatch, ensure calling parameters and generic types match");
      }
    }
    else if(expression->GetExpressionType() == METHOD_CALL_EXPR && static_cast<MethodCall*>(expression)->HasConcreteTypes()) {
      MethodCall* mthd_call = static_cast<MethodCall*>(expression);
      if(variable->GetEntry()->GetType() && variable->GetEntry()->GetType()->GetGenerics().size() != mthd_call->GetConcreteTypes().size()) {
        ProcessError(variable, L"Generic size mismatch, ensure calling parameters and generic types match");
      }
    }

    Type* left_type = variable->GetEvalType();
    bool check_right_cast = true;
    if(left_type && left_type->GetType() == CLASS_TYPE) {
#ifndef _SYSTEM
      LibraryClass* left_class = linker->SearchClassLibraries(left_type->GetName(), program->GetLibUses(current_class->GetFileName()));
#else
      Class* left_class = SearchProgramClasses(left_type->GetName());
#endif
      if(left_class) {
        const std::wstring left_name = left_class->GetName();
        //
        // 'System.String' append operations
        //
        if(left_name == L"System.String") {
          Type* right_type = GetExpressionType(expression, depth + 1);
          if(right_type && right_type->GetType() == CLASS_TYPE) {
#ifndef _SYSTEM
            LibraryClass* right_class = linker->SearchClassLibraries(right_type->GetName(), program->GetLibUses(current_class->GetFileName()));
#else
            Class* right_class = SearchProgramClasses(right_type->GetName());
#endif
            if(right_class) {
              const std::wstring right = right_class->GetName();
              // rhs string append
              if(right == L"System.String") {
                switch(type) {
                case ADD_ASSIGN_STMT:
                  static_cast<OperationAssignment*>(assignment)->SetStringConcat(true);
                  check_right_cast = false;
                  if(left_type->GetDimension() != 0 || right_type->GetDimension() != 0) {
                    if(expression->GetExpressionType() == VAR_EXPR) {
                      Variable* rhs_var = static_cast<Variable*>(expression);
                      if(!rhs_var->GetIndices()) {
                        ProcessError(expression, L"Dimension size mismatch");
                      }
                      else if(!rhs_var->GetIndices()->GetExpressions().size()) {
                        ProcessError(expression, L"Dimension size mismatch");
                      }
                    }
                    else {
                      ProcessError(expression, L"Dimension size mismatch");
                    }
                  }
                  else {
                    static_cast<OperationAssignment*>(assignment)->SetStringConcat(true);
                    check_right_cast = false;
                  }
                  break;

                case SUB_ASSIGN_STMT:
                case MUL_ASSIGN_STMT:
                case DIV_ASSIGN_STMT:
                  ProcessError(assignment, L"Invalid operation using classes: 'System.String' and 'System.String'");
                  break;

                case ASSIGN_STMT:
                  break;

                default:
                  ProcessError(assignment, L"Internal compiler error.");
                  exit(1);
                }
              }
              else {
                ProcessError(assignment, L"Invalid operation using classes: 'System.String' and '" + right + L"'");
              }
            }
            else {
              ProcessError(assignment, L"Invalid operation using classes: 'System.String' and '" + right_type->GetName() + L"'");
            }
          }
          // rhs 'Char', 'Byte', 'Int', 'Float' or 'Bool'
          else if(right_type && (right_type->GetType() == CHAR_TYPE || right_type->GetType() == BYTE_TYPE ||
                                 right_type->GetType() == INT_TYPE || right_type->GetType() == FLOAT_TYPE ||
                                 right_type->GetType() == BOOLEAN_TYPE)) {
            switch(type) {
            case ADD_ASSIGN_STMT:
              static_cast<OperationAssignment*>(assignment)->SetStringConcat(true);
              check_right_cast = false;
              break;

            case SUB_ASSIGN_STMT:
            case MUL_ASSIGN_STMT:
            case DIV_ASSIGN_STMT:
              if(right_type->GetType() == CHAR_TYPE) {
                ProcessError(assignment, L"Invalid operation using classes: 'System.String' and 'System.Char'");
              }
              else if(right_type->GetType() == BYTE_TYPE) {
                ProcessError(assignment, L"Invalid operation using classes: 'System.String' and 'System.Byte'");
              }
              else if(right_type->GetType() == INT_TYPE) {
                ProcessError(assignment, L"Invalid operation using classes: 'System.String' and 'System.Int'");
              }
              else if(right_type->GetType() == FLOAT_TYPE) {
                ProcessError(assignment, L"Invalid operation using classes: 'System.String' and 'System.Float'");
              }
              else {
                ProcessError(assignment, L"Invalid operation using classes: 'System.String' and 'System.Bool'");
              }
              break;

            case ASSIGN_STMT:
              break;

            default:
              ProcessError(assignment, L"Internal compiler error.");
              exit(1);
            }
          }
        }
        //
        // Unboxing for assignment operations
        //
        else if(IsHolderType(left_name)) {
          CalculatedExpression* calc_expression = nullptr;
          switch(type) {
          case ADD_ASSIGN_STMT:
            calc_expression = TreeFactory::Instance()->MakeCalculatedExpression(variable->GetFileName(),
                                                                                variable->GetLineNumber(),
                                                                                variable->GetLinePosition(),
                                                                                ADD_EXPR, variable, expression);
            break;

          case SUB_ASSIGN_STMT:
            calc_expression = TreeFactory::Instance()->MakeCalculatedExpression(variable->GetFileName(),
                                                                                variable->GetLineNumber(),
                                                                                variable->GetLinePosition(),
                                                                                SUB_EXPR, variable, expression);
            break;

          case MUL_ASSIGN_STMT:
            calc_expression = TreeFactory::Instance()->MakeCalculatedExpression(variable->GetFileName(),
                                                                                variable->GetLineNumber(),
                                                                                variable->GetLinePosition(),
                                                                                MUL_EXPR, variable, expression);
            break;

          case DIV_ASSIGN_STMT:
            calc_expression = TreeFactory::Instance()->MakeCalculatedExpression(variable->GetFileName(),
                                                                                variable->GetLineNumber(),
                                                                                variable->GetLinePosition(),
                                                                                DIV_EXPR, variable, expression);
            break;

          case ASSIGN_STMT:
            break;

          default:
            ProcessError(assignment, L"Internal compiler error.");
            exit(1);
          }

          if(calc_expression) {
            assignment->SetExpression(calc_expression);
            expression = calc_expression;
            static_cast<OperationAssignment*>(assignment)->SetStatementType(ASSIGN_STMT);
            AnalyzeCalculation(calc_expression, depth + 1);
          }
        }
      }
    }

    if(check_right_cast) {
      Expression* box_expression = AnalyzeRightCast(variable, expression, (IsScalar(variable) && IsScalar(expression)), depth + 1);
      if(box_expression) {
        AnalyzeExpression(box_expression, depth + 1);
        assignment->SetExpression(box_expression);
      }
    }

    if(expression->GetExpressionType() == METHOD_CALL_EXPR) {
      MethodCall* method_call = static_cast<MethodCall*>(expression);
      // 'Nil' return check
      if(method_call->GetMethod() && method_call->GetMethod()->GetReturn()->GetType() == NIL_TYPE &&
         !method_call->IsFunctionDefinition()) {
        ProcessError(expression, L"Invalid assignment method '" + method_call->GetMethod()->GetName() + L"(..)' does not return a value");
      }
      else if(method_call->GetEvalType() && method_call->GetEvalType()->GetType() == NIL_TYPE) {
        ProcessError(expression, L"Invalid assignment, call does not return a value");
      }
    }
  }

  in_assignment = false;
}

/****************************
 * Analyzes a logical or mathematical
 * operation.
 ****************************/
void ContextAnalyzer::AnalyzeCalculation(CalculatedExpression* expression, const int depth)
{
  Type* cls_type = nullptr;
  Expression* left = expression->GetLeft();
  Expression* right = expression->GetRight();

  if(left && right) {
    switch(left->GetExpressionType()) {
    case AND_EXPR:
    case OR_EXPR:
    case EQL_EXPR:
    case NEQL_EXPR:
    case LES_EXPR:
    case GTR_EXPR:
    case LES_EQL_EXPR:
    case GTR_EQL_EXPR:
    case ADD_EXPR:
    case SUB_EXPR:
    case MUL_EXPR:
    case DIV_EXPR:
    case MOD_EXPR:
    case SHL_EXPR:
    case SHR_EXPR:
    case BIT_AND_EXPR:
    case BIT_OR_EXPR:
    case BIT_XOR_EXPR:
      AnalyzeCalculation(static_cast<CalculatedExpression*>(left), depth + 1);
      break;

    default:
      break;
    }

    switch(right->GetExpressionType()) {
    case AND_EXPR:
    case OR_EXPR:
    case EQL_EXPR:
    case NEQL_EXPR:
    case LES_EXPR:
    case GTR_EXPR:
    case LES_EQL_EXPR:
    case GTR_EQL_EXPR:
    case ADD_EXPR:
    case SUB_EXPR:
    case MUL_EXPR:
    case DIV_EXPR:
    case MOD_EXPR:
    case SHL_EXPR:
    case SHR_EXPR:
    case BIT_AND_EXPR:
    case BIT_OR_EXPR:
    case BIT_XOR_EXPR:
      AnalyzeCalculation(static_cast<CalculatedExpression*>(right), depth + 1);
      break;

    default:
      break;
    }
    AnalyzeExpression(left, depth + 1);
    AnalyzeExpression(right, depth + 1);

    // check operations
    AnalyzeCalculationCast(expression, depth);

    // check for valid operation cast
    if(left->GetCastType() && left->GetEvalType()) {
      AnalyzeRightCast(left->GetCastType(), left->GetEvalType(), left, IsScalar(left), depth);
    }

    // check for valid operation cast
    if(right->GetCastType() && right->GetEvalType()) {
      AnalyzeRightCast(right->GetCastType(), right->GetEvalType(), right, IsScalar(right), depth);
    }

    switch(expression->GetExpressionType()) {
    case AND_EXPR:
    case OR_EXPR:
      if(!IsBooleanExpression(left) || !IsBooleanExpression(right)) {
        ProcessError(expression, L"Invalid mathematical operation");
      }
      break;

    case EQL_EXPR:
    case NEQL_EXPR:
      if(IsBooleanExpression(left) && !IsBooleanExpression(right)) {
        ProcessError(expression, L"Invalid mathematical operation");
      }
      else if(!IsBooleanExpression(left) && IsBooleanExpression(right)) {
        ProcessError(expression, L"Invalid mathematical operation");
      }
      expression->SetEvalType(TypeFactory::Instance()->MakeType(BOOLEAN_TYPE), true);
      break;

    case LES_EXPR:
    case GTR_EXPR:
    case LES_EQL_EXPR:
    case GTR_EQL_EXPR:
      if(IsBooleanExpression(left) || IsBooleanExpression(right)) {
        ProcessError(expression, L"Invalid mathematical operation");
      }
      else if(IsEnumExpression(left) && IsEnumExpression(right)) {
        ProcessError(expression, L"Invalid mathematical operation");
      }
      else if((left->GetEvalType() && left->GetEvalType()->GetType() == NIL_TYPE) || (right->GetEvalType() && right->GetEvalType()->GetType() == NIL_TYPE)) {
        ProcessError(expression, L"Invalid mathematical operation");
      }
      expression->SetEvalType(TypeFactory::Instance()->MakeType(BOOLEAN_TYPE), true);
      break;

    case MOD_EXPR: {
      if(IsBooleanExpression(left) || IsBooleanExpression(right)) {
        ProcessError(expression, L"Invalid mathematical operation");
      }
      else if(((cls_type = GetExpressionType(left, depth + 1)) && cls_type->GetType() == CLASS_TYPE) || ((cls_type = GetExpressionType(right, depth + 1)) && cls_type->GetType() == CLASS_TYPE)) {
        const std::wstring cls_name = cls_type->GetName();
        if(cls_name != L"System.ByteRef" && cls_name != L"System.CharRef" && cls_name != L"System.IntRef") {
          ProcessError(expression, L"Invalid mathematical operation");
        }
      }

      Type* expr_type = GetExpressionType(left, depth + 1);
      if(expr_type && expr_type->GetType() == FLOAT_TYPE && left->GetEvalType()) {
        if(left->GetCastType()) {
          switch(left->GetCastType()->GetType()) {
          case BYTE_TYPE:
          case INT_TYPE:
          case CHAR_TYPE:
            break;
          default:
            ProcessError(expression, L"Expected Byte, Char, Int or Enum class type\n\tConsider 'Float->Mod(..)' for floating point values");
            break;
          }
        }
        else {
          ProcessError(expression, L"Expected Byte, Char, Int Enum class type\n\tConsider 'Float->Mod(..)' for floating point values");
        }
      }

      expr_type = GetExpressionType(right, depth + 1);
      if(expr_type && expr_type->GetType() == FLOAT_TYPE && right->GetEvalType()) {
        if(right->GetCastType()) {
          switch(right->GetCastType()->GetType()) {
          case BYTE_TYPE:
          case INT_TYPE:
          case CHAR_TYPE:
            break;
          default:
            ProcessError(expression, L"Expected Byte, Char, Int Enum class type");
            break;
          }
        }
        else {
          ProcessError(expression, L"Expected Byte, Char, Int Enum class type");
        }
      }
    }
      break;

    case ADD_EXPR:
    case SUB_EXPR:
    case MUL_EXPR:
    case DIV_EXPR:
    case SHL_EXPR:
    case SHR_EXPR:
    case BIT_AND_EXPR:
    case BIT_OR_EXPR:
    case BIT_XOR_EXPR:
      if(IsBooleanExpression(left) || IsBooleanExpression(right)) {
        ProcessError(expression, L"Invalid mathematical operation");
      }
      break;

    default:
      break;
    }
  }
}

/****************************
 * Preforms type conversions
 * operational expressions.  This
 * method uses execution simulation.
 ****************************/
void ContextAnalyzer::AnalyzeCalculationCast(CalculatedExpression* expression, const int depth)
{
  Expression* left_expr = expression->GetLeft();
  Expression* right_expr = expression->GetRight();

  Type* left = GetExpressionType(left_expr, depth + 1);
  Type* right = GetExpressionType(right_expr, depth + 1);

  if(!left || !right) {
    return;
  }

  if(!IsScalar(left_expr) || !IsScalar(right_expr)) {
    // check for valid equal and not-equal checks, consider Nil and array based equal and not-equal checks
    if(!(right->GetType() == NIL_TYPE && (expression->GetExpressionType() == EQL_EXPR || expression->GetExpressionType() == NEQL_EXPR)) && left->GetDimension() != right->GetDimension()) {
      ProcessError(left_expr, L"Invalid array based calculation");
    }
  }
  else {
    switch(left->GetType()) {
    case VAR_TYPE:
      // VAR
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Var and Function");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Var and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Var and Nil");
        break;

      case BYTE_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Var and System.Byte");
        break;

      case CHAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Var and System.Char");
        break;

      case INT_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Var and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Var and System.Float");
        break;

      case CLASS_TYPE:
        if(HasProgramOrLibraryEnum(right->GetName())) {
          ProcessError(left_expr, L"Invalid operation using classes: Var and Enum");
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Var and System.Bool");
        break;
      }
      break;
        
    case ALIAS_TYPE:
      break;

    case NIL_TYPE:
      // NIL
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Nil and function reference");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Nil and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Nil and Nil");
        break;

      case BYTE_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Nil and System.Byte");
        break;

      case CHAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Nil and System.Char");
        break;

      case INT_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Nil and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Nil and System.Float");
        break;

      case CLASS_TYPE:
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Nil and System.Bool");
        break;
      }
      break;

    case BYTE_TYPE:
      // BYTE
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Byte and function reference");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Byte and Var");
        break;
      
      case ALIAS_TYPE:
        break;
          
      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Byte and Nil");
        break;

      case CHAR_TYPE:
      case INT_TYPE:
      case BYTE_TYPE:
        expression->SetEvalType(left, true);
        break;

      case FLOAT_TYPE:
        left_expr->SetCastType(right, true);
        expression->SetEvalType(right, true);
        break;

      case CLASS_TYPE:
        if(HasProgramOrLibraryEnum(right->GetName())) {
          right_expr->SetCastType(left, true);
          expression->SetEvalType(left, true);
        }
        else if(!UnboxingCalculation(right, right_expr, expression, false, depth)) {
          ProcessError(left_expr, L"Invalid operation using classes: System.Int and " +
                       FormatTypeString(right->GetName()));
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Byte and System.Bool");
        break;
      }
      break;

    case CHAR_TYPE:
      // CHAR
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Char and function reference");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Char and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Char and Nil");
        break;

      case INT_TYPE:
      case CHAR_TYPE:
      case BYTE_TYPE:
        expression->SetEvalType(left, true);
        break;

      case FLOAT_TYPE:
        left_expr->SetCastType(right, true);
        expression->SetEvalType(right, true);
        break;

      case CLASS_TYPE:
        if(HasProgramOrLibraryEnum(right->GetName())) {
          right_expr->SetCastType(left, true);
          expression->SetEvalType(left, true);
        }
        else if(!UnboxingCalculation(right, right_expr, expression, false, depth)) {
          ProcessError(left_expr, L"Invalid operation using classes: System.Int and " +
                       FormatTypeString(right->GetName()));
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes:  Char and System.Bool");
        break;
      }
      break;

    case INT_TYPE:
      // INT
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Int and function reference");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Int and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Int and Nil");
        break;

      case BYTE_TYPE:
      case CHAR_TYPE:
      case INT_TYPE:
        expression->SetEvalType(left, true);
        break;

      case FLOAT_TYPE:
        left_expr->SetCastType(right, true);
        expression->SetEvalType(right, true);
        break;

      case CLASS_TYPE:
        if(HasProgramOrLibraryEnum(right->GetName())) {
          right_expr->SetCastType(left, true);
          expression->SetEvalType(left, true);
        }
        else if(!UnboxingCalculation(right, right_expr, expression, false, depth)) {
          ProcessError(left_expr, L"Invalid operation using classes: System.Int and " +
                       FormatTypeString(right->GetName()));
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Int and System.Bool");
        break;
      }
      break;

    case FLOAT_TYPE:
      // FLOAT
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Float and function reference");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Float and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Float and Nil");
        break;

      case FLOAT_TYPE:
        expression->SetEvalType(left, true);
        break;

      case BYTE_TYPE:
      case CHAR_TYPE:
      case INT_TYPE:
        right_expr->SetCastType(left, true);
        expression->SetEvalType(left, true);
        break;

      case CLASS_TYPE:
        if(HasProgramOrLibraryEnum(right->GetName())) {
          right_expr->SetCastType(left, true);
          expression->SetEvalType(left, true);
        }
        else if(UnboxingCalculation(right, right_expr, expression, false, depth)) {
          expression->SetEvalType(left, true);
        }
        else {
          ProcessError(left_expr, L"Invalid operation using classes: System.Float and " +
                       FormatTypeString(right->GetName()));
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Float and System.Bool");
        break;
      }
      break;

    case CLASS_TYPE:
      // CLASS
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: " +
                     FormatTypeString(left->GetName()) +
                     L" and function reference");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: " +
                     FormatTypeString(left->GetName()) +
                     L" and Var");
        break;

      case ALIAS_TYPE:
      case NIL_TYPE:
        break;

      case BYTE_TYPE:
        if(HasProgramOrLibraryEnum(left->GetName())) {
          left_expr->SetCastType(right, true);
          expression->SetEvalType(right, true);
        }
        else if(!UnboxingCalculation(left, left_expr, expression, true, depth)) {
          ProcessError(left_expr, L"Invalid operation using classes: " +
                       FormatTypeString(left->GetName()) + L" and System.Byte");
        }
        break;

      case CHAR_TYPE:
        if(HasProgramOrLibraryEnum(left->GetName())) {
          left_expr->SetCastType(right, true);
          expression->SetEvalType(right, true);
        }
        else if(!UnboxingCalculation(left, left_expr, expression, true, depth)) {
          ProcessError(left_expr, L"Invalid operation using classes: " +
                       FormatTypeString(left->GetName()) + L" and System.Char");
        }
        break;

      case INT_TYPE:
        if(HasProgramOrLibraryEnum(left->GetName())) {
          left_expr->SetCastType(right, true);
          expression->SetEvalType(right, true);
        }
        else if(!UnboxingCalculation(left, left_expr, expression, true, depth)) {
          ProcessError(left_expr, L"Invalid operation using classes: " + FormatTypeString(left->GetName()) + L" and System.Int");
        }
        break;

      case FLOAT_TYPE:
        if(HasProgramOrLibraryEnum(left->GetName())) {
          left_expr->SetCastType(right, true);
          expression->SetEvalType(right, true);
        } 
        else if(!UnboxingCalculation(left, left_expr, expression, true, depth)) {
          ProcessError(left_expr, L"Invalid operation using classes: " + FormatTypeString(left->GetName()) + L" and System.Float");
        }
        break;

      case CLASS_TYPE: {
        ResolveClassEnumType(left);
        const bool can_unbox_left = UnboxingCalculation(left, left_expr, expression, true, depth);
        const bool is_left_enum = HasProgramOrLibraryEnum(left->GetName());

        ResolveClassEnumType(right);
        const bool can_unbox_right = UnboxingCalculation(right, right_expr, expression, false, depth);
        const bool is_right_enum = HasProgramOrLibraryEnum(right->GetName());

        if(!can_unbox_left && !is_left_enum && !can_unbox_right && !is_right_enum && 
           expression->GetExpressionType() != EQL_EXPR && expression->GetExpressionType() != NEQL_EXPR) {
          ProcessError(left_expr, L"Invalid operation using classes: " + FormatTypeString(left->GetName()) + L" and " + FormatTypeString(right->GetName()));
        }
        
        if((is_left_enum && !is_right_enum) || (!is_left_enum && is_right_enum)) {
          ProcessError(left_expr, L"Invalid operation between class and enum: '" + FormatTypeString(left->GetName()) + 
                       L"' and '" + FormatTypeString(right->GetName()) + L"'");
        }
        else if((is_left_enum && is_right_enum) || (can_unbox_left && can_unbox_right)) {
          AnalyzeClassCast(left, right, left_expr, false, depth + 1);
        }
        else if(can_unbox_left && !is_right_enum) {
          ProcessError(left_expr, L"Invalid operation between class and enum: '" + left->GetName() + L"' and '" + right->GetName() + L"'");
        }
        else if(can_unbox_right && !is_left_enum) {
          ProcessError(left_expr, L"Invalid operation between class and enum: '" + left->GetName() + L"' and '" + right->GetName() + L"'");
        }
        
        if(left->GetName() == L"System.FloatRef" || right->GetName() == L"System.FloatRef") {
          expression->SetEvalType(TypeFactory::Instance()->MakeType(FLOAT_TYPE), true);
        }
        else {
          expression->SetEvalType(TypeFactory::Instance()->MakeType(INT_TYPE), true);
        }
      }
        break;
        
      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: " + FormatTypeString(left->GetName()) + L" and System.Bool");
        break;
      }
      break;

    case BOOLEAN_TYPE:
      // BOOLEAN
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Bool and function reference");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Bool and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Bool and Nil");
        break;

      case BYTE_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Bool and System.Byte");
        break;

      case CHAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Bool and System.Char");
        break;

      case INT_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Bool and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Bool and System.Float");
        break;

      case CLASS_TYPE:
        if(HasProgramOrLibraryEnum(right->GetName())) {
          right_expr->SetCastType(left, true);
          expression->SetEvalType(left, true);
        }
        else if(!UnboxingCalculation(right, right_expr, expression, false, depth)) {
          ProcessError(left_expr, L"Invalid operation using classes: System.Bool and " +
                       FormatTypeString(right->GetName()));
        }
        break;
        
      case BOOLEAN_TYPE:
        expression->SetEvalType(left, true);
        break;
      }
      break;

    case FUNC_TYPE:
      // FUNCTION
      switch(right->GetType()) {
      case FUNC_TYPE: {
        AnalyzeVariableFunctionParameters(left, expression);
        if(left->GetName().size() == 0) {
          left->SetName(L"m." + EncodeFunctionType(left->GetFunctionParameters(),
                             left->GetFunctionReturn()));
        }

        if(right->GetName().size() == 0) {
          right->SetName(L"m." + EncodeFunctionType(right->GetFunctionParameters(),
                              right->GetFunctionReturn()));
        }

        if(left->GetName() != right->GetName()) {
          ProcessError(expression, L"Invalid operation using functions: " +
                       FormatTypeString(left->GetName()) + L" and " +
                       FormatTypeString(right->GetName()));
        }
      }
                      break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: function reference and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: function reference and Nil");
        break;

      case BYTE_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: function reference and System.Byte");
        break;

      case CHAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: function reference and System.Char");
        break;

      case INT_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: function reference and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: function reference and System.Float");
        break;

      case CLASS_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: function reference and " +
                     FormatTypeString(right->GetName()));
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: function reference and System.Bool");
        break;
      }
      break;
    }
  }
}

bool ContextAnalyzer::UnboxingCalculation(Type* type, Expression* expression, CalculatedExpression* calc_expression, bool set_left, const int depth)
{
  if(!type || !expression) {
    return false;
  }

  ResolveClassEnumType(type);
  if(expression->GetExpressionType() == VAR_EXPR && IsHolderType(type->GetName())) {
    ExpressionList* box_expressions = TreeFactory::Instance()->MakeExpressionList();
    MethodCall* box_method_call = TreeFactory::Instance()->MakeMethodCall(expression->GetFileName(), expression->GetLineNumber(), expression->GetLinePosition(),
                                                                          -1, -1, expression->GetLineNumber(), expression->GetLinePosition(),
                                                                          static_cast<Variable*>(expression), L"Get", box_expressions);
    AnalyzeMethodCall(box_method_call, depth + 1);

    if(set_left) {
      calc_expression->SetLeft(box_method_call);
    }
    else {
      calc_expression->SetRight(box_method_call);
    }

    AnalyzeCalculationCast(calc_expression, depth + 1);
    return true;
  }
  else if(expression->GetExpressionType() == METHOD_CALL_EXPR && IsHolderType(type->GetName())) {
    ExpressionList* box_expressions = TreeFactory::Instance()->MakeExpressionList();
    MethodCall* box_method_call = TreeFactory::Instance()->MakeMethodCall(expression->GetFileName(), expression->GetLineNumber(), expression->GetLinePosition(), 
                                                                          -1, -1, expression->GetLineNumber(), expression->GetLinePosition(),
                                                                          expression->GetEvalType()->GetName(), L"Get", box_expressions);
    expression->SetMethodCall(box_method_call);
    AnalyzeExpression(calc_expression, depth + 1);
    return true;
  }

  return false;
}

MethodCall* ContextAnalyzer::BoxUnboxingReturn(Type* to_type, Expression* from_expr, const int depth)
{
  if(to_type && from_expr) {
    ResolveClassEnumType(to_type);

    Type* from_type = from_expr->GetEvalType();
    if(!from_type) {
      from_type = from_expr->GetBaseType();
    }

    if(!from_type) {
      return nullptr;
    }

    ResolveClassEnumType(from_type);

    switch(to_type->GetType()) {
    case BOOLEAN_TYPE:
    case BYTE_TYPE:
    case CHAR_TYPE:
    case INT_TYPE:
    case FLOAT_TYPE: {
      if(from_expr->GetExpressionType() == METHOD_CALL_EXPR && IsHolderType(from_type->GetName())) {
        ExpressionList* box_expressions = TreeFactory::Instance()->MakeExpressionList();
        MethodCall* box_method_call = TreeFactory::Instance()->MakeMethodCall(from_expr->GetFileName(), from_expr->GetLineNumber(), from_expr->GetLinePosition(),
                                                                              -1, -1, -1, -1, from_expr->GetEvalType()->GetName(), L"Get", box_expressions);
        
        from_expr->SetMethodCall(box_method_call);
        AnalyzeMethodCall(static_cast<MethodCall*>(from_expr), depth);
        return static_cast<MethodCall*>(from_expr);
      }
    }
      break;

    case CLASS_TYPE: {
      switch(from_type->GetType()) {
      case BOOLEAN_TYPE:
      case BYTE_TYPE:
      case CHAR_TYPE:
      case INT_TYPE:
      case FLOAT_TYPE:
        if(IsHolderType(to_type->GetName())) {
          ExpressionList* box_expressions = TreeFactory::Instance()->MakeExpressionList();
          box_expressions->AddExpression(from_expr);
          MethodCall* box_method_call = TreeFactory::Instance()->MakeMethodCall(from_expr->GetFileName(), from_expr->GetLineNumber(), from_expr->GetLinePosition(), 
                                                                                -1, -1, NEW_INST_CALL, to_type->GetName(), box_expressions);
          AnalyzeMethodCall(box_method_call, depth);
          return box_method_call;
      }
        break;

      default:
        break;
      }
    }
      break;

    default:
      break;
    }
  }

  return nullptr;
}

/****************************
 * Preforms type conversions for
 * assignment statements.  This
 * method uses execution simulation.
 ****************************/
Expression* ContextAnalyzer::AnalyzeRightCast(Variable* variable, Expression* expression, bool is_scalar, const int depth)
{
  Expression* box_expression = AnalyzeRightCast(variable->GetEvalType(), GetExpressionType(expression, depth + 1), expression, is_scalar, depth);
  if(variable->GetIndices() && !is_scalar) {
    ProcessError(expression, L"Dimension size mismatch");
  }

  return box_expression;
}

Expression* ContextAnalyzer::AnalyzeRightCast(Type* left, Expression* expression, bool is_scalar, const int depth)
{
  return AnalyzeRightCast(left, GetExpressionType(expression, depth + 1), expression, is_scalar, depth);
}

Expression* ContextAnalyzer::AnalyzeRightCast(Type* left, Type* right, Expression* expression, bool is_scalar, const int depth)
{
  // assert(left && right);
  if(!expression || !left || !right) {
    return nullptr;
  }

  // scalar
  if(is_scalar) {
    switch(left->GetType()) {
    case VAR_TYPE:
      // VAR
      switch(right->GetType()) {
      case ALIAS_TYPE:
        break;
          
      case VAR_TYPE:
        ProcessError(expression, L"Invalid operation using classes: Var and Var");
        break;

      case NIL_TYPE:
      case BYTE_TYPE:
      case CHAR_TYPE:
      case INT_TYPE:
      case FLOAT_TYPE:
      case CLASS_TYPE:
      case BOOLEAN_TYPE:
        break;

      default:
        break;
      }
      break;

    case NIL_TYPE:
      // NIL
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(expression, L"Invalid operation using classes: Nil and function reference");
        break;

      case VAR_TYPE:
        ProcessError(expression, L"Invalid operation using classes: Nil and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(expression, L"Invalid operation with Nil");
        break;

      case BYTE_TYPE:
        ProcessError(expression, L"Invalid cast with classes: Nil and System.Byte");
        break;

      case CHAR_TYPE:
        ProcessError(expression, L"Invalid cast with classes: Nil and System.Char");
        break;

      case INT_TYPE:
        ProcessError(expression, L"Invalid cast with classes: Nil and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(expression, L"Invalid cast with classes: Nil and System.Float");
        break;

      case CLASS_TYPE:
        ProcessError(expression, L"Invalid cast with classes: Nil and " +
                     FormatTypeString(right->GetName()));
        break;

      case BOOLEAN_TYPE:
        ProcessError(expression, L"Invalid cast with classes: Nil and System.Bool");
        break;
      }
      break;

    case BYTE_TYPE:
      // BYTE
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(expression, L"Invalid operation using classes: System.Byte and function reference");
        break;

      case VAR_TYPE:
        ProcessError(expression, L"Invalid operation using classes: System.Byte and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        if(left->GetDimension() < 1) {
          ProcessError(expression, L"Invalid cast with classes: System.Byte and Nil");
        }
        break;

      case BYTE_TYPE:
      case CHAR_TYPE:
      case INT_TYPE:
        if(expression->GetEvalType() && expression->GetEvalType()->GetType() != FLOAT_TYPE) {
          expression->SetEvalType(left, false);
        }
        break;

      case FLOAT_TYPE:
        expression->SetCastType(left, false);
        expression->SetEvalType(right, false);
        break;

      case CLASS_TYPE:
        if(!HasProgramOrLibraryEnum(right->GetName())) {
          Expression* unboxed_expresion = UnboxingExpression(right, expression, true, depth);
          if(unboxed_expresion) {
            return unboxed_expresion;
          }
          else {
            ProcessError(expression, L"Invalid cast with classes: System.Byte and " +
                         FormatTypeString(right->GetName()));
          }
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(expression, L"Invalid cast with classes: System.Byte and System.Bool");
        break;
      }
      break;

    case CHAR_TYPE:
      // CHAR
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(expression, L"Invalid operation using classes: System.Char and function reference");
        break;

      case VAR_TYPE:
        ProcessError(expression, L"Invalid operation using classes: System.Char and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        if(left->GetDimension() < 1) {
          ProcessError(expression, L"Invalid cast with classes: System.Char and Nil");
        }
        break;

      case CHAR_TYPE:
      case BYTE_TYPE:
      case INT_TYPE:
        if(expression->GetEvalType() && expression->GetEvalType()->GetType() != FLOAT_TYPE) {
          expression->SetEvalType(left, false);
        }
        break;

      case FLOAT_TYPE:
        expression->SetCastType(left, false);
        expression->SetEvalType(right, false);
        break;

      case CLASS_TYPE:
        if(!HasProgramOrLibraryEnum(right->GetName())) {
          Expression* unboxed_expresion = UnboxingExpression(right, expression, true, depth);
          if(unboxed_expresion) {
            return unboxed_expresion;
          }
          else {
            ProcessError(expression, L"Invalid cast with classes: System.Char and " +  
                         FormatTypeString(right->GetName()));
         }
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(expression, L"Invalid cast with classes: System.Char and System.Bool");
        break;
      }
      break;

    case INT_TYPE:
      // INT
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(expression, L"Invalid operation using classes: System.Int and function reference");
        break;

      case VAR_TYPE:
        ProcessError(expression, L"Invalid operation using classes: Var and Int");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        if(left->GetDimension() < 1) {
          ProcessError(expression, L"Invalid cast with classes: System.Int and Nil");
        }
        break;

      case INT_TYPE:
      case BYTE_TYPE:
      case CHAR_TYPE:
        if(expression->GetEvalType() && expression->GetEvalType()->GetType() != FLOAT_TYPE) {
          expression->SetEvalType(left, false);
        }
        break;

      case FLOAT_TYPE:
        expression->SetCastType(left, false);
        expression->SetEvalType(right, false);
        break;

      case CLASS_TYPE:
        if(!HasProgramOrLibraryEnum(right->GetName())) {
          Expression* unboxed_expresion = UnboxingExpression(right, expression, true, depth);
          if(unboxed_expresion) {
            return unboxed_expresion;
          }
          else {
            ProcessError(expression, L"Invalid cast with classes: System.Int and " +
                         FormatTypeString(right->GetName()));
          }
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(expression, L"Invalid cast with classes: System.Int and System.Bool");
        break;
      }
      break;

    case FLOAT_TYPE:
      // FLOAT
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(expression, L"Invalid operation using classes: System.Float and function reference");
        break;

      case VAR_TYPE:
        ProcessError(expression, L"Invalid operation using classes: Nil and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        if(left->GetDimension() < 1) {
          ProcessError(expression, L"Invalid cast with classes: System.Float and Nil");
        }
        break;

      case FLOAT_TYPE:
        if(expression->GetEvalType() && expression->GetEvalType()->GetType() != INT_TYPE) {
          expression->SetEvalType(left, false);
        }
        break;

      case BYTE_TYPE:
      case CHAR_TYPE:
      case INT_TYPE:
        expression->SetCastType(left, false);
        expression->SetEvalType(right, false);
        break;

      case CLASS_TYPE:
        if(HasProgramOrLibraryEnum(right->GetName())) {
          expression->SetCastType(left, false);
        }
        else {
          ProcessError(expression, L"Invalid cast with classes: System.Float and " +
                       FormatTypeString(FormatTypeString(right->GetName())));
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(expression, L"Invalid cast with classes: System.Float and System.Bool");
        break;
      }
      break;

    case CLASS_TYPE:
      // CLASS
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(expression, L"Invalid operation using classes: " +
                     FormatTypeString(left->GetName()) + L" and function reference");
        break;

      case VAR_TYPE:
        ProcessError(expression, L"Invalid cast with classes: " +
                     FormatTypeString(left->GetName()) + L" and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        expression->SetCastType(left, false);
        expression->SetEvalType(right, false);
        break;

      case BYTE_TYPE:
        if(!HasProgramOrLibraryEnum(left->GetName())) {
          Expression* boxed_expression = BoxExpression(left, expression, depth);
          if(boxed_expression) {
            return boxed_expression;
          }
          ProcessError(expression, L"Invalid cast with classes: " + FormatTypeString(left->GetName()) + L" and System.Byte");
        }
        break;

      case CHAR_TYPE:
        if(!HasProgramOrLibraryEnum(left->GetName())) {
          Expression* boxed_expression = BoxExpression(left, expression, depth);
          if(boxed_expression) {
            return boxed_expression;
          }
          ProcessError(expression, L"Invalid cast with classes: " + FormatTypeString(left->GetName()) + L" and System.Char");
        }
        break;

      case INT_TYPE:
        if(!HasProgramOrLibraryEnum(left->GetName())) {
          Expression* boxed_expression = BoxExpression(left, expression, depth);
          if(boxed_expression) {
            return boxed_expression;
          }
          ProcessError(expression, L"Invalid cast with classes: " + FormatTypeString(left->GetName()) + L" and Int");
        }
        break;

      case FLOAT_TYPE:
        if(!HasProgramOrLibraryEnum(left->GetName())) {
          Expression* boxed_expression = BoxExpression(left, expression, depth);
          if(boxed_expression) {
            return boxed_expression;
          }
          ProcessError(expression, L"Invalid cast with classes: " + FormatTypeString(left->GetName()) + L" and System.Float");
        }
        break;

      case CLASS_TYPE:
        AnalyzeClassCast(left, expression, depth + 1);
        break;

      case BOOLEAN_TYPE:
        if(!HasProgramOrLibraryEnum(left->GetName())) {
          Expression* boxed_expression = BoxExpression(left, expression, depth);
          if(boxed_expression) {
            return boxed_expression;
          }
          ProcessError(expression, L"Invalid cast with classes: " + left->GetName() + L" and System.Bool");
        }
        else {
          ProcessError(expression, L"Invalid cast with classes: " + FormatTypeString(left->GetName()) + L" and System.Bool");
        }
        break;
      }
      break;
      
    case BOOLEAN_TYPE:
      // BOOLEAN
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(expression, L"Invalid operation using classes: System.Bool and function reference");
        break;

      case VAR_TYPE:
        ProcessError(expression, L"Invalid operation using classes: System.Bool and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        if(left->GetDimension() < 1) {
          ProcessError(expression, L"Invalid cast with classes: System.Bool and Nil");
        }
        break;

      case BYTE_TYPE:
        ProcessError(expression, L"Invalid cast with classes: System.Bool and System.Byte");
        break;

      case CHAR_TYPE:
        ProcessError(expression, L"Invalid cast with classes: System.Bool and System.Char");
        break;

      case INT_TYPE:
        ProcessError(expression, L"Invalid cast with classes: System.Bool and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(expression, L"Invalid cast with classes: System.Bool and System.Float");
        break;

      case CLASS_TYPE:
        if(!HasProgramOrLibraryEnum(right->GetName())) {
          Expression* unboxed_expresion = UnboxingExpression(right, expression, true, depth);
          if(unboxed_expresion) {
            return unboxed_expresion;
          }
          else {
            ProcessError(expression, L"Invalid cast with classes: System.Bool and " +
                         FormatTypeString(FormatTypeString(right->GetName())));
          }
        }
        break;
        
      case BOOLEAN_TYPE:
        break;
      }
      break;

    case FUNC_TYPE:
      // FUNCTION
      switch(right->GetType()) {
      case FUNC_TYPE: {
        AnalyzeVariableFunctionParameters(left, expression);
        if(left->GetName().size() == 0) {
          left->SetName(L"m." + EncodeFunctionType(left->GetFunctionParameters(), left->GetFunctionReturn()));
        }

        if(right->GetName().size() == 0) {
          right->SetName(L"m." + EncodeFunctionType(right->GetFunctionParameters(), right->GetFunctionReturn()));
        }
      }
        break;

      case VAR_TYPE:
        ProcessError(expression, L"Invalid operation using classes: function reference and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(expression, L"Invalid cast with classes: function reference and Nil");
        break;

      case BYTE_TYPE:
        ProcessError(expression, L"Invalid cast with classes: function reference and System.Byte");
        break;

      case CHAR_TYPE:
        ProcessError(expression, L"Invalid cast with classes: function reference and System.Char");
        break;

      case INT_TYPE:
        ProcessError(expression, L"Invalid cast with classes: function reference and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(expression, L"Invalid cast with classes: function reference and System.Float");
        break;

      case CLASS_TYPE:
        ProcessError(expression, L"Invalid cast with classes: function reference and " +
                     FormatTypeString(FormatTypeString(right->GetName())));
        break;

      case BOOLEAN_TYPE:
        ProcessError(expression, L"Invalid cast with classes: function reference and System.Bool");
        break;
      }
      break;

    default:
      break;
    }
  }
  // multi-dimensional
  else {
    if(left->GetDimension() != right->GetDimension() &&
       right->GetType() != NIL_TYPE) {
      ProcessError(expression, L"Dimension size mismatch");
    }

    if(left->GetType() != right->GetType() && right->GetType() != NIL_TYPE) {
      ProcessError(expression, L"Invalid array cast");
    }

    if(left->GetType() == CLASS_TYPE && right->GetType() == CLASS_TYPE) {
      AnalyzeClassCast(left, expression, depth + 1);
    }

    expression->SetEvalType(left, false);
  }

  return nullptr;
}

//
// Unboxing expression
//
Expression* ContextAnalyzer::UnboxingExpression(Type* to_type, Expression* from_expr, bool is_cast, int depth)
{
  if(!to_type || !from_expr) {
    return nullptr;
  }
  
  Type* from_type = GetExpressionType(from_expr, depth);
  if(!from_type) {
    return nullptr;
  }
  
  ResolveClassEnumType(to_type);
  ResolveClassEnumType(from_type);
  
  if(to_type->GetType() == CLASS_TYPE && (from_type->GetType() != CLASS_TYPE || is_cast)) {
    if(from_expr->GetExpressionType() == VAR_EXPR && IsHolderType(to_type->GetName())) {
      MethodCall* box_method_call = TreeFactory::Instance()->MakeMethodCall(from_expr->GetFileName(), from_expr->GetLineNumber(), from_expr->GetLinePosition(),
                                                                            -1, -1, -1, -1, static_cast<Variable*>(from_expr), L"Get", TreeFactory::Instance()->MakeExpressionList());
      AnalyzeMethodCall(box_method_call, depth);
      return box_method_call;
    }
    else if(from_expr->GetExpressionType() == METHOD_CALL_EXPR && IsHolderType(to_type->GetName())) {
      MethodCall* box_method_call = TreeFactory::Instance()->MakeMethodCall(from_expr->GetFileName(), from_expr->GetLineNumber(), from_expr->GetLinePosition(),
                                                                            -1, -1, -1, -1, from_expr->GetEvalType()->GetName(), L"Get", TreeFactory::Instance()->MakeExpressionList());
      from_expr->SetMethodCall(box_method_call);
      AnalyzeMethodCall(static_cast<MethodCall*>(from_expr), depth);
      return from_expr;
    }
  }

  return nullptr;
}

//
// Boxing expression
//
Expression* ContextAnalyzer::BoxExpression(Type* to_type, Expression* from_expr, int depth)
{
  if(!to_type || !from_expr) {
    return nullptr;
  }

  ResolveClassEnumType(to_type);

  Type* from_type = GetExpressionType(from_expr, depth);
  if(!from_type) {
    return nullptr;
  }

  const bool is_enum = from_expr->GetExpressionType() == METHOD_CALL_EXPR && static_cast<MethodCall*>(from_expr)->GetEnumItem();
  if(to_type->GetType() == CLASS_TYPE && (is_enum ||
     from_type->GetType() == BOOLEAN_TYPE || 
     from_type->GetType() == BYTE_TYPE || 
     from_type->GetType() == CHAR_TYPE || 
     from_type->GetType() == INT_TYPE || 
     from_type->GetType() == FLOAT_TYPE)) {
    if(IsHolderType(to_type->GetName())) {
      ExpressionList* box_expressions = TreeFactory::Instance()->MakeExpressionList();
      box_expressions->AddExpression(from_expr);
      MethodCall* box_method_call = TreeFactory::Instance()->MakeMethodCall(from_expr->GetFileName(), from_expr->GetLineNumber(), from_expr->GetLinePosition(), 
                                                                            -1, -1, NEW_INST_CALL, to_type->GetName(), box_expressions);
      AnalyzeMethodCall(box_method_call, depth);
      return box_method_call;
    }
  }

  return nullptr;
}

/****************************
 * Analyzes a class cast. Up
 * casting is resolved a runtime.
 ****************************/
void ContextAnalyzer::AnalyzeClassCast(Type* left, Expression* expression, const int depth)
{
  if(expression->GetCastType() && expression->GetEvalType() && (expression->GetCastType()->GetType() != CLASS_TYPE || expression->GetEvalType()->GetType() != CLASS_TYPE)) {
    AnalyzeRightCast(expression->GetCastType(), expression->GetEvalType(), expression, IsScalar(expression), depth + 1);
  }

  Type* right = GetExpressionType(expression, depth + 1);
  AnalyzeClassCast(left, right, expression, false, depth);
}

void ContextAnalyzer::AnalyzeClassCast(Type* left, Type* right, Expression* expression, bool generic_check, const int depth)
{
  // this will be cause upstream
  if((left && left->GetType() != CLASS_TYPE) || (right && right->GetType() != CLASS_TYPE)) {
    return;
  }

  Class* left_class = nullptr;
  LibraryEnum* left_lib_enum = nullptr;
  LibraryClass* left_lib_class = nullptr;
  
  if(left && right) {
    if(current_class->HasGenerics() || left->HasGenerics() || right->HasGenerics()) {
      CheckGenericEqualTypes(left, right, expression);
    }

    if(current_class->HasGenerics()) {
      Class* left_tmp = current_class->GetGenericClass(left->GetName());
      if(left_tmp && left_tmp->HasGenericInterface()) {
        left = left_tmp->GetGenericInterface();
      }

      Class* right_tmp = current_class->GetGenericClass(right->GetName());
      if(right_tmp && right_tmp->HasGenericInterface()) {
        right = right_tmp->GetGenericInterface();
      }
    }
  }

  //
  // program enum
  //
  Enum* left_enum = GetExpressionEnum(left, expression, depth + 1);
  if(right && left_enum) {
    // program
    Enum* right_enum = SearchProgramEnums(right->GetName());
    if(right_enum) {
      if(left_enum->GetName() != right_enum->GetName()) {
        const std::wstring left_str = FormatTypeString(left->GetName());
        const std::wstring right_str = FormatTypeString(right->GetName());
        ProcessError(expression, L"Invalid cast between enums: '" + left_str + L"' and '" + right_str + L"'");
      }
    }
    // library
    else if(right && linker->SearchEnumLibraries(right->GetName(), program->GetLibUses(current_class->GetFileName()))) {
      LibraryEnum* right_lib_enum = linker->SearchEnumLibraries(right->GetName(), program->GetLibUses(current_class->GetFileName()));
      if(right_lib_enum && (left_enum->GetName() != right_lib_enum->GetName())) {
        const std::wstring left_str = FormatTypeString(left->GetName());
        const std::wstring right_str = FormatTypeString(right->GetName());
        ProcessError(expression, L"Invalid cast between enums: '" + left_str + L"' and '" + right_str + L"'");
      }
    }
    else {
      ProcessError(expression, L"Invalid cast between enum and class");
    }
  }
  //
  // program class
  //
  else if(left && right && (left_class = SearchProgramClasses(left->GetName()))) {
    // program and generic
    Class* right_class = SearchProgramClasses(right->GetName());
    if(!right_class) {
      right_class = current_class->GetGenericClass(right->GetName());
    }
    if(right_class) {
      // downcast
      if(ValidDownCast(left_class->GetName(), right_class, nullptr)) {
        left_class->SetCalled(true);
        right_class->SetCalled(true);
        if(left_class->IsInterface() && !generic_check) {
          expression->SetToClass(left_class);
        }
        return;
      }
      // upcast
      else if(right_class->IsInterface() || ValidUpCast(left_class->GetName(), right_class)) {
        expression->SetToClass(left_class);
        left_class->SetCalled(true);
        right_class->SetCalled(true);
        return;
      }
      // invalid cast
      else {
        expression->SetToClass(left_class);
        ProcessError(expression, L"Invalid cast between classes: '" +
                     FormatTypeString(left->GetName()) + L"' and '" +
                     FormatTypeString(right->GetName()) + L"'");
      }
    }
    // library
    else if(linker->SearchClassLibraries(right->GetName(), program->GetLibUses(current_class->GetFileName()))) {
      LibraryClass* right_lib_class = linker->SearchClassLibraries(right->GetName(), program->GetLibUses(current_class->GetFileName()));
      // downcast
      if(ValidDownCast(left_class->GetName(), nullptr, right_lib_class)) {
        if(left_class->IsInterface() && !generic_check) {
          expression->SetToClass(left_class);
        }
        return;
      }
      // upcast
      else if(right_lib_class && (right_lib_class->IsInterface() || ValidUpCast(left_class->GetName(), right_lib_class))) {
        expression->SetToClass(left_class);
        return;
      }
      // invalid cast
      else {
        expression->SetToClass(left_class);
        ProcessError(expression, L"Invalid cast between classes: '" +
                     FormatTypeString(left->GetName()) + L"' and '" +
                     FormatTypeString(right->GetName()) + L"'");
      }
    }
    else {
      ProcessError(expression, L"Invalid cast between class, enum or return type");
    }
  }
  //
  // generic class
  //
  else if(left && right && (left_class = current_class->GetGenericClass(left->GetName()))) {
    // program
    Class* right_class = current_class->GetGenericClass(right->GetName());
    if(right_class) {
      if(left->GetName() == right->GetName()) {
        return;
      }
      else {
        ProcessError(expression, L"Invalid cast between generics: '" +
                     FormatTypeString(left->GetName()) + L"' and '" +
                     FormatTypeString(right->GetName()) + L"'");
      }
    }
    else {
      ProcessError(expression, L"Invalid cast between generic: '" +
                   FormatTypeString(left->GetName()) + L"' and class/enum '" +
                   FormatTypeString(right->GetName()) + L"'");
    }
  }
  //
  // enum library
  //
  else if(left && right && (left_lib_enum = linker->SearchEnumLibraries(left->GetName(), program->GetLibUses(current_class->GetFileName())))) {
    // program
    Enum* right_enum = SearchProgramEnums(right->GetName());
    if(right_enum) {
      if(left_lib_enum->GetName() != right_enum->GetName()) {
        const std::wstring left_str = FormatTypeString(left_lib_enum->GetName());
        const std::wstring right_str = FormatTypeString(right_enum->GetName());
        ProcessError(expression, L"Invalid cast between enums: '" + left_str + L"' and '" + right_str + L"'");
      }
    }
    // library
    else if(linker->SearchEnumLibraries(right->GetName(), program->GetLibUses(current_class->GetFileName()))) {
      LibraryEnum* right_lib_enum = linker->SearchEnumLibraries(right->GetName(), program->GetLibUses(current_class->GetFileName()));
      if(left_lib_enum->GetName() != right_lib_enum->GetName()) {
        const std::wstring left_str = FormatTypeString(left_lib_enum->GetName());
        const std::wstring right_str = FormatTypeString(right_lib_enum->GetName());
        ProcessError(expression, L"Invalid cast between enums: '" + left_str + L"' and '" + right_str + L"'");
      }
    }
    else {
      ProcessError(expression, L"Invalid cast between enum and class");
    }
  }
  //
  // class library
  //
  else if(left && right && (left_lib_class = linker->SearchClassLibraries(left->GetName(), program->GetLibUses(current_class->GetFileName())))) {
    // program and generic
    Class* right_class = SearchProgramClasses(right->GetName());
    if(!right_class) {
      right_class = current_class->GetGenericClass(right->GetName());
    }
    if(right_class) {
      // downcast
      if(ValidDownCast(left_lib_class->GetName(), right_class, nullptr)) {
        left_lib_class->SetCalled(true);
        right_class->SetCalled(true);
        if(left_lib_class->IsInterface() && !generic_check) {
          expression->SetToLibraryClass(left_lib_class);
        }
        return;
      }
      // upcast
      else if(right_class->IsInterface() || ValidUpCast(left_lib_class->GetName(), right_class)) {
        expression->SetToLibraryClass(left_lib_class);
        left_lib_class->SetCalled(true);
        right_class->SetCalled(true);
        return;
      }
      // invalid cast
      else {
        ProcessError(expression, L"Invalid cast between classes: '" + FormatTypeString(left->GetName()) +
                     L"' and '" + FormatTypeString(right->GetName()) + L"'");
      }
    }
    // library
    else if(linker->SearchClassLibraries(right->GetName(), program->GetLibUses(current_class->GetFileName()))) {
      LibraryClass* right_lib_class = linker->SearchClassLibraries(right->GetName(), program->GetLibUses(current_class->GetFileName()));
      // downcast
      if(ValidDownCast(left_lib_class->GetName(), nullptr, right_lib_class)) {
        left_lib_class->SetCalled(true);
        right_lib_class->SetCalled(true);
        if(left_lib_class->IsInterface() && !generic_check) {
          expression->SetToLibraryClass(left_lib_class);
        }
        return;
      }
      // upcast
      else if(right_lib_class && (right_lib_class->IsInterface() || ValidUpCast(left_lib_class->GetName(), right_lib_class))) {
        expression->SetToLibraryClass(left_lib_class);
        left_lib_class->SetCalled(true);
        right_lib_class->SetCalled(true);
        return;
      }
      // downcast
      else if(right_lib_class) {
        ProcessError(expression, L"Invalid cast between classes: '" + left_lib_class->GetName() + L"' and '" + right_lib_class->GetName() + L"'");
      }
      else {
        ProcessError(expression, L"Invalid cast");
      }
    }
    else {
      ProcessError(expression, L"Invalid cast between class or enum: '" + FormatTypeString(left->GetName()) + L"' and '" + FormatTypeString(right->GetName()) + L"'");
    }
  }
  else {
    if(left) {
      ProcessError(expression, L"Unknown class cast type: '" + left->GetName() + L"'");
    }
    else {
      ProcessError(expression, L"Invalid class, enum or method call context\n\tEnsure all required libraries have been included");
    }
  }
}

bool ContextAnalyzer::CheckGenericEqualTypes(Type* left, Type* right, Expression* expression, bool check_only)
{
  // note, enums and consts checked elsewhere
  Class* left_klass = nullptr; LibraryClass* lib_left_klass = nullptr;
  if(!GetProgramOrLibraryClass(left, left_klass, lib_left_klass) && !current_class->GetGenericClass(left->GetName())) {
    return false;
  }

  // note, enums and consts checked elsewhere
  Class* right_klass = nullptr; LibraryClass* lib_right_klass = nullptr;
  if(!GetProgramOrLibraryClass(right, right_klass, lib_right_klass) && !current_class->GetGenericClass(right->GetName())) {
    return false;
  }

  if(left_klass == right_klass && lib_left_klass == lib_right_klass) {
    const std::vector<Type*> left_generic_types = left->GetGenerics();
    const std::vector<Type*> right_generic_types = right->GetGenerics();
    if(left_generic_types.size() != right_generic_types.size()) {
      if(check_only) {
        return false;
      }
      ProcessError(expression, L"Concrete size mismatch");
    }
    else {
      std::vector<Type*> right_concrete_types;

      for(size_t i = 0; i < right_generic_types.size(); ++i) {
        // process lhs
        Type* left_generic_type = left_generic_types[i];
        ResolveClassEnumType(left_generic_type);

        Class* left_generic_klass = nullptr; LibraryClass* lib_generic_left_klass = nullptr;
        if(GetProgramOrLibraryClass(left_generic_type, left_generic_klass, lib_generic_left_klass)) {
          if(left_generic_klass && left_generic_klass->HasGenericInterface()) {
            left_generic_type = left_generic_klass->GetGenericInterface();
          }
          else if(lib_generic_left_klass && lib_generic_left_klass->HasGenericInterface()) {
            left_generic_type = lib_generic_left_klass->GetGenericInterface();
          }
        }
        else {
          left_generic_klass = current_class->GetGenericClass(left_generic_type->GetName());
          if(left_generic_klass && left_generic_klass->HasGenericInterface()) {
            left_generic_type = left_generic_klass->GetGenericInterface();
          }
          else {
            left_generic_type = ResolveGenericType(left_generic_type, expression, left_klass, lib_left_klass);
          }
        }
        
        // process rhs
        Type* right_generic_type = right_generic_types[i];
        ResolveClassEnumType(right_generic_type);

        Class* right_generic_klass = nullptr; LibraryClass* lib_generic_right_klass = nullptr;
        if(GetProgramOrLibraryClass(right_generic_type, right_generic_klass, lib_generic_right_klass)) {
          if(right_generic_klass && right_generic_klass->HasGenericInterface()) {
            right_generic_type = right_generic_klass->GetGenericInterface();
          }
          else if(lib_generic_right_klass && lib_generic_right_klass->HasGenericInterface()) {
            right_generic_type = lib_generic_right_klass->GetGenericInterface();
          }
        }
        else {
          right_generic_klass = current_class->GetGenericClass(right_generic_type->GetName());
          if(right_generic_klass && right_generic_klass->HasGenericInterface()) {
            right_generic_type = right_generic_klass->GetGenericInterface();
          }
          else {
            right_generic_type = ResolveGenericType(right_generic_type, expression, left_klass, lib_left_klass);
          }
        }

        // const std::wstring left_type_name = left_generic_type->GetName();
        // const std::wstring right_type_name = right_generic_type->GetName();

        std::wstring left_type_name = L'<' + left_generic_type->GetName();
        if(left_generic_type->HasGenerics()) {
          std::vector<Type*> left_generics = left_generic_type->GetGenerics();
          AppendGenericNames(left_type_name, left_generics);
        }
        left_type_name += L'>';

        std::wstring right_type_name = L'<' + right_generic_type->GetName();
        if(right_generic_type->HasGenerics()) {
          std::vector<Type*> right_generics = right_generic_type->GetGenerics();
          AppendGenericNames(right_type_name, right_generics);
        }
        right_type_name += L'>';

        // alternative mapping signature
        if(left_generic_type->IsResolved() && left_type_name != right_type_name) {
          right_type_name = L'<' + right_generic_type->GetName();
          if(right_generic_type->HasGenerics()) {
            Type* right_concrete_type = ResolveGenericType(left_generic_type, expression, right_klass, lib_right_klass);
            right_concrete_types.push_back(right_concrete_type);
            std::vector<Type*> right_generic_concrete_types = right_concrete_type->GetGenerics();
            AppendGenericNames(right_type_name, right_generic_concrete_types);
          }
          right_type_name += L'>';

          if(left_type_name != right_type_name) {
            if(check_only) {
              return false;
            }
            ProcessError(expression, L"Cannot map generic/concrete class to concrete class: '" + left_type_name + L"' and '" + right_type_name + L"'");
          }
        }
      }
    }
  }

  return true;
}

/****************************
 * Analyzes a declaration
 ****************************/
void ContextAnalyzer::AnalyzeDeclaration(Declaration * declaration, Class* klass, const int depth)
{
  SymbolEntry* entry = declaration->GetEntry();
  if(entry) {
    // load entry
    if(!entry->IsLoaded()) {
      const size_t offset = entry->GetName().find(L':');
      if(offset != std::wstring::npos) {
        const std::wstring short_name = entry->GetName().substr(0, offset);
        if(HasProgramOrLibraryClass(short_name)) {
          entry->WasLoaded();
        }
      }
    }

    if(entry->GetType() && entry->GetType()->GetType() == CLASS_TYPE) {
      // resolve declaration type
      Type* type = entry->GetType();
      if(!ResolveClassEnumType(type, klass)) {
        ProcessError(entry, L"Undefined class or enum: '" + FormatTypeString(type->GetName()) + L"'\n\tIf generic ensure concrete types are properly defined.");
      }

      ValidateConcrete(type, type, declaration, depth);
    }
    else if(entry->GetType() && entry->GetType()->GetType() == FUNC_TYPE) {
      // resolve function name
      Type* type = entry->GetType();
      AnalyzeVariableFunctionParameters(type, entry, klass);
      const std::wstring encoded_name = L"m." + EncodeFunctionType(type->GetFunctionParameters(), type->GetFunctionReturn());
#ifdef _DEBUG
      GetLogger() << L"Encoded function declaration: |" << encoded_name << L"|" << std::endl;
#endif
      type->SetName(encoded_name);
    }

    Statement* statement = declaration->GetAssignment();
    if(entry->IsStatic()) {
      if(current_method) {
        ProcessError(entry, L"Static variables can only be declared at class scope");
      }

      if(statement) {
        ProcessError(entry, L"Variables cannot be initialized at class scope");
      }
    }

    if(!entry->IsLocal() && statement) {
      ProcessError(entry, L"Variables cannot be initialized at class scope");
    }

    if(statement) {
      entry->WasLoaded();
      AnalyzeStatement(statement, depth);
    }
  }
  else {
    ProcessError(declaration, L"Undefined variable entry");
  }
}

/****************************
 * Analyzes a declaration
 ****************************/
void ContextAnalyzer::AnalyzeExpressions(ExpressionList* parameters, const int depth)
{
  std::vector<Expression*> expressions = parameters->GetExpressions();
  
  for(size_t i = 0; i < expressions.size(); ++i) {
    Expression* expression = expressions[i];
    AnalyzeExpression(expression, depth);    
    if(expression->GetPreviousExpression() && expression->GetPreviousExpression()->GetExpressionType() == STR_CONCAT_EXPR) {
      parameters->SetExpression(expression->GetPreviousExpression(), i);
    }
  }
}

/********************************
 * Encodes a function definition
 ********************************/
std::wstring ContextAnalyzer::EncodeFunctionReference(ExpressionList* calling_params, const int depth)
{
  std::wstring encoded_name;
  std::vector<Expression*> expressions = calling_params->GetExpressions();
  for(size_t i = 0; i < expressions.size(); ++i) {
    if(expressions[i]->GetExpressionType() == VAR_EXPR) {
      Variable* variable = static_cast<Variable*>(expressions[i]);
      if(variable->GetName() == BOOL_CLASS_ID) {
        encoded_name += L'l';
        variable->SetEvalType(TypeFactory::Instance()->MakeType(BOOLEAN_TYPE), true);
      }
      else if(variable->GetName() == BYTE_CLASS_ID) {
        encoded_name += L'b';
        variable->SetEvalType(TypeFactory::Instance()->MakeType(BYTE_TYPE), true);
      }
      else if(variable->GetName() == INT_CLASS_ID) {
        encoded_name += L'i';
        variable->SetEvalType(TypeFactory::Instance()->MakeType(INT_TYPE), true);
      }
      else if(variable->GetName() == FLOAT_CLASS_ID) {
        encoded_name += L'f';
        variable->SetEvalType(TypeFactory::Instance()->MakeType(FLOAT_TYPE), true);
      }
      else if(variable->GetName() == CHAR_CLASS_ID) {
        encoded_name += L'c';
        variable->SetEvalType(TypeFactory::Instance()->MakeType(CHAR_TYPE), true);
      }
      else if(variable->GetName() == NIL_CLASS_ID) {
        encoded_name += L'n';
        variable->SetEvalType(TypeFactory::Instance()->MakeType(NIL_TYPE), true);
      }
      else if(variable->GetName() == VAR_CLASS_ID) {
        encoded_name += L'v';
        variable->SetEvalType(TypeFactory::Instance()->MakeType(VAR_TYPE), true);
      }
      else {
        encoded_name += L"o.";
        // search program
        const std::wstring klass_name = variable->GetName();
        Class* klass = program->GetClass(klass_name);
        if(!klass) {
          std::vector<std::wstring> uses = program->GetLibUses(current_class->GetFileName());
          for(size_t i = 0; !klass && i < uses.size(); ++i) {
            klass = program->GetClass(uses[i] + L"." + klass_name);
          }
        }
        // check class
        if(klass) {
          encoded_name += klass->GetName();
          variable->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, klass->GetName()), true);
        }
        // search libraries
        else {
          LibraryClass* lib_klass = linker->SearchClassLibraries(klass_name, program->GetLibUses(current_class->GetFileName()));
          if(lib_klass) {
            encoded_name += lib_klass->GetName();
            variable->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, lib_klass->GetName()), true);
          }
          else {
            encoded_name += variable->GetName();
            variable->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, variable->GetName()), true);
          }
        }
        // generics
        if(variable->HasConcreteTypes()) {
          const std::vector<Type*> generic_types = variable->GetConcreteTypes();
          for(size_t j = 0; j < generic_types.size(); ++j) {
            encoded_name += L"|" + generic_types[j]->GetName();
          }
        }
      }

      // dimension
      if(variable->GetIndices()) {
        std::vector<Expression*> indices = variable->GetIndices()->GetExpressions();
        variable->GetEvalType()->SetDimension((int)indices.size());
        for(size_t j = 0; j < indices.size(); ++j) {
          encoded_name += L'*';
        }
      }

      encoded_name += L',';
    }
    else if(expressions[i]->GetExpressionType() == METHOD_CALL_EXPR && static_cast<MethodCall*>(expressions[i])->GetCallType() == ENUM_CALL) {
      MethodCall* mthd_call = static_cast<MethodCall*>(expressions[i]);

      std::wstring klass_name = mthd_call->GetVariableName();
      Class* klass = nullptr; LibraryClass* lib_klass = nullptr;
      if(GetProgramOrLibraryClass(klass_name, klass, lib_klass)) {
        if(klass) {
          klass_name = klass->GetName();
        }
        else if(lib_klass) {
          klass_name = lib_klass->GetName();
        }
      }

      encoded_name += L"o.";
      encoded_name += klass_name;
      encoded_name += L'#';
      encoded_name += mthd_call->GetMethodName();
      encoded_name += L',';
    }
    else {
      // induce error condition
      encoded_name += L'#';
    }
  }

  return encoded_name;
}

/****************************
 * Encodes a function type
 ****************************/
std::wstring ContextAnalyzer::EncodeFunctionType(std::vector<Type*> func_params, Type* func_rtrn) {
  std::wstring encoded_name = L"(";
  for(size_t i = 0; i < func_params.size(); ++i) {
    // encode params
    encoded_name += EncodeType(func_params[i]);

    // encode dimension
    for(int j = 0; j < func_params[i]->GetDimension(); ++j) {
      encoded_name += L'*';
    }
    encoded_name += L',';
  }

  // encode return
  encoded_name += L")~";
  encoded_name += EncodeType(func_rtrn);
  // encode dimension
  for(int i = 0; func_rtrn && i < func_rtrn->GetDimension(); ++i) {
    encoded_name += L'*';
  }

  return encoded_name;
}

/****************************
 * Encodes a method call
 ****************************/
std::wstring ContextAnalyzer::EncodeMethodCall(ExpressionList* calling_params, const int depth)
{
  std::wstring encoded_name;
  std::vector<Expression*> expressions = calling_params->GetExpressions();
  for(size_t i = 0; i < expressions.size(); ++i) {
    Expression* expression = expressions[i];
    while(expression->GetMethodCall()) {
      AnalyzeExpressionMethodCall(expression, depth + 1);
      expression = expression->GetMethodCall();
    }

    Type* type;
    if(expression->GetCastType()) {
      type = expression->GetCastType();
    }
    else {
      type = expression->GetEvalType();
    }

    if(type) {
      // encode params
      encoded_name += EncodeType(type);

      // encode dimension
      for(int j = 0; !IsScalar(expression) && j < type->GetDimension(); ++j) {
        encoded_name += L'*';
      }
      encoded_name += L',';
    }
  }

  return encoded_name;
}

bool ContextAnalyzer::IsScalar(Expression* expression, bool check_last /*= true*/)
{
  while(check_last && expression->GetMethodCall()) {
    expression = expression->GetMethodCall();
  }

  Type* type;
  if(expression->GetCastType() && !(expression->GetEvalType() && expression->GetEvalType()->GetDimension() > 0) ) {
    type = expression->GetCastType();
  }
  else {
    type = expression->GetEvalType();
  }

  if(type && type->GetDimension() > 0) {
    ExpressionList* indices = nullptr;
    if(expression->GetExpressionType() == VAR_EXPR) {
      indices = static_cast<Variable*>(expression)->GetIndices();
    }
    else {
      return false;
    }

    return indices != nullptr;
  }

  return true;
}

bool ContextAnalyzer::IsBooleanExpression(Expression* expression)
{
  while(expression->GetMethodCall()) {
    expression = expression->GetMethodCall();
  }

  Type* eval_type = expression->GetEvalType();
  if(eval_type && eval_type->GetType() == BOOLEAN_TYPE) {
    if(expression->GetExpressionType() == VAR_EXPR) {
      return !eval_type->GetDimension() || static_cast<Variable*>(expression)->GetIndices();
    }

    return true;
  }

  return false;
}

bool ContextAnalyzer::IsEnumExpression(Expression* expression)
{
  while(expression->GetMethodCall()) {
    expression = expression->GetMethodCall();
  }
  Type* eval_type = expression->GetEvalType();
  if(eval_type) {
    if(eval_type->GetType() == CLASS_TYPE) {
      // program
      if(program->GetEnum(eval_type->GetName())) {
        return true;
      }
      // library
      if(linker->SearchEnumLibraries(eval_type->GetName(), program->GetLibUses())) {
        return true;
      }
    }
  }

  return false;
}

bool ContextAnalyzer::IsStringExpression(Expression* expression) 
{
  Type* eval_type = expression->GetEvalType();
  if(eval_type && eval_type->GetType() == CLASS_TYPE && eval_type->GetName() == L"System.String") {
    return true;
  }

  return false;
}

bool ContextAnalyzer::IsIntegerExpression(Expression* expression)
{
  while(expression->GetMethodCall()) {
    expression = expression->GetMethodCall();
  }

  Type* eval_type;
  if(expression->GetCastType()) {
    eval_type = expression->GetCastType();
  }
  else {
    eval_type = expression->GetEvalType();
  }

  if(eval_type) {
    // integer types
    if(eval_type->GetType() == INT_TYPE ||
       eval_type->GetType() == CHAR_TYPE ||
       eval_type->GetType() == BYTE_TYPE) {
      return true;
    }
    // enum types
    if(eval_type->GetType() == CLASS_TYPE) {
      // program
      if(SearchProgramEnums(eval_type->GetName())) {
        return true;
      }
      // library
      if(linker->SearchEnumLibraries(eval_type->GetName(), program->GetLibUses())) {
        return true;
      }
    }
  }

  return false;
}

bool ContextAnalyzer::DuplicateParentEntries(SymbolEntry* entry, Class* klass)
{
  if(klass->GetParent() && klass->GetParent()->GetSymbolTable() && (!entry->IsLocal() || entry->IsStatic())) {
    Class* parent = klass->GetParent();
    do {
      size_t offset = entry->GetName().find(L':');
      if(offset != std::wstring::npos) {
        ++offset;
        const std::wstring short_name = entry->GetName().substr(offset, entry->GetName().size() - offset);
        const std::wstring lookup = parent->GetName() + L':' + short_name;
        SymbolEntry * parent_entry = parent->GetSymbolTable()->GetEntry(lookup);
        if(parent_entry) {
          return true;
        }
      }
      // update
      parent = parent->GetParent();
    } while(parent);
  }

  return false;
}

bool ContextAnalyzer::DuplicateCaseItem(std::map<INT64_VALUE, StatementList*>label_statements, INT64_VALUE value)
{
  std::map<INT64_VALUE, StatementList*>::iterator result = label_statements.find(value);
  if(result != label_statements.end()) {
    return true;
  }

  return false;
}

bool ContextAnalyzer::InvalidStatic(MethodCall* method_call, Method* method)
{
  // same class, calling method static and called method not static,
  // called method not new, called method not from a variable
  if(current_method->IsStatic() && !method->IsStatic() && 
     method->GetMethodType() != NEW_PUBLIC_METHOD && method->GetMethodType() != NEW_PRIVATE_METHOD) {
    SymbolEntry* entry = GetEntry(method_call->GetVariableName());
    if(entry && (entry->IsLocal() || entry->IsStatic())) {
      return false;
    }

    Variable* variable = method_call->GetVariable();
    if(variable) {
      entry = variable->GetEntry();
      if(entry && (entry->IsLocal() || entry->IsStatic())) {
        return false;
      }
    }

    return true;
  }
  else if(!method_call->GetEntry() && !method_call->GetVariable() && !method->IsStatic() && 
          method->GetMethodType() != NEW_PUBLIC_METHOD && method->GetMethodType() != NEW_PRIVATE_METHOD) {
    Class* method_class = method->GetClass();
    Class* parent = current_method->GetClass();
    while(parent) {
      if(method_class == parent) {
        return false;
      }
      // update
      parent = parent->GetParent();
    }

    return true;
  }

  return false;
}

bool ContextAnalyzer::InvalidStatic(MethodCall* method_call, LibraryMethod* lib_method)
{
  const bool not_variable = !method_call->GetVariable() && !method_call->GetEntry();
  const bool not_static_prev = !lib_method->IsStatic() && !method_call->GetPreviousExpression();
  const bool not_new = lib_method->GetMethodType() != NEW_PUBLIC_METHOD && lib_method->GetMethodType() != NEW_PRIVATE_METHOD;
  
  return not_variable && not_static_prev && not_new;
}

SymbolEntry* ContextAnalyzer::GetEntry(std::wstring name, bool is_parent)
{
  if(current_table) {
    // check locally
    SymbolEntry* entry = current_table->GetEntry(current_method->GetName() + L':' + name);
    if(!is_parent && entry) {
      return entry;
    }
    else {
      // check class
      SymbolTable* table = symbol_table->GetSymbolTable(current_class->GetName());
      entry = table->GetEntry(current_class->GetName() + L':' + name);
      if(!is_parent && entry) {
        return entry;
      }
      else {
        // check parents
        entry = nullptr;
        const std::wstring& bundle_name = bundle->GetName();
        Class* parent;
        if(bundle_name.size() > 0) {
          parent = bundle->GetClass(bundle_name + L"." + current_class->GetParentName());
        }
        else {
          parent = bundle->GetClass(current_class->GetParentName());
        }
        while(parent && !entry) {
          SymbolTable* table = symbol_table->GetSymbolTable(parent->GetName());
          entry = table->GetEntry(parent->GetName() + L':' + name);
          if(entry) {
            return entry;
          }
          // get next parent    
          if(bundle_name.size() > 0) {
            parent = bundle->GetClass(bundle_name + L"." + parent->GetParentName());
          }
          else {
            parent = bundle->GetClass(parent->GetParentName());
          }
        }
      }
    }
  }

  return nullptr;
}

SymbolEntry* ContextAnalyzer::GetEntry(MethodCall* method_call, const std::wstring& variable_name, int depth)
{
  SymbolEntry* entry;
  if(method_call->GetVariable()) {
    Variable* variable = method_call->GetVariable();
    AnalyzeVariable(variable, depth);
    entry = variable->GetEntry();
  }
  else {
    entry = GetEntry(variable_name);
    if(entry) {
      method_call->SetEntry(entry);
    }
  }

  return entry;
}

Type* ContextAnalyzer::GetExpressionType(Expression* expression, int depth)
{
  Type* type = nullptr;

  MethodCall* mthd_call = expression->GetMethodCall();
  if(expression->GetExpressionType() == METHOD_CALL_EXPR &&
     static_cast<MethodCall*>(expression)->GetCallType() == ENUM_CALL) {
    // favor casts
    if(expression->GetCastType()) {
      type = expression->GetCastType();
    }
    else {
      type = expression->GetEvalType();
    }
  }
  else if(mthd_call) {
    do {
      AnalyzeExpressionMethodCall(mthd_call, depth + 1);

      // favor casts
      if(mthd_call->GetPreviousExpression() && mthd_call->GetPreviousExpression()->GetCastType() && mthd_call->GetEvalType() && !SearchProgramEnums(mthd_call->GetEvalType()->GetName())) {
        type = mthd_call->GetPreviousExpression()->GetCastType();
      }
      else if(mthd_call->GetCastType()) {
        type = mthd_call->GetCastType();
      }
      else {
        type = mthd_call->GetEvalType();
      }

      mthd_call = mthd_call->GetMethodCall();
    } 
    while(mthd_call);
  }
  else {
    // favor casts
    if(expression->GetCastType()) {
      type = expression->GetCastType();
    }
    else {
      type = expression->GetEvalType();
    }
  }

  return type;
}

bool ContextAnalyzer::ValidDownCast(const std::wstring& cls_name, Class* class_tmp, LibraryClass* lib_class_tmp)
{
  if(cls_name == L"System.Base") {
    return true;
  }

  while(class_tmp || lib_class_tmp) {
    // get cast name
    std::wstring cast_name;
    std::vector<std::wstring> interface_names;
    if(class_tmp) {
      cast_name = class_tmp->GetName();
      interface_names = class_tmp->GetInterfaceNames();
    }
    else if(lib_class_tmp) {
      cast_name = lib_class_tmp->GetName();
      interface_names = lib_class_tmp->GetInterfaceNames();
    }

    // parent cast
    if(cls_name == cast_name) {
      return true;
    }

    // interface cast
    for(size_t i = 0; i < interface_names.size(); ++i) {
      Class* klass = SearchProgramClasses(interface_names[i]);
      if(klass && klass->GetName() == cls_name) {
        return true;
      }
      else {
        LibraryClass* lib_klass = linker->SearchClassLibraries(interface_names[i], program->GetLibUses());
        if(lib_klass && lib_klass->GetName() == cls_name) {
          return true;
        }
      }
    }

    // update
    if(class_tmp) {
      if(class_tmp->GetParent()) {
        class_tmp = class_tmp->GetParent();
        lib_class_tmp = nullptr;
      }
      else {
        lib_class_tmp = class_tmp->GetLibraryParent();
        class_tmp = nullptr;
      }

    }
    // library parent
    else {
      lib_class_tmp = linker->SearchClassLibraries(lib_class_tmp->GetParentName(), program->GetLibUses());
      class_tmp = nullptr;
    }
  }

  return false;
}

bool ContextAnalyzer::ValidUpCast(const std::wstring& to, Class* from_klass)
{
  if(from_klass->GetName() == L"System.Base") {
    return true;
  }

  // parent cast
  if(to == from_klass->GetName()) {
    return true;
  }

  // interface cast
  std::vector<std::wstring> interface_names = from_klass->GetInterfaceNames();
  for(size_t i = 0; i < interface_names.size(); ++i) {
    Class* klass = SearchProgramClasses(interface_names[i]);
    if(klass && klass->GetName() == to) {
      return true;
    }
    else {
      LibraryClass* lib_klass = linker->SearchClassLibraries(interface_names[i], program->GetLibUses());
      if(lib_klass && lib_klass->GetName() == to) {
        return true;
      }
    }
  }

  // updates
  std::vector<Class*> children = from_klass->GetChildren();
  for(size_t i = 0; i < children.size(); ++i) {
    if(ValidUpCast(to, children[i])) {
      return true;
    }
  }

  return false;
}

bool ContextAnalyzer::ValidUpCast(const std::wstring& to, LibraryClass* from_klass)
{
  if(from_klass->GetName() == L"System.Base") {
    return true;
  }

  // parent cast
  if(to == from_klass->GetName()) {
    return true;
  }

  // interface cast
  std::vector<std::wstring> interface_names = from_klass->GetInterfaceNames();
  for(size_t i = 0; i < interface_names.size(); ++i) {
    Class* klass = SearchProgramClasses(interface_names[i]);
    if(klass && klass->GetName() == to) {
      return true;
    }
    else {
      LibraryClass* lib_klass = linker->SearchClassLibraries(interface_names[i], program->GetLibUses());
      if(lib_klass && lib_klass->GetName() == to) {
        return true;
      }
    }
  }

  // program updates
  std::vector<LibraryClass*> children = from_klass->GetLibraryChildren();
  for(size_t i = 0; i < children.size(); ++i) {
    if(ValidUpCast(to, children[i])) {
      return true;
    }
  }

  // library updates
  std::vector<ParseNode*> lib_children = from_klass->GetChildren();
  for(size_t i = 0; i < lib_children.size(); ++i) {
    if(ValidUpCast(to, static_cast<Class*>(lib_children[i]))) {
      return true;
    }
  }

  return false;
}

bool ContextAnalyzer::GetProgramOrLibraryClass(const std::wstring &cls_name, Class*& klass, LibraryClass*& lib_klass)
{
  klass = SearchProgramClasses(cls_name);
  if(klass) {
    return true;
  }

  lib_klass = linker->SearchClassLibraries(cls_name, program->GetLibUses(current_class->GetFileName()));
  if(lib_klass) {
    return true;
  }

  return false;
}

bool ContextAnalyzer::GetProgramOrLibraryClass(Type* type, Class*& klass, LibraryClass*& lib_klass)
{
  Class* cls_ptr = static_cast<Class*>(type->GetClassPtr());
  if(cls_ptr) {
    klass = cls_ptr;
    return true;
  }

  LibraryClass* lib_cls_ptr = static_cast<LibraryClass*>(type->GetLibraryClassPtr());
  if(lib_cls_ptr) {
    lib_klass = lib_cls_ptr;
    return true;
  }

  std::wstring type_name = type->GetName();
  if(type_name.empty()) {
    switch(type->GetType()) {
    case 	BOOLEAN_TYPE:
      type_name = L"System.$Bool";
      break;

    case 	BYTE_TYPE:
      type_name = L"System.$Byte";
      break;

    case CHAR_TYPE:
      type_name = L"System.$Char";
      break;

    case INT_TYPE:
      type_name = L"System.$Int";
      break;

    case FLOAT_TYPE:
      type_name = L"System.$Float";
      break;

    default:
      break;
    }

    type->SetName(type_name);
  }

  if(GetProgramOrLibraryClass(type_name, klass, lib_klass)) {
    if(klass) {
      type->SetName(klass->GetName());
      type->SetClassPtr(klass);
      type->SetResolved(true);
    }
    else {
      type->SetName(lib_klass->GetName());
      type->SetLibraryClassPtr(lib_klass);
      type->SetResolved(true);
    }

    return true;
  }

  return false;
}

std::wstring ContextAnalyzer::GetProgramOrLibraryClassName(const std::wstring& n)
{
  Class* klass = nullptr; LibraryClass* lib_klass = nullptr;
  if(GetProgramOrLibraryClass(n, klass, lib_klass)) {
    if(klass) {
      return klass->GetName();
    }
    else {
      return lib_klass->GetName();
    }
  }

  return n;
}

const std::wstring ContextAnalyzer::EncodeType(Type* type)
{
  std::wstring encoded_name;

  if(type) {
    switch(type->GetType()) {
    case BOOLEAN_TYPE:
      encoded_name += 'l';
      break;

    case BYTE_TYPE:
      encoded_name += 'b';
      break;

    case INT_TYPE:
      encoded_name += 'i';
      break;

    case FLOAT_TYPE:
      encoded_name += 'f';
      break;

    case CHAR_TYPE:
      encoded_name += 'c';
      break;

    case NIL_TYPE:
      encoded_name += 'n';
      break;

    case VAR_TYPE:
      encoded_name += 'v';
      break;
        
    case ALIAS_TYPE:
      break;

    case CLASS_TYPE: {
      encoded_name += L"o.";

      // search program and libraries
      Class* klass = nullptr; LibraryClass* lib_klass = nullptr;
      if(GetProgramOrLibraryClass(type, klass, lib_klass)) {
        if(klass) {
          encoded_name += klass->GetName();
        }
        else {
          encoded_name += lib_klass->GetName();
        }
      }
      else {
        encoded_name += type->GetName();
      }
    }
      break;
      
    case FUNC_TYPE:
      if(type->GetName().size() == 0) {
        type->SetName(EncodeFunctionType(type->GetFunctionParameters(), type->GetFunctionReturn()));
      }
      encoded_name += type->GetName();
      break;
    }
  }
  
  return encoded_name;
}

bool ContextAnalyzer::ResolveClassEnumType(Type* type, Class* context_klass)
{
   if(type->IsResolved()) {
      auto is_resloved = true;

      auto generic_types = type->GetGenerics();
      for(size_t i = 0; is_resloved && i < generic_types.size(); ++i) {
         auto generic_type = generic_types[i];
         if(!generic_type->IsResolved()) {
            is_resloved = false;
         }
      }

      if(is_resloved) {
         return true;
      }
   }

   Class* klass = SearchProgramClasses(type->GetName());
   if(klass) {
      // concreate generics
      auto generic_types = type->GetGenerics();
      for(auto& generic_type : generic_types) {
         if(!ResolveClassEnumType(generic_type, context_klass)) {
            return false;
         }
      }

      klass->SetCalled(true);
      type->SetName(klass->GetName());
      type->SetResolved(true);
      return true;
   }

   LibraryClass* lib_klass = linker->SearchClassLibraries(type->GetName(), program->GetLibUses());
   if(lib_klass) {
      // concreate generics
      auto generic_types = type->GetGenerics();
      for(auto& generic_type : generic_types) {
         if(!ResolveClassEnumType(generic_type, context_klass)) {
            return false;
         }
      }

      lib_klass->SetCalled(true);
      type->SetName(lib_klass->GetName());
      type->SetResolved(true);
      return true;
   }

   // class defined generics
   if(context_klass->HasGenerics()) {
      klass = context_klass->GetGenericClass(type->GetName());
      if(klass) {
         if(klass->HasGenericInterface()) {
            Type* inf_type = klass->GetGenericInterface();
            if(ResolveClassEnumType(inf_type)) {
               type->SetName(inf_type->GetName());
               type->SetResolved(true);
               return true;
            }
         }
         else {
            type->SetName(type->GetName());
            type->SetResolved(true);
            return true;
         }
      }
   }

   Enum* eenum = SearchProgramEnums(type->GetName());
   if(eenum) {
      type->SetName(type->GetName());
      type->SetResolved(true);
      return true;
   }
   else {
      eenum = SearchProgramEnums(context_klass->GetName() + L"#" + type->GetName());
      if(eenum) {
         type->SetName(context_klass->GetName() + L"#" + type->GetName());
         type->SetResolved(true);
         return true;
      }
   }

   LibraryEnum* lib_eenum = linker->SearchEnumLibraries(type->GetName(), program->GetLibUses());
   if(lib_eenum) {
      type->SetName(lib_eenum->GetName());
      type->SetResolved(true);
      return true;
   }
   else {
      lib_eenum = linker->SearchEnumLibraries(type->GetName(), program->GetLibUses());
      if(lib_eenum) {
         type->SetName(lib_eenum->GetName());
         type->SetResolved(true);
         return true;
      }
   }

   return false;
}

std::wstring ContextAnalyzer::FormatTypeString(const std::wstring name)
{
  const size_t index = name.find(L'#');
  if(index != std::wstring::npos) {
    const std::wstring left = name.substr(0, index);
    const std::wstring right = name.substr(index + 1);
    if(left == right) {
      return left;
    }
    else {
      return ReplaceSubstring(name, L"#", L"->");
    }
  }

  return name;
}

bool ContextAnalyzer::IsClassEnumParameterMatch(Type* calling_type, Type* method_type)
{
  const std::wstring& from_klass_name = calling_type->GetName();

  LibraryClass* from_lib_klass = nullptr;
  Class* from_klass = SearchProgramClasses(from_klass_name);
  if(!from_klass && current_class->HasGenerics()) {
    from_klass = current_class->GetGenericClass(from_klass_name);
  }

  if(!from_klass) {
    from_lib_klass = linker->SearchClassLibraries(from_klass_name, program->GetLibUses());
  }

  // resolve to class name
  std::wstring to_klass_name;
  Class* to_klass = SearchProgramClasses(method_type->GetName());
  if(!to_klass && current_class->HasGenerics()) {
    to_klass = current_class->GetGenericClass(method_type->GetName());
    if(to_klass) {
      to_klass_name = to_klass->GetName();
    }
  }

  if(!to_klass) {
    LibraryClass* to_lib_klass = linker->SearchClassLibraries(method_type->GetName(), program->GetLibUses());
    if(to_lib_klass) {
      to_klass_name = to_lib_klass->GetName();
    }
  }

  // check enum types
  if(!from_klass && !from_lib_klass) {
    Enum* from_enum = SearchProgramEnums(from_klass_name);
    LibraryEnum* from_lib_enum = linker->SearchEnumLibraries(from_klass_name, program->GetLibUses());

    std::wstring to_enum_name;
    Enum* to_enum = SearchProgramEnums(method_type->GetName());
    if(to_enum) {
      to_enum_name = to_enum->GetName();
    }
    else {
      LibraryEnum* to_lib_enum = linker->SearchEnumLibraries(method_type->GetName(), program->GetLibUses());
      if(to_lib_enum) {
        to_enum_name = to_lib_enum->GetName();
      }
    }

    // look for exact class match
    if(from_enum && from_enum->GetName() == to_enum_name) {
      return true;
    }

    // look for exact class library match
    if(from_lib_enum && from_lib_enum->GetName() == to_enum_name) {
      return true;
    }
  }
  else {
    // look for exact class match
    if(from_klass && from_klass->GetName() == to_klass_name) {
      return true;
    }

    // look for exact class library match
    if(from_lib_klass && from_lib_klass->GetName() == to_klass_name) {
      return true;
    }
  }

  return false;
}

void ContextAnalyzer::ResolveEnumCall(LibraryEnum* lib_eenum, const std::wstring& item_name, MethodCall* method_call)
{
  // item_name = method_call->GetMethodCall()->GetVariableName();
  LibraryEnumItem* lib_item = lib_eenum->GetItem(item_name);
  if(lib_item) {
    if(method_call->GetMethodCall()) {
      method_call->GetMethodCall()->SetLibraryEnumItem(lib_item, lib_eenum->GetName());
      method_call->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, lib_eenum->GetName()), false);
      method_call->GetMethodCall()->SetEvalType(method_call->GetEvalType(), false);
    }
    else {
      method_call->SetLibraryEnumItem(lib_item, lib_eenum->GetName());
      method_call->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, lib_eenum->GetName()), false);
    }
  }
  else {
    ProcessError(static_cast<Expression*>(method_call), L"Undefined enum item: '" + item_name + L"'");
  }
}

Enum* ContextAnalyzer::GetExpressionEnum(Type* type, Expression* expression, int depth)
{
  Enum* type_enum = nullptr;

  if(type) {
    type_enum = SearchProgramEnums(type->GetName());

    if(!type_enum) {
      type_enum = SearchProgramEnums(current_class->GetName() + L"#" + type->GetName());
      if(!type_enum && expression->GetCastType() && expression->GetMethodCall()) {
        while(expression->GetMethodCall()) {
          AnalyzeExpressionMethodCall(expression, depth + 1);
          expression = expression->GetMethodCall();
        }

        if(expression->GetEvalType()) {
          ResolveClassEnumType(expression->GetEvalType());
          type_enum = SearchProgramEnums(expression->GetEvalType()->GetName());
        }
      }
    }
  }

  return type_enum;
}

StringConcat* ContextAnalyzer::AnalyzeStringConcat(Expression* expression, int depth) {
  if(expression->GetExpressionType() == ADD_EXPR) {
    std::list<Expression*> concat_exprs;

    concat_exprs.push_front(static_cast<CalculatedExpression*>(expression)->GetRight());
    Expression* calc_left_expr = static_cast<CalculatedExpression*>(expression)->GetLeft();

    while(calc_left_expr && calc_left_expr->GetExpressionType() == ADD_EXPR) {
      concat_exprs.push_front(static_cast<CalculatedExpression*>(calc_left_expr)->GetRight());
      calc_left_expr = static_cast<CalculatedExpression*>(calc_left_expr)->GetLeft();
    }

    if(calc_left_expr) {
      AnalyzeExpression(calc_left_expr, depth + 1);
      Type* calc_left_type = GetExpressionType(calc_left_expr, depth + 1);
      if(calc_left_type && calc_left_type->GetName() == L"System.String") {
        concat_exprs.push_front(calc_left_expr);

        std::unordered_map<Expression*, Method*> methods_to_string;
        std::unordered_map<Expression*, LibraryMethod*> lib_methods_to_string;

        for(std::list<Expression*>::iterator iter = concat_exprs.begin(); iter != concat_exprs.end(); ++iter) {
          Expression* concat_expr = *iter;
          AnalyzeExpression(concat_expr, depth + 1);

          if(concat_expr->GetEvalType()) {
            if(concat_expr->GetEvalType()->GetType() == CLASS_TYPE && concat_expr->GetEvalType()->GetName() != L"System.String" && concat_expr->GetEvalType()->GetName() != L"String") {
              const std::wstring cls_name = concat_expr->GetEvalType()->GetName();
              Class* klass = SearchProgramClasses(cls_name);
              if(klass) {
                Method* method = klass->GetMethod(cls_name + L":ToString:");
                if(method && method->GetMethodType() != PRIVATE_METHOD) {
                  methods_to_string[concat_expr] = method;
                }
                else {
                  ProcessError(concat_expr, L"Class/enum variable does not have a public 'ToString' method");
                }
              }
              else {
                LibraryClass* lib_klass = linker->SearchClassLibraries(cls_name, program->GetLibUses());
                if(lib_klass) {
                  LibraryMethod* lib_method = lib_klass->GetMethod(cls_name + L":ToString:");
                  if(lib_method && lib_method->GetMethodType() != PRIVATE_METHOD) {
                    lib_methods_to_string[concat_expr] = lib_method;
                  }
                  else {
                    ProcessError(concat_expr, L"Class/enum variable does not have a public 'ToString' method");
                  }
                }
                else {
                  ProcessError(concat_expr, L"Class/enum variable does not have a 'ToString' method");
                }
              }
            }
            else if(concat_expr->GetEvalType()->GetType() == FUNC_TYPE) {
              ProcessError(concat_expr, L"Invalid function variable type");
            }
          }
        }

        // create temporary variable for concat of strings and variables
        StringConcat* str_concat = TreeFactory::Instance()->MakeStringConcat(concat_exprs, methods_to_string, lib_methods_to_string);
        Type * type = TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.String");
        const std::wstring scope_name = current_method->GetName() + L":#_add_concat_#";
        
        SymbolEntry* entry = current_table->GetEntry(scope_name);
        if(!entry) {
          entry = TreeFactory::Instance()->MakeSymbolEntry(scope_name, type, false, true);
          current_table->AddEntry(entry, true);
        }
        str_concat->SetConcat(entry);
        
        return str_concat;
      }
    }
  }

  return nullptr;
}

void ContextAnalyzer::AnalyzeCharacterStringVariable(SymbolEntry* entry, CharacterString* char_str, int depth)
{
#ifdef _DEBUG
  Debug(L"variable=|" + entry->GetName() + L"|", char_str->GetLineNumber(), depth + 1);
#endif
  if(InvalidStatic(entry)) {
    ProcessError(char_str, L"Cannot reference an instance variable from this context");
  }
  else {
    if(!entry->GetType() || entry->GetType()->GetDimension() > 0) {
      ProcessError(char_str, L"Invalid function variable type or dimension size");
    }
    else if(entry->GetType()->GetType() == CLASS_TYPE && entry->GetType()->GetName() != L"System.String" && entry->GetType()->GetName() != L"String") {
      const std::wstring cls_name = entry->GetType()->GetName();
      Class* klass = SearchProgramClasses(cls_name);
      if(klass) {
        Method* method = klass->GetMethod(cls_name + L":ToString:");
        if(method && method->GetMethodType() != PRIVATE_METHOD) {
          char_str->AddSegment(entry, method);
          entry->WasLoaded();
        }
        else {
          ProcessError(char_str, L"Class/enum variable does not have a public 'ToString' method");
        }
      }
      else {
        LibraryClass* lib_klass = linker->SearchClassLibraries(cls_name, program->GetLibUses());
        if(lib_klass) {
          LibraryMethod* lib_method = lib_klass->GetMethod(cls_name + L":ToString:");
          if(lib_method && lib_method->GetMethodType() != PRIVATE_METHOD) {
            char_str->AddSegment(entry, lib_method);
            entry->WasLoaded();
          }
          else {
            ProcessError(char_str, L"Class/enum variable does not have a public 'ToString' method");
          }
        }
        else {
          ProcessError(char_str, L"Class/enum variable does not have a 'ToString' method");
        }
      }
    }
    else if(entry->GetType()->GetType() == FUNC_TYPE) {
      ProcessError(char_str, L"Invalid function variable type");
    }
    else {
      char_str->AddSegment(entry);
    }
  }
}

void ContextAnalyzer::AnalyzeVariableCast(Type* to_type, Expression* expression)
{
  if(to_type && to_type->GetType() == CLASS_TYPE && expression->GetCastType() && to_type->GetDimension() < 1 &&
     to_type->GetName() != L"System.Base" && to_type->GetName() != L"Base") {
    const std::wstring to_class_name = to_type->GetName();
    if(SearchProgramEnums(to_class_name) ||
       linker->SearchEnumLibraries(to_class_name, program->GetLibUses(current_class->GetFileName()))) {
      return;
    }

    Class* to_class = SearchProgramClasses(to_class_name);
    if(to_class) {
      expression->SetToClass(to_class);
    }
    else {
      LibraryClass* to_lib_class = linker->SearchClassLibraries(to_class_name, program->GetLibUses());
      if(to_lib_class) {
        expression->SetToLibraryClass(to_lib_class);
      }
      else {
        ProcessError(expression, L"Undefined class: '" + to_class_name + L"'");
      }
    }
  }
}

void ContextAnalyzer::AnalyzeVariableFunctionParameters(Type* func_type, ParseNode* node, Class* klass)
{
  const std::vector<Type*> func_params = func_type->GetFunctionParameters();
  Type* rtrn_type = func_type->GetFunctionReturn();

  // might be a resolved string from a class library
  if(func_params.size() > 0 && rtrn_type) {
    for(size_t i = 0; i < func_params.size(); ++i) {
      Type* type = func_params[i];
      if(type->GetType() == CLASS_TYPE && !ResolveClassEnumType(type, klass)) {
        ProcessError(node, L"Undefined class or enum: '" + type->GetName() + L"'");
      }
    }

    if(rtrn_type && rtrn_type->GetType() == CLASS_TYPE && !ResolveClassEnumType(rtrn_type, klass)) {
      ProcessError(node, L"Undefined class or enum: '" + rtrn_type->GetName() + L"'");
    }
  }
}

void ContextAnalyzer::AddMethodParameter(MethodCall* method_call, SymbolEntry* entry, int depth)
{
  const std::wstring& entry_name = entry->GetName();
  const size_t start = entry_name.rfind(':');
  if(start != std::wstring::npos) {
    const std::wstring& param_name = entry_name.substr(start + 1);
    Variable * variable = TreeFactory::Instance()->MakeVariable(static_cast<Expression*>(method_call)->GetFileName(),
                                                                static_cast<Expression*>(method_call)->GetLineNumber(),
                                                                static_cast<Expression*>(method_call)->GetLinePosition(),
                                                                param_name);
    method_call->SetVariable(variable);
    AnalyzeVariable(variable, entry, depth + 1);
  }
}

bool ContextAnalyzer::ClassEquals(const std::wstring &left_name, Class* right_klass, LibraryClass* right_lib_klass)
{
  Class* left_klass = nullptr; LibraryClass* left_lib_klass = nullptr;
  if(GetProgramOrLibraryClass(left_name, left_klass, left_lib_klass)) {
    if(left_klass && right_klass) {
      return left_klass->GetName() == right_klass->GetName();
    }
    else if(left_lib_klass && right_lib_klass) {
      return left_lib_klass->GetName() == right_lib_klass->GetName();
    }
  }

  if(right_klass) {
    left_klass = current_class->GetGenericClass(left_name);
    if(left_klass) {
      return left_klass->GetName() == right_klass->GetName();
    }
  }

  return false;
}

std::wstring ContextAnalyzer::ReplaceSubstring(std::wstring s, const std::wstring& f, const std::wstring& r)
{
  const size_t index = s.find(f);
  if(index != std::string::npos) {
    s.replace(index, f.size(), r);
  }

  return s;
}

void ContextAnalyzer::ReplaceAllSubstrings(std::wstring& str, const std::wstring& from, const std::wstring& to)
{
  size_t start_pos = 0;
  while((start_pos = str.find(from, start_pos)) != std::wstring::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }
}

Type* ContextAnalyzer::ResolveGenericType(Type* candidate_type, MethodCall* method_call, Class* klass, LibraryClass* lib_klass, bool is_rtrn)
{
  const bool has_generics = (klass && klass->HasGenerics()) || (lib_klass && lib_klass->HasGenerics());
  if(has_generics) {
    if(candidate_type->GetType() == FUNC_TYPE) {
      if(klass) {
        Type* concrete_rtrn = ResolveGenericType(candidate_type->GetFunctionReturn(), method_call, klass, lib_klass, false);
        std::vector<Type*> concrete_params;
        const std::vector<Type*> type_params = candidate_type->GetFunctionParameters();
        for(size_t i = 0; i < type_params.size(); ++i) {
          concrete_params.push_back(ResolveGenericType(type_params[i], method_call, klass, lib_klass, false));
        }

        return TypeFactory::Instance()->MakeType(concrete_params, concrete_rtrn);
      }
      else {
        ResolveClassEnumType(candidate_type);
        std::wstring func_name = candidate_type->GetName();

        const std::vector<LibraryClass*> generic_classes = lib_klass->GetGenericClasses();
        for(size_t i = 0; i < generic_classes.size(); ++i) {
          const std::wstring find_name = generic_classes[i]->GetName();
          Type* to_type = ResolveGenericType(TypeFactory::Instance()->MakeType(CLASS_TYPE, find_name), method_call, klass, lib_klass, false);
          const std::wstring from_name = L"o." + generic_classes[i]->GetName();
          std::wstring to_name = L"o." + to_type->GetName();
          // generics
          if(to_type->HasGenerics()) {
            const std::vector<Type*> generic_types = to_type->GetGenerics();
            for(size_t j = 0; j < generic_types.size(); ++j) {
              to_name += L"|" + generic_types[j]->GetName();
            }
          }
          ReplaceAllSubstrings(func_name, from_name, to_name);
        }

        return TypeParser::ParseType(func_name);
      }
    }
    else {
      // find concrete index
      int concrete_index = -1;
      ResolveClassEnumType(candidate_type);
      const std::wstring generic_name = candidate_type->GetName();
      if(klass) {
        concrete_index = klass->GenericIndex(generic_name);
      }
      else if(lib_klass) {
        concrete_index = lib_klass->GenericIndex(generic_name);
      }

      if(is_rtrn) {
        Class* klass_generic = nullptr; LibraryClass* lib_klass_generic = nullptr;
        if(GetProgramOrLibraryClass(candidate_type, klass_generic, lib_klass_generic)) {
          const std::vector<Type*> candidate_types = GetConcreteTypes(method_call);
          if(method_call->GetEntry()) {
            std::vector<Type*> from_concrete_types = method_call->GetEntry()->GetType()->GetGenerics();
            for(size_t i = 0; i < candidate_types.size(); ++i) {
              if(klass && method_call->GetEvalType()) {
                const std::vector<Type*> map_types = method_call->GetEvalType()->GetGenerics();
                if(i < map_types.size()) {
                  ResolveClassEnumType(map_types[i]);
                }
                else {
                  ProcessError(static_cast<Expression*>(method_call), L"Generic to concrete size mismatch");
                }
              }
              else if(lib_klass && method_call->GetEvalType()) {
                const std::vector<Type*> map_types = method_call->GetEvalType()->GetGenerics();
                if(i < map_types.size()) {
                  Type* map_type = map_types[i];
                  ResolveClassEnumType(map_type);

                  const int map_type_index = lib_klass->GenericIndex(map_type->GetName());
                  if(map_type_index > -1 && map_type_index < (int)from_concrete_types.size()) {
                    Type* candidate_type = candidate_types[i];
                    ResolveClassEnumType(candidate_type);

                    Type* concrete_type = from_concrete_types[map_type_index];
                    ResolveClassEnumType(concrete_type);

                    ValidateConcretes(candidate_type, concrete_type, method_call);
                  }
                  else {
                    std::vector<Type*> to_concrete_types = method_call->GetConcreteTypes();
                    if(from_concrete_types.size() != to_concrete_types.size() && to_concrete_types.size() == 1) {
                      std::vector<Type*> mthd_types;
                      if(method_call->GetMethod()) {
                        mthd_types = method_call->GetMethod()->GetReturn()->GetGenerics();
                      }
                      else if(method_call->GetLibraryMethod()) {
                        mthd_types = method_call->GetLibraryMethod()->GetReturn()->GetGenerics();
                      }

                      for(size_t j = 0; j < mthd_types.size(); ++j) {
                        Type* mthd_type = mthd_types[j];
                        ResolveClassEnumType(mthd_type);

                        Type* to_concrete_type = to_concrete_types[j];
                        ResolveClassEnumType(to_concrete_type);

                        ValidateConcretes(mthd_type, to_concrete_type, method_call);
                        to_concrete_types = to_concrete_type->GetGenerics();
                      }
                    }

                    if(from_concrete_types.size() == to_concrete_types.size()) {
                      for(size_t j = 0; j < from_concrete_types.size(); ++j) {
                        Type* from_concrete_type = from_concrete_types[j];
                        ResolveClassEnumType(from_concrete_type);

                        Type* to_concrete_type = to_concrete_types[j];
                        ResolveClassEnumType(to_concrete_type);

                        ValidateConcretes(from_concrete_type, to_concrete_type, method_call);
                      }
                    }
                  }
                }
                else {
                  ProcessError(static_cast<Expression*>(method_call), L"Generic to concrete size mismatch");
                }
              }
            }
          }

          if(klass_generic && klass_generic->HasGenerics()) {
            ValidateGenericConcreteMapping(candidate_types, klass_generic, static_cast<Expression*>(method_call));
            if(method_call->GetEvalType()) {
              method_call->GetEvalType()->SetGenerics(candidate_types);
            }
          }
          else if(lib_klass_generic && lib_klass_generic->HasGenerics()) {
            ValidateGenericConcreteMapping(candidate_types, lib_klass_generic, static_cast<Expression*>(method_call));
            if(method_call->GetEvalType()) {
              method_call->GetEvalType()->SetGenerics(candidate_types);
            }
          }
        }
      }

      // find concrete type
      if(concrete_index > -1) {
        std::vector<Type*> concrete_types;
        // get types from declaration
        if(method_call->GetEntry()) {
          concrete_types = method_call->GetEntry()->GetType()->GetGenerics();
        }
        else if(method_call->GetVariable() && method_call->GetVariable()->GetEntry()) {
          concrete_types = method_call->GetVariable()->GetEntry()->GetType()->GetGenerics();
        }
        else if(method_call->GetCallType() == NEW_INST_CALL) {
          concrete_types = GetConcreteTypes(method_call);
        }
        else if(!method_call->GetConcreteTypes().empty()) {
          concrete_types = method_call->GetConcreteTypes();
        }
        else if(method_call->GetEvalType()) {
          concrete_types = method_call->GetEvalType()->GetGenerics();
          method_call->SetConcreteTypes(concrete_types);
        }

        // get concrete type
        if(concrete_index < (int)concrete_types.size()) {
          Type* concrete_type = TypeFactory::Instance()->MakeType(concrete_types[concrete_index]);
          concrete_type->SetDimension(candidate_type->GetDimension());
          ResolveClassEnumType(concrete_type);
          return concrete_type;
        }
      }
    }
  }

  if(!candidate_type->IsResolved()) {
    ResolveClassEnumType(candidate_type);
  }

  return candidate_type;
}

Type* ContextAnalyzer::ResolveGenericType(Type* type, Expression* expression, Class* left_klass, LibraryClass* lib_left_klass)
{
  int concrete_index = -1;
  const std::wstring left_type_name = type->GetName();

  if(program->GetEnum(left_type_name) || linker->SearchEnumLibraries(left_type_name, program->GetLibUses())) {
    ProcessError(expression, L"Generic must be a class type");
  }

  if(left_klass) {
    concrete_index = left_klass->GenericIndex(left_type_name);
  }
  else if(lib_left_klass) {
    concrete_index = lib_left_klass->GenericIndex(left_type_name);
  }

  if(concrete_index > -1) {
    std::vector<Type*> concrete_types;

    if(expression->GetExpressionType() == VAR_EXPR) {
      Variable* variable = static_cast<Variable*>(expression);
      if(variable->GetEntry()) {
        concrete_types = variable->GetEntry()->GetType()->GetGenerics();
      }
    }
    else if(expression->GetExpressionType() == METHOD_CALL_EXPR) {
      concrete_types = GetConcreteTypes(static_cast<MethodCall*>(expression));
    }

    if(concrete_index < (int)concrete_types.size()) {
      return concrete_types[concrete_index];
    }
  }

  return type;
}

Class* ContextAnalyzer::SearchProgramClasses(const std::wstring& klass_name)
{
  Class* klass = program->GetClass(klass_name);
  if(!klass) {
    klass = program->GetClass(bundle->GetName() + L"." + klass_name);
    if(!klass) {
      std::vector<std::wstring> uses = program->GetLibUses();
      for(size_t i = 0; !klass && i < uses.size(); ++i) {
        klass = program->GetClass(uses[i] + L"." + klass_name);
      }
    }
  }

  return klass;
}

Enum* ContextAnalyzer::SearchProgramEnums(const std::wstring& eenum_name)
{
  Enum* eenum = program->GetEnum(eenum_name);
  if(!eenum) {
    eenum = program->GetEnum(bundle->GetName() + L"." + eenum_name);
    if(!eenum) {
      std::vector<std::wstring> uses = program->GetLibUses();
      for(size_t i = 0; !eenum && i < uses.size(); ++i) {
        eenum = program->GetEnum(uses[i] + L"." + eenum_name);
        if(!eenum) {
          eenum = program->GetEnum(uses[i] + eenum_name);
        }
      }
    }
  }

  return eenum;
}

/****************************
 * Support for inferred method
 * signatures
 ****************************/
LibraryMethod* LibraryMethodCallSelector::GetSelection()
{
  // no match
  if(valid_matches.size() == 0) {
    return nullptr;
  }
  // single match
  else if(valid_matches.size() == 1) {
    method_call->GetCallingParameters()->SetExpressions(valid_matches[0]->GetCallingParameters());
    return valid_matches[0]->GetLibraryMethod();
  }

  int match_index = -1;
  int high_score = 0;
  for(size_t i = 0; i < matches.size(); ++i) {
    // calculate match score
    int match_score = 0;
    bool exact_match = true;
    std::vector<int> parm_matches = matches[i]->GetParameterMatches();
    for(size_t j = 0; exact_match && j < parm_matches.size(); ++j) {
      if(parm_matches[j] == 0) {
        match_score++;
      }
      else {
        exact_match = false;
      }
    }
    // save the index of the best match
    if(match_score > high_score) {
      match_index = (int)i;
      high_score = match_score;
    }
  }

  if(match_index == -1) {
    return nullptr;
  }

  method_call->GetCallingParameters()->SetExpressions(matches[match_index]->GetCallingParameters());
  return matches[match_index]->GetLibraryMethod();
}

Method* MethodCallSelector::GetSelection()
{
  // no match
  if(valid_matches.size() == 0) {
    return nullptr;
  }
  // single match
  else if(valid_matches.size() == 1) {
    method_call->GetCallingParameters()->SetExpressions(valid_matches[0]->GetCallingParameters());
    return valid_matches[0]->GetMethod();
  }

  int match_index = -1;
  int high_score = 0;
  for(size_t i = 0; i < matches.size(); ++i) {
    // calculate match score
    int match_score = 0;
    bool exact_match = true;
    std::vector<int> parm_matches = matches[i]->GetParameterMatches();
    for(size_t j = 0; exact_match && j < parm_matches.size(); ++j) {
      if(parm_matches[j] == 0) {
        match_score++;
      }
      else {
        exact_match = false;
      }
    }
    // save the index of the best match
    if(match_score > high_score) {
      match_index = (int)i;
      high_score = match_score;
    }
  }

  if(match_index == -1) {
    return nullptr;
  }

  method_call->GetCallingParameters()->SetExpressions(matches[match_index]->GetCallingParameters());
  return matches[match_index]->GetMethod();
}

//
// START: diagnostics operations
//
#ifdef _DIAG_LIB
bool ContextAnalyzer::GetCompletion(ParsedProgram* program, Method* method, const std::wstring var_str, const std::wstring mthd_str,
                                    const int line_num, const int line_pos, std::vector<std::pair<int, std::wstring> >& found_completion)
{
  // static keywords
  found_completion.push_back(std::pair<int, std::wstring>(14, L"if"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"else"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"do"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"while"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"static"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"select"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"break"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"continue"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"other"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"for"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"each"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"reverse"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"label"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"return"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"critical"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"use"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"leaving"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"virtual"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"Parent"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"from"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"implements"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"Byte"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"Int"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"Float"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"Char"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"Bool"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"String"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"As"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"Nil"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"method"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"function"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"class"));

  // search code
  Class* context_klass = method->GetClass();
  SymbolTable* symbol_table = method->GetSymbolTable();
  std::vector<SymbolEntry*> entries = symbol_table->GetEntries();

  std::set<std::wstring> unique_names;

  if(var_str.empty() && !mthd_str.empty()) {
    // local variables
    for(size_t i = 0; i < entries.size(); ++i) {
      SymbolEntry* entry = entries[i];
      const std::wstring full_var_name = entry->GetName();
      const size_t short_var_pos = full_var_name.find_last_of(L':');
      const std::wstring short_var_name = full_var_name.substr(short_var_pos + 1, full_var_name.size() - short_var_pos - 1);
      if(short_var_name.rfind(mthd_str, 0) == 0) {
        std::set<std::wstring>::iterator found = unique_names.find(short_var_name);
        if(found == unique_names.end()) {
          unique_names.insert(short_var_name);
          found_completion.push_back(std::pair<int, std::wstring>(6, short_var_name));
        }
      }
    }
    
    // instance and class variables
    symbol_table = context_klass->GetSymbolTable();
    entries = symbol_table->GetEntries();
    for(size_t i = 0; i < entries.size(); ++i) {
      SymbolEntry* entry = entries[i];
      const std::wstring full_var_name = entry->GetName();
      const size_t short_var_pos = full_var_name.find_last_of(L':');
      std::wstring short_var_name = full_var_name.substr(short_var_pos + 1, full_var_name.size() - short_var_pos - 1);
      if(short_var_name.rfind(mthd_str, 0) == 0) {
        std::set<std::wstring>::iterator found = unique_names.find(short_var_name);
        if(found == unique_names.end()) {
          if(short_var_name[0] == L'@') {
            short_var_name.erase(0, 1);
          }

          unique_names.insert(short_var_name);
          found_completion.push_back(std::pair<int, std::wstring>(6, short_var_name));
        }
      }
    }

    // methods
    std::vector<Method*> methods = context_klass->GetMethods();
    const std::wstring search_str = L':' + mthd_str;
    for(size_t i = 0; i < methods.size(); ++i) {
      const std::wstring mthd_name = methods[i]->GetName();
      if(mthd_name.find(search_str, 0) != std::wstring::npos) {
        std::set<std::wstring>::iterator found = unique_names.find(mthd_name);
        if(found == unique_names.end()) {
          const size_t short_name_index = mthd_name.find_last_of(L':');
          if(short_name_index != std::wstring::npos) {
            const std::wstring short_name = mthd_name.substr(short_name_index + 1);
            unique_names.insert(short_name);
            found_completion.push_back(std::pair<int, std::wstring>(2, short_name));
          }
        }
      }
    }

    std::vector<Expression*> expressions = method->GetExpressions();
    for(size_t i = 0; i < expressions.size(); ++ i) {
      Expression* expression = expressions[i];
      if(expression->GetLineNumber() == line_num + 1 && expression->GetExpressionType() == METHOD_CALL_EXPR) {
        GetCompletionMethods(static_cast<MethodCall*>(expression), mthd_str, unique_names, found_completion);
      }
    }
  }
  else if(!var_str.empty() && !mthd_str.empty()) {
    std::vector<Method*> found_methods; std::vector<LibraryMethod*> found_lib_methods;

    // local variables
    SymbolTable* symbol_table = method->GetSymbolTable();
    std::vector<SymbolEntry*> entries = symbol_table->GetEntries();
    for(size_t i = 0; i < entries.size(); ++i) {  
      SymbolEntry* entry = entries[i];
      const std::wstring full_var_name = entry->GetName();
      const size_t short_var_pos = full_var_name.find_last_of(L':');
      if(short_var_pos != std::wstring::npos) {
        const std::wstring short_var_name = full_var_name.substr(short_var_pos + 1, full_var_name.size() - short_var_pos - 1);

        if(!var_str.empty() && iswdigit(var_str.front())) {
          // float literal
          if(var_str.find(L'.') != std::wstring::npos) {
            FindSignatureClass(type_map[L"Float"], mthd_str, context_klass, found_methods, found_lib_methods, true);
          }
          // integer literal
          else {
            FindSignatureClass(type_map[L"Int"], mthd_str, context_klass, found_methods, found_lib_methods, true);
          }
        }
        // character literal
        else if(!var_str.empty() && var_str.front() == L'\'') {
          FindSignatureClass(type_map[L"Char"], mthd_str, context_klass, found_methods, found_lib_methods, true);
        }
        // string literal
        else if(!var_str.empty() && var_str.front() == L'"') {
          FindSignatureClass(type_map[L"String"], mthd_str, context_klass, found_methods, found_lib_methods, true);
        }
        // variable
        else if(short_var_name == var_str) {
          FindSignatureClass(entry->GetType(), mthd_str, context_klass, found_methods, found_lib_methods, true);
        }
      }
    }

    // instance and class variables
    symbol_table = context_klass->GetSymbolTable();
    entries = symbol_table->GetEntries();
    for(size_t i = 0; i < entries.size(); ++i) {
      SymbolEntry* entry = entries[i];
      const std::wstring full_var_name = entry->GetName();
      const size_t short_var_pos = full_var_name.find_last_of(L':');
      if(short_var_pos != std::wstring::npos) {
        const std::wstring short_var_name = full_var_name.substr(short_var_pos + 1, full_var_name.size() - short_var_pos - 1);
        if(short_var_name == var_str) {
          FindSignatureClass(entry->GetType(), mthd_str, context_klass, found_methods, found_lib_methods, true);
        }
      }

      // find enums
      const std::vector<ParsedBundle*> bundles = program->GetBundles();
      for(size_t j = 0; j < bundles.size(); ++j) {
        ParsedBundle* bundle = bundles[j];
        const std::vector<Enum*> eenums = bundle->GetEnums();
        for(size_t k = 0; k < eenums.size(); ++k) {
          Enum* eenum = eenums[k];
          const std::wstring var_str_check =  L'#' + var_str;
          if(EndsWith(eenum->GetName(), var_str_check)) {
            std::map<const std::wstring, EnumItem*> eenum_items = eenum->GetItems();
            for(std::map<const std::wstring, EnumItem*>::iterator iter = eenum_items.begin(); iter != eenum_items.end(); ++iter) {
              if(iter->first.rfind(mthd_str, 0) == 0) {
                std::set<std::wstring>::iterator found = unique_names.find(iter->first);
                if(found == unique_names.end()) {
                  unique_names.insert(iter->first);
                  found_completion.push_back(std::pair<int, std::wstring>(2, iter->first));
                }
              }
            }
          }
        }
      }

      // find unique signatures
      if(!found_methods.empty()) {
        for(size_t j = 0; j < found_methods.size(); ++j) {
          const std::wstring mthd_name = found_methods[j]->GetName();
          const size_t mthd_name_start = mthd_name.find(L':');
          const size_t mthd_name_end = mthd_name.find_last_of(L':');

          if(mthd_name_start != std::wstring::npos && mthd_name_end != std::wstring::npos) {
            std::wstring short_mthd_name = mthd_name.substr(mthd_name_start + 1, mthd_name_end - mthd_name_start - 1);
            std::set<std::wstring>::iterator found = unique_names.find(short_mthd_name);
            if(found == unique_names.end()) {
              unique_names.insert(short_mthd_name);
              found_completion.push_back(std::pair<int, std::wstring>(2, short_mthd_name));
            }
          }
        }
      }
      else if(!found_lib_methods.empty()) {
        for(size_t i = 0; i < found_lib_methods.size(); ++i) {
          const std::wstring mthd_name = found_lib_methods[i]->GetName();
          const size_t mthd_name_start = mthd_name.find(L':');
          const size_t mthd_name_end = mthd_name.find_last_of(L':');

          if(mthd_name_start != std::wstring::npos && mthd_name_end != std::wstring::npos) {
            std::wstring short_mthd_name = mthd_name.substr(mthd_name_start + 1, mthd_name_end - mthd_name_start - 1);
            std::set<std::wstring>::iterator found = unique_names.find(short_mthd_name);
            if(found == unique_names.end()) {
              unique_names.insert(short_mthd_name);
              found_completion.push_back(std::pair<int, std::wstring>(2, short_mthd_name));
            }
          }
        }
      }
    }
  }

  return !found_completion.empty();
}

void ContextAnalyzer::GetCompletionMethods(MethodCall* mthd_call, const std::wstring mthd_str, std::set<std::wstring> unique_names, std::vector<std::pair<int, std::wstring> >& found_completion)
{
  while(mthd_call->GetMethodCall()) {
    mthd_call = mthd_call->GetMethodCall();
  }

  // line position
  // get the return type
  Type* rtrn_type = NULL;
  if(mthd_call->GetMethod()) {
    rtrn_type = mthd_call->GetMethod()->GetReturn();
  }
  else if(mthd_call->GetLibraryMethod()) {
    rtrn_type = mthd_call->GetLibraryMethod()->GetReturn();
  }

  if(rtrn_type) {
    Class* klass = nullptr; LibraryClass* lib_klass = nullptr;
    if(GetProgramOrLibraryClass(rtrn_type, klass, lib_klass)) {
      const std::wstring check_mthd_str = rtrn_type->GetName() + L':' + mthd_str;

      if(klass) {
        std::vector<Method*> klass_mthds = klass->GetMethods();
        for(size_t i = 0; i < klass_mthds.size(); ++i) {
          Method* klass_mthd = klass_mthds[i];
          const std::wstring klass_mthd_name = klass_mthd->GetEncodedName();
          if(klass_mthd_name.rfind(check_mthd_str, 0) == 0) {
            size_t short_name_start_index = klass_mthd_name.find_first_of(L':');
            const size_t short_name_end_index = klass_mthd_name.find_last_of(L':');
            if(short_name_start_index != std::wstring::npos && short_name_end_index != std::wstring::npos) {
              ++short_name_start_index;
              const std::wstring short_name = klass_mthd_name.substr(short_name_start_index, short_name_end_index - short_name_start_index);
              unique_names.insert(short_name);
              found_completion.push_back(std::pair<int, std::wstring>(2, short_name));
            }
          }
        }
      }
      else {
        std::map<const std::wstring, LibraryMethod*> klass_lib_mthds = lib_klass->GetMethods();
        std::map<const std::wstring, LibraryMethod*>::iterator iter;
        for(iter = klass_lib_mthds.begin(); iter != klass_lib_mthds.end(); ++iter) {
          const std::wstring klass_lib_mthd_name = iter->first;
          if(klass_lib_mthd_name.rfind(check_mthd_str, 0) == 0) {
            size_t short_name_start_index = klass_lib_mthd_name.find_first_of(L':');
            const size_t short_name_end_index = klass_lib_mthd_name.find_last_of(L':');
            if(short_name_start_index != std::wstring::npos && short_name_end_index != std::wstring::npos) {
              ++short_name_start_index;
              const std::wstring short_name = klass_lib_mthd_name.substr(short_name_start_index, short_name_end_index - short_name_start_index);
              unique_names.insert(short_name);
              found_completion.push_back(std::pair<int, std::wstring>(2, short_name));
            }
          }
        }
      }
    }
  }
}

bool ContextAnalyzer::GetSignature(Method* method, const std::wstring var_str, const std::wstring mthd_str, std::vector<Method*> &found_methods, std::vector<LibraryMethod*> & found_lib_methods)
{
  Class* context_klass = method->GetClass();

  if(var_str.empty() && !mthd_str.empty()) {
    FindSignatureMethods(context_klass, nullptr, mthd_str, found_methods, found_lib_methods);
    return !found_methods.empty();
  }
  else if(method && context_klass) {
    // local variables
    SymbolTable* symbol_table = method->GetSymbolTable();
    std::vector<SymbolEntry*> entries = symbol_table->GetEntries();
    for(size_t i = 0; i < entries.size(); ++i) {
      SymbolEntry* entry = entries[i];
      const std::wstring full_var_name = entry->GetName();
      const size_t short_var_pos = full_var_name.find_last_of(L':');
      if(short_var_pos != std::wstring::npos) {
        const std::wstring short_var_name = full_var_name.substr(short_var_pos + 1, full_var_name.size() - short_var_pos - 1);
        if(short_var_name == var_str) {
          FindSignatureClass(entry->GetType(), mthd_str, context_klass, found_methods, found_lib_methods, false);
          return !found_methods.empty() || !found_lib_methods.empty();
        }
      }
    }

    // instance and class variables
    symbol_table = context_klass->GetSymbolTable();
    entries = symbol_table->GetEntries();
    for(size_t i = 0; i < entries.size(); ++i) {
      SymbolEntry* entry = entries[i];
      const std::wstring full_var_name = entry->GetName();
      const size_t short_var_pos = full_var_name.find_last_of(L':');
      if(short_var_pos != std::wstring::npos) {
        const std::wstring short_var_name = full_var_name.substr(short_var_pos + 1, full_var_name.size() - short_var_pos - 1);
        if(short_var_name == var_str) {
          FindSignatureClass(entry->GetType(), mthd_str, context_klass, found_methods, found_lib_methods, false);
          return !found_methods.empty() || !found_lib_methods.empty();
        }
      }
    }
  }
  
  return false;
}

void ContextAnalyzer::FindSignatureClass(Type* type, const std::wstring mthd_str, Class* context_klass, std::vector<Method*> &found_methods, std::vector<LibraryMethod*>& found_lib_methods, bool is_completion)
{
  Class* klass = nullptr; LibraryClass* lib_klass = nullptr;

  switch(type->GetType()) {
  case NIL_TYPE:
    break;

  case BOOLEAN_TYPE:
    lib_klass = linker->SearchClassLibraries(BOOL_CLASS_ID, program->GetLibUses(context_klass->GetFileName()));
    break;

  case BYTE_TYPE:
    lib_klass = linker->SearchClassLibraries(BYTE_CLASS_ID, program->GetLibUses(context_klass->GetFileName()));

    break;

  case CHAR_TYPE:
    lib_klass = linker->SearchClassLibraries(CHAR_CLASS_ID, program->GetLibUses(context_klass->GetFileName()));
    break;

  case INT_TYPE:
    lib_klass = linker->SearchClassLibraries(INT_CLASS_ID, program->GetLibUses(context_klass->GetFileName()));
    break;

  case FLOAT_TYPE:
    lib_klass = linker->SearchClassLibraries(FLOAT_CLASS_ID, program->GetLibUses(context_klass->GetFileName()));
    break;

  case CLASS_TYPE:
    klass = SearchProgramClasses(type->GetName());
    if(!klass) {
      lib_klass = linker->SearchClassLibraries(type->GetName(), program->GetLibUses(context_klass->GetFileName()));
    }
    break;

  case FUNC_TYPE:
    break;

  case ALIAS_TYPE:
    break;

  case VAR_TYPE:
    break;
  }

  if(is_completion) {
    FindCompletionMethods(klass, lib_klass, mthd_str, found_methods, found_lib_methods);
  }
  else {
    FindSignatureMethods(klass, lib_klass, mthd_str, found_methods, found_lib_methods);
  }
}

void ContextAnalyzer::FindCompletionMethods(Class* klass, LibraryClass* lib_klass, const std::wstring mthd_str, std::vector<Method*>& found_methods, std::vector<LibraryMethod*>& found_lib_methods)
{
  if(klass) {
    std::vector<Method*> methods = klass->GetMethods();
    const std::wstring search_str = L':' + mthd_str;
    for(size_t i = 0; i < methods.size(); ++i) {
      const std::wstring mthd_name = methods[i]->GetName();
      if(mthd_name.find(search_str, 0) != std::wstring::npos) {
        found_methods.push_back(methods[i]);
      }
    }
  }
  else if(lib_klass) {
    std::map<const std::wstring, LibraryMethod*> lib_methods = lib_klass->GetMethods();
    const std::wstring search_str = L':' + mthd_str;
    for(std::map<const std::wstring, LibraryMethod*>::iterator iter = lib_methods.begin(); iter != lib_methods.end(); ++iter) {
      if(iter->first.find(search_str, 0) != std::wstring::npos) {
        found_lib_methods.push_back(iter->second);
      }
    }
  }
}

void ContextAnalyzer::FindSignatureMethods(Class* klass, LibraryClass* lib_klass, const std::wstring mthd_str, std::vector<Method*>& found_methods, std::vector<LibraryMethod*>& found_lib_methods)
{
  if(klass) {
    std::vector<Method*> methods = klass->GetMethods();
    const std::wstring search_str = L':' + mthd_str;
    for(size_t i = 0; i < methods.size(); ++i) {
      const std::wstring mthd_name = methods[i]->GetName();
      if(mthd_name.find(search_str, 0) != std::wstring::npos) {
        found_methods.push_back(methods[i]);
      }
    }
  }
  else if(lib_klass) {
    std::map<const std::wstring, LibraryMethod*> lib_methods = lib_klass->GetMethods();
    const std::wstring search_str = L':' + mthd_str;
    for(std::map<const std::wstring, LibraryMethod*>::iterator iter = lib_methods.begin(); iter != lib_methods.end(); ++iter) {
      if(iter->first.find(search_str, 0) != std::wstring::npos) {
        found_lib_methods.push_back(iter->second);
      }
    }
  }
}

//
// definitions
//

bool ContextAnalyzer::GetDefinition(Class* klass, const int line_num, const int line_pos, Class*& found_klass)
{
  std::vector<Statement*> decelerations = klass->GetStatements();
  for(size_t i = 0; i < decelerations.size(); ++i) {
    if(decelerations[i]->GetStatementType() == DECLARATION_STMT) {
      Declaration* deceleration = static_cast<Declaration*>(decelerations[i]);

      const int start_line = deceleration->GetLineNumber();
      const int start_pos = deceleration->GetLinePosition();
      const int end_pos = deceleration->GetEndLinePosition();

      if(start_line - 1 == line_num && start_pos <= line_pos && end_pos >= line_pos) {
        const std::wstring search_name = deceleration->GetEntry()->GetType()->GetName();
        found_klass = SearchProgramClasses(search_name);
        return found_klass != nullptr;
      }
    }
  }

  return false;
}

bool ContextAnalyzer::GetDefinition(Method* &method, const int line_num, const int line_pos, std::wstring& found_name, 
                                    int& found_line, int& found_start_pos, int& found_end_pos, Class* &klass, 
                                    Enum*& eenum, EnumItem*& eenum_item)
{
  // find matching expressions
  std::vector<Expression*> all_expressions;
  Expression* found_expression = nullptr;
  bool is_alt = false;

  if(LocateExpression(method, line_num, line_pos, found_expression, found_name, is_alt, all_expressions)) {
    const std::wstring entry_name = method->GetName() + L':' + found_name;
    SymbolEntry* found_entry = method->GetSymbolTable()->GetEntry(entry_name);

    // found variable
    if(found_entry && found_entry->GetType()) {
      const std::wstring search_name = found_entry->GetType()->GetName();
      // found class
      klass = SearchProgramClasses(search_name);
      if(klass) {
        return true;
      }
      else {
        // found enum
        eenum = SearchProgramEnums(search_name);
        if(eenum) {
          return true;
        }
      }
    }
    // found enum
    else if(found_expression->GetExpressionType() == METHOD_CALL_EXPR && static_cast<MethodCall*>(found_expression)->GetEnumItem()) {
      MethodCall* mthd_call = static_cast<MethodCall*>(found_expression);
      eenum = SearchProgramEnums(found_name);
      if(eenum) {
        std::map<const std::wstring, EnumItem*> eenum_items = eenum->GetItems();
        std::map<const std::wstring, EnumItem*>::iterator result = eenum_items.find(mthd_call->GetMethodName());
        if(result != eenum_items.end()) {
          eenum_item = result->second;
        }
        return true;
      }
    }
    // found method
    else {
      for(size_t i = 0; i < all_expressions.size(); ++i) {
        Expression* expr = all_expressions[i];
        if(expr->GetExpressionType() == METHOD_CALL_EXPR) {
          MethodCall* mthd_call = static_cast<MethodCall*>(expr);
          if(mthd_call->GetMethodName() == found_name && mthd_call->GetMethod()) {
            method = mthd_call->GetMethod();
            return true;
          }
        }
      }
    }
  }
  
  return false;
}

//
// declarations
//
bool ContextAnalyzer::GetHover(Method* method, const int line_num, const int line_pos, 
                               std::wstring& found_name, int& found_line, int& found_start_pos, int& found_end_pos,
                               Expression* &found_expression, SymbolEntry* &found_entry)
{
  // find matching expressions
  found_expression = nullptr;
  found_entry = nullptr;

  std::vector<Expression*> all_expressions; bool is_alt = false;
  if(LocateExpression(method, line_num, line_pos, found_expression, found_name, is_alt, all_expressions)) {
    // function/method lookup
    if(is_alt) {
      if(found_expression->GetExpressionType() == METHOD_CALL_EXPR || found_expression->GetExpressionType() == VAR_EXPR) {
        return true;
      }
    }
    // variable lookup
    else {
      const std::wstring class_entry_name = method->GetClass()->GetName() + L':' + found_name;
      found_entry = method->GetClass()->GetSymbolTable()->GetEntry(class_entry_name);
      if(found_entry) {
        return true;
      }
      else {
        const std::wstring method_entry_name = method->GetName() + L':' + found_name;
        found_entry = method->GetSymbolTable()->GetEntry(method_entry_name);
        if(found_entry) {
          return true;
        }
      }
    }
  }

  return false;
}

//
// declarations
//
bool ContextAnalyzer::GetDeclaration(Method* method, const int line_num, const int line_pos, std::wstring& found_name, int& found_line, int& found_start_pos, int& found_end_pos)
{
  // find matching expressions
  std::vector<Expression*> all_expressions;
  Expression* found_expression = nullptr;
  bool is_alt = false;

  if(LocateExpression(method, line_num, line_pos, found_expression, found_name, is_alt, all_expressions)) {
    // function/method lookup
    if(is_alt) {
      Method* called_method = static_cast<MethodCall*>(found_expression)->GetMethod();
      if(called_method) {
        found_name = called_method->GetName();
        found_line = called_method->GetLineNumber();
        found_start_pos = called_method->GetLinePosition();
        found_end_pos = called_method->GetReturn()->GetLinePosition();

        return true;
      }
    }
    // variable lookup
    else {
      const std::wstring class_entry_name = method->GetClass()->GetName() + L':' + found_name;
      SymbolEntry* found_entry = method->GetClass()->GetSymbolTable()->GetEntry(class_entry_name);
      if(found_entry) {
        found_name = found_entry->GetName();
        size_t var_name_pos = found_name.find_last_of(L':');
        if(var_name_pos != std::wstring::npos) {
          found_name = found_name.substr(var_name_pos + 1, found_name.size() - var_name_pos - 1);
        }
        found_line = found_entry->GetLineNumber();
        found_start_pos = found_end_pos = found_entry->GetLinePosition() - 1;
        found_end_pos += (int)found_name.size();

        return true;
      }
      else {
        const std::wstring method_entry_name = method->GetName() + L':' + found_name;
        found_entry = method->GetSymbolTable()->GetEntry(method_entry_name);
        if(found_entry) {
          found_name = found_entry->GetName();
          size_t var_name_pos = found_name.find_last_of(L':');
          if(var_name_pos != std::wstring::npos) {
            found_name = found_name.substr(var_name_pos + 1, found_name.size() - var_name_pos - 1);
          }
          found_line = found_entry->GetLineNumber();
          found_start_pos = found_end_pos = found_entry->GetLinePosition() - 1;
          found_end_pos += (int)found_name.size();

          return true;
        }
      }
    }
  }

  return false;
}


Declaration* ContextAnalyzer::FindDeclaration(Class* klass, const int line_num, const int line_pos)
{
  std::vector<Expression*> expressions;

  std::vector<Statement*> decelerations = klass->GetStatements();
  for(size_t i = 0; i < decelerations.size(); ++i) {
    if(decelerations[i]->GetStatementType() == DECLARATION_STMT) {
      Declaration* deceleration = static_cast<Declaration*>(decelerations[i]);

      const int start_line = deceleration->GetLineNumber();
      const int start_pos = deceleration->GetLinePosition();
      const int end_pos = deceleration->GetEndLinePosition();

      if(start_line - 1 == line_num && start_pos <= line_pos && end_pos >= line_pos) {
        return deceleration;
      }
    }
  }

  return nullptr;
}

std::vector<Expression*> ContextAnalyzer::FindExpressions(Method* method, const int line_num, const int line_pos, bool &is_var, bool& is_cls)
{
  std::vector<Expression*> matched_expressions;
  is_var = true;
  is_cls = false;

  // find matching expressions
  std::vector<Expression*> all_expressions;
  Expression* found_expression = nullptr;
  std::wstring found_name;
  bool is_alt = false;

  if(LocateExpression(method, line_num, line_pos, found_expression, found_name, is_alt, all_expressions)) {
    for(size_t i = 0; i < all_expressions.size(); ++i) {
      Expression* expression = all_expressions[i];
      switch(expression->GetExpressionType()) {
      case VAR_EXPR: {
        Variable* variable = static_cast<Variable*>(expression);
        if(variable->GetName() == found_name) {
          if(variable->GetEntry() && !variable->GetEntry()->IsLocal()) {
            is_cls = true;
          }
          matched_expressions.push_back(expression);
        }
      }
        break;

      case METHOD_CALL_EXPR: {
        MethodCall* method_call = static_cast<MethodCall*>(expression);
        if(method_call->GetVariableName() == found_name) {
          matched_expressions.push_back(expression);
        }
        else if(method_call->GetMethodName() == found_name) {
          matched_expressions.push_back(expression);
          is_var = false;
        }
      }
        break;

      default:
        break;
      }
    }
  }

  // find method calls associated with methods
  if(matched_expressions.empty()) {
    GetMethodCallExpressions(line_num, line_pos, is_var, matched_expressions);
  }

  return matched_expressions;
}

void ContextAnalyzer::GetMethodCallExpressions(const int line_num, const int line_pos, bool &is_var, std::vector<Expression*> &matched_expressions)
{
  std::vector<ParsedBundle*> bundles = program->GetBundles();
  for(auto bundle : bundles) {
    std::vector<Class*> classes = bundle->GetClasses();
    for(auto klass : classes) {
      std::vector<Method*> methods = klass->GetMethods();
      for(auto method : methods) {
        const int mthd_line_num = method->GetLineNumber() - 1;
        if(mthd_line_num == line_num) {
          const std::wstring mthd_long_name = method->GetName();
          const size_t mthd_long_name_index = mthd_long_name.find(':');
          if(mthd_long_name_index != std::wstring::npos) {
            const std::wstring mthd_name = mthd_long_name.substr(mthd_long_name_index + 1);
            const int start_pos = method->GetMidLinePosition();
            const int end_pos = start_pos + (int)mthd_name.size();
            if(start_pos < line_pos && end_pos > line_pos) {
              is_var = false;
              const std::vector<MethodCall*> method_calls = method->GetMethodCalls();
              if(method_calls.empty()) {
                MethodCall* method_call = TreeFactory::Instance()->MakeMethodCall(method->GetFileName(), method->GetLineNumber(), method->GetLinePosition(),
                                                                                  method->GetEndLineNumber(), method->GetEndLinePosition(), L"#", method->GetName());
                method_call->SetMethod(method);
                matched_expressions.push_back(method_call);
              }
              else {
                for(auto method_call : method_calls) {
                  matched_expressions.push_back(method_call);
                }
              }
            }
          }
        }
      }
    }
  }
}

bool ContextAnalyzer::LocateExpression(Method* method, const int line_num, const int line_pos, Expression*& found_expression, 
                                       std::wstring& found_name, bool &is_alt, std::vector<Expression*>& all_expressions)
{
  // get all expressions
  all_expressions = method->GetExpressions();

  // local entries
  std::vector<SymbolEntry*> local_entries = symbol_table->GetEntries(method->GetParsedName());
  for(size_t i = 0; i < local_entries.size(); ++i) {
    SymbolEntry* local_entry = local_entries[i];
    
    // add declaration
    const std::wstring full_entry_name = local_entry->GetName();
    const size_t full_entry_index = full_entry_name.find_last_of(L':');
    if(full_entry_index != std::wstring::npos) {
      const std::wstring entry_name = full_entry_name.substr(full_entry_index + 1, full_entry_name.size() - full_entry_index + 1);
      Variable* variable = TreeFactory::Instance()->MakeVariable(local_entry->GetFileName(), local_entry->GetLineNumber(),
                                                                 local_entry->GetLinePosition(), entry_name);
      variable->SetEntry(local_entry);
      all_expressions.push_back(variable);
    }

    // add variable references
    const std::vector<Variable*> variables = local_entry->GetVariables();
    for(size_t j = 0; j < variables.size(); ++j) {
      all_expressions.push_back(variables[j]);
    }
  }

  // instance and class entries
  std::vector<SymbolEntry*> class_entries = symbol_table->GetEntries(method->GetClass()->GetName());
  for(size_t i = 0; i < class_entries.size(); ++i) {
    SymbolEntry* class_entry = class_entries[i];
    
    // add declaration
    const std::wstring full_entry_name = class_entry->GetName();
    const size_t full_entry_index = full_entry_name.find_last_of(L':');
    if(full_entry_index != std::wstring::npos) {
      const std::wstring entry_name = full_entry_name.substr(full_entry_index + 1, full_entry_name.size() - full_entry_index + 1);
      Variable* variable = TreeFactory::Instance()->MakeVariable(class_entry->GetFileName(), class_entry->GetLineNumber(), 
                                                                 class_entry->GetLinePosition(), entry_name);
      variable->SetEntry(class_entry);
      all_expressions.push_back(variable);
    }
    
    // add variable references
    const std::vector<Variable*> variables = class_entry->GetVariables();
    for(size_t j = 0; j < variables.size(); ++j) {
      all_expressions.push_back(variables[j]);
    }
  }
  
  // class declarations
  std::vector<Expression*> class_dclrs = method->GetClass()->GetExpressions();
  for(size_t i = 0; i < class_dclrs.size(); ++i) {
    all_expressions.push_back(class_dclrs[i]);
  }

  // find expression
  std::wstring alt_found_name;
  for(size_t i = 0; !found_expression && i < all_expressions.size(); ++i) {
    Expression* expression = all_expressions[i];
    if(expression->GetLineNumber() == line_num + 1) {
      int start_pos = expression->GetLinePosition() - 1;
      int end_pos = start_pos;
      
      int alt_start_pos = -1;
      int alt_end_pos = -1;

      switch(expression->GetExpressionType()) {
      case VAR_EXPR: {
        Variable* variable = static_cast<Variable*>(expression);
        found_name = variable->GetName();
        end_pos += (int)found_name.size();
      }
        break;

      case METHOD_CALL_EXPR: {
        MethodCall* method_call = static_cast<MethodCall*>(expression);
        if(method_call->GetEntry()) {
          found_name = method_call->GetVariableName();
          end_pos += (int)found_name.size();

          alt_found_name = method_call->GetMethodName();
          alt_start_pos = alt_end_pos = method_call->GetMidLinePosition();
          alt_end_pos += (int)alt_found_name.size();
        }
        else if(method_call->GetMethod()) {
          found_name = method_call->GetMethodName();
          end_pos += (int)found_name.size();

          alt_found_name = method_call->GetVariableName();
          alt_start_pos = alt_end_pos = method_call->GetMidLinePosition();
          alt_end_pos += (int)alt_found_name.size();
        }
        else if(method_call->GetCallType() == ENUM_CALL) {
          found_name = method_call->GetVariableName();
          end_pos += (int)found_name.size();

          alt_found_name = method_call->GetMethodName();
          end_pos = method_call->GetEndLinePosition();
        }
        else {
          found_name = L"@self";
          end_pos += (int)found_name.size();

          alt_found_name = method_call->GetMethodName();
          alt_start_pos = alt_end_pos = method_call->GetMidLinePosition();
          alt_end_pos += (int)alt_found_name.size();
        }
      }
        break;

      default:
        break;
      }

      if((start_pos <= line_pos && end_pos >= line_pos) || (alt_start_pos <= line_pos && alt_end_pos >= line_pos)) {
        if((alt_start_pos <= line_pos && alt_end_pos >= line_pos) || found_name == L"@self") {
          found_name = alt_found_name;
          is_alt = true;
        }
        else {
          is_alt = false;
        }
        
        found_expression = expression;
        return true;
      }
    }
  }

  return false;
}

bool ContextAnalyzer::LocateExpression(Class* klass, const int line_num, const int line_pos, Expression*& found_expression,
                                       std::wstring& found_name, bool& is_alt, std::vector<Expression*>& all_expressions)
{
  // class declarations
  std::vector<Statement*> class_dclrs = klass->GetStatements();
  for(size_t i = 0; i < class_dclrs.size(); ++i) {
    Declaration* class_dclr = static_cast<Declaration*> (class_dclrs[i]);
    std::vector<Variable*> variables = class_dclr->GetEntry()->GetVariables();
    for(size_t j = 0; j < variables.size(); ++j) {
      all_expressions.push_back(variables[j]);
    }
  }

  // find expression
  std::wstring alt_found_name;
  for(size_t i = 0; !found_expression && i < all_expressions.size(); ++i) {
    Expression* expression = all_expressions[i];
    if(expression->GetLineNumber() == line_num + 1) {
      const int start_pos = expression->GetLinePosition() - 1;
      int end_pos = start_pos;

      int alt_start_pos = -1;
      int alt_end_pos = -1;

      switch(expression->GetExpressionType()) {
      case VAR_EXPR: {
        Variable* variable = static_cast<Variable*>(expression);
        found_name = variable->GetName();
        end_pos += (int)found_name.size();
      }
        break;

      case METHOD_CALL_EXPR: {
        MethodCall* method_call = static_cast<MethodCall*>(expression);
        if(method_call->GetEntry()) {
          found_name = method_call->GetVariableName();
          end_pos += (int)found_name.size();

          alt_found_name = method_call->GetMethodName();
          alt_start_pos = alt_end_pos = method_call->GetMidLinePosition();
          alt_end_pos += (int)alt_found_name.size();
        }
        else {
          found_name = L"@self";
          end_pos += (int)found_name.size();

          alt_found_name = method_call->GetMethodName();
          alt_start_pos = alt_end_pos = method_call->GetMidLinePosition();
          alt_end_pos += (int)alt_found_name.size();
        }
      }
        break;

      default:
        break;
      }

      if((start_pos <= line_pos && end_pos >= line_pos) || (alt_start_pos <= line_pos && alt_end_pos >= line_pos)) {
        if((alt_start_pos <= line_pos && alt_end_pos >= line_pos) || found_name == L"@self") {
          found_name = alt_found_name;
          is_alt = true;
        }
        else {
          is_alt = false;
        }

        found_expression = expression;
        return true;
      }
    }
  }

  return false;
}

#endif
//
// END: diagnostics operations
//
