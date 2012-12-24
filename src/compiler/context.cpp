/***************************************************************************
 * Performs contextual analysis.
 *
 * Copyright (c) 2008-2012, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/cor other materials provided with the distribution.
 * - Neither the name of the StackVM Team nor the names of its
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

#include "context.h"
#include "linker.h"
#include "../shared/instrs.h"

/****************************
 * Emits an error
 ****************************/
void ContextAnalyzer::ProcessError(ParseNode* node, const string &msg)
{
#ifdef _DEBUG
  cout << "\tError: " << node->GetFileName() << ":" << node->GetLineNumber()
       << ": " << msg << endl;
#endif

  const string &str_line_num = ToString(node->GetLineNumber());
  errors.insert(pair<int, string>(node->GetLineNumber(), node->GetFileName() +
                                  ":" + str_line_num + ": " + msg));
}

/****************************
 * Emits an error
 ****************************/
void ContextAnalyzer::ProcessError(const string &msg)
{
#ifdef _DEBUG
  cout << "\tError: " << msg << endl;
#endif

  errors.insert(pair<int, string>(0, msg));
}

/****************************
 * Check for errors detected
 * during the contextual
 * analysis process.
 ****************************/
bool ContextAnalyzer::CheckErrors()
{
  // check and process errors
  if(errors.size()) {
    map<int, string>::iterator error;
    for(error = errors.begin(); error != errors.end(); ++error) {
      cerr << error->second << endl;
    }

    // clean up
    delete program;
    program = NULL;

    return false;
  }

  return true;
}

/****************************
 * Starts the analysis process
 ****************************/
bool ContextAnalyzer::Analyze()
{
#ifdef _DEBUG
  cout << "\n--------- Contextual Analysis ---------" << endl;
#endif
  int class_id = 0;

#ifndef _SYSTEM
  // process libraries classes
  linker->Load();
#endif

  // check uses
  vector<string> program_uses = program->GetUses();
  for(size_t i = 0; i < program_uses.size(); i++) {
    const string& name = program_uses[i];
    if(!program->HasBundleName(name) && !linker->HasBundleName(name)) {
      ProcessError("Bundle name '" + name + "' not defined in program or linked libraries");
    }
  }

  // re-encode method signatures; i.e. fully expand class names
  vector<ParsedBundle*> bundles = program->GetBundles();
  for(size_t i = 0; i < bundles.size(); i++) {
    vector<Class*> classes = bundles[i]->GetClasses();
    for(size_t j = 0; j < classes.size(); j++) {
      vector<Method*> methods = classes[j]->GetMethods();
      for(size_t k = 0; k < methods.size(); k++) {
        methods[k]->EncodeSignature(program, linker);
      }
    }
  }

  // associate re-encoded method signatures with methods
  for(size_t i = 0; i < bundles.size(); i++) {
    bundle = bundles[i];
    vector<Class*> classes = bundle->GetClasses();
    
    for(size_t j = 0; j < classes.size(); j++) {
      Class* klass = classes[j];
      string parent_name = klass->GetParentName();
      // TODO: fix ulgy hack!!!
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
	  } else {
	    LibraryClass* lib_parent = linker->SearchClassLibraries(parent_name, program->GetUses());
	    if(lib_parent) {
	      klass->SetLibraryParent(lib_parent);
	      lib_parent->AddChild(klass);
	    } else {
	      ProcessError(klass, "Attempting to inherent from an undefined class type");
	    }
	  }
	}
	// associate methods
	classes[j]->AssociateMethods();
      }
    }
  
    // process bundles
    bundles = program->GetBundles();
    for(size_t i = 0; i < bundles.size(); i++) {
      bundle = bundles[i];
      symbol_table = bundle->GetSymbolTableManager();

      // process enums
      vector<Enum*> enums = bundle->GetEnums();
      for(size_t j = 0; j < enums.size(); j++) {
	AnalyzeEnum(enums[j], 0);
      }
      // process classes
      vector<Class*> classes = bundle->GetClasses();
      for(size_t j = 0; j < classes.size(); j++) {
	AnalyzeClass(classes[j], class_id++, 0);
      }
      // process class methods
      classes = bundle->GetClasses();
      for(size_t j = 0; j < classes.size(); j++) {
	AnalyzeMethods(classes[j], 0);
      }
    }
  
    if(!main_found && !is_lib && !is_web) {
      ProcessError("The 'Main(args)' function was not defined");
    }
    
    if(is_web && !web_found) {
      ProcessError("The 'Request(args)' function was not defined");
    } 

    return CheckErrors();
  }

  /****************************
   * Analyzes a class
   ****************************/
  void ContextAnalyzer::AnalyzeEnum(Enum* eenum, int depth)
  {
#ifdef _DEBUG
    string msg = "[enum: name='" + eenum->GetName() + "']";
    Show(msg, eenum->GetLineNumber(), depth);
#endif

    if(!SearchProgramEnums(eenum->GetName()) &&
       !linker->SearchEnumLibraries(eenum->GetName(), program->GetUses())) {
      ProcessError(eenum, "Undefined enum: '" + eenum->GetName() + "'");
    }

    if(SearchProgramEnums(eenum->GetName()) &&
       linker->SearchEnumLibraries(eenum->GetName(), program->GetUses())) {
      ProcessError(eenum, "Enum '" + eenum->GetName() +
		   "' defined in program and shared libraries");
    }
  }

  /****************************
   * Analyzes a class
   ****************************/
  void ContextAnalyzer::AnalyzeClass(Class* klass, int id, int depth)
  {
#ifdef _DEBUG
    string msg = "[class: name='" + klass->GetName() + "'; id=" + ToString(id) +
      "; virtual=" + ToString(klass->IsVirtual()) + "]";
    Show(msg, klass->GetLineNumber(), depth);
#endif

    current_class = klass;
    current_class->SetCalled(true);
    klass->SetSymbolTable(symbol_table->GetSymbolTable(current_class->GetName()));
    if(!SearchProgramClasses(klass->GetName()) &&
       !linker->SearchClassLibraries(klass->GetName(), program->GetUses())) {
      ProcessError(klass, "Undefined class: '" + klass->GetName() + "'");
    }

    if(SearchProgramClasses(klass->GetName()) &&
       linker->SearchClassLibraries(klass->GetName(), program->GetUses())) {
      ProcessError(klass, "Class '" + klass->GetName() +
		   "' defined in program and shared libraries");
    }
  
    // check parent class
    Class* parent_klass = klass->GetParent();
    if(parent_klass && parent_klass->IsInterface()) {
      ProcessError(klass, "Classes cannot be derived from interfaces, however classes may implement interfaces");
    }
    else {
      LibraryClass* parent_lib_klass = klass->GetLibraryParent();
      if(parent_lib_klass && parent_lib_klass->IsInterface()) {
	ProcessError(klass, "Classes cannot be derived from interfaces, however classes may implement interfaces");
      }
    }
    // check interfaces
    AnalyzeInterfaces(klass, depth);
  
    // declarations
    vector<Statement*> statements = klass->GetStatements();
    for(size_t i = 0; i < statements.size(); i++) {
      AnalyzeDeclaration(static_cast<Declaration*>(statements[i]), depth + 1);
    }
  }

  /****************************
   * Analyzes methods
   ****************************/
  void ContextAnalyzer::AnalyzeMethods(Class* klass, int depth)
  {
#ifdef _DEBUG
    string msg = "[class: name='" + klass->GetName() + "]";
    Show(msg, klass->GetLineNumber(), depth);
#endif

    current_class = klass;
    current_table = symbol_table->GetSymbolTable(current_class->GetName());

    // methods
    vector<Method*> methods = klass->GetMethods();
    for(size_t i = 0; i < methods.size(); i++) {
      AnalyzeMethod(methods[i], i, depth + 1);
    }

    // look for parent virutal methods
    if(current_class->GetParent() && current_class->GetParent()->IsVirtual()) {
      if(!AnalyzeVirtualMethods(current_class, current_class->GetParent(), depth)) {
	ProcessError(current_class, "Not all virtual methods have been implemented for the class/interface: " +
		     current_class->GetParent()->GetName());
      }
    }
    else if(current_class->GetLibraryParent() && current_class->GetLibraryParent()->IsVirtual()) {
      if(!AnalyzeVirtualMethods(current_class, current_class->GetLibraryParent(), depth)) {
	ProcessError(current_class, "Not all virtual methods have been implemented for the class/interface: " +
		     current_class->GetLibraryParent()->GetName());
      }
    }
  }

  /****************************
   * Checks for interface
   * implementations
   ****************************/
  void ContextAnalyzer::AnalyzeInterfaces(Class* klass, int depth)
  {
    vector<string> interface_names = klass->GetInterfaceNames();
    vector<Class*> interfaces;
    vector<LibraryClass*> lib_interfaces;
    for(size_t i = 0; i < interface_names.size(); i++) {
      const string& interface_name = interface_names[i];
      Class* inf_klass = SearchProgramClasses(interface_name);
    
      if(inf_klass) {
	// ensure interface methods are virtual
	vector<Method*> methods = inf_klass->GetMethods();
	for(size_t i = 0; i < methods.size(); i++) {
	  if(!methods[i]->IsVirtual()) {
	    ProcessError(methods[i], "Interface method must be defined as 'virtual'");
	  }
	}
	// ensure implementation
	if(!AnalyzeVirtualMethods(klass, inf_klass, depth)) {
	  ProcessError(klass, "Not all methods have been implemented for the interface: " +
		       inf_klass->GetName());
	}

	if(!inf_klass->IsInterface()) {
	  ProcessError(klass, "Classes cannot be implemented, however classes may be derived from other classes");
	}
	else {
	  // add interface
	  inf_klass->SetCalled(true);
	  inf_klass->AddChild(klass);
	  interfaces.push_back(inf_klass);
	}
      }
      else {
	LibraryClass* inf_lib_klass = linker->SearchClassLibraries(interface_name, program->GetUses());
	if(inf_lib_klass) {
	  // ensure interface methods are virtual
	  map<const string, LibraryMethod*> lib_methods = inf_lib_klass->GetMethods();
	  map<const string, LibraryMethod*>::iterator iter;
	  for(iter = lib_methods.begin(); iter != lib_methods.end(); ++iter) {
	    LibraryMethod* lib_method = iter->second;
	    if(!lib_method->IsVirtual()) {
	      ProcessError(klass, "Interface method must be defined as 'virtual'");
	    }
	  }
	  // ensure implementation
	  if(!AnalyzeVirtualMethods(klass, inf_lib_klass, depth)) {
	    ProcessError(klass, "Not all methods have been implemented for the interface: " +
			 inf_lib_klass->GetName());
	  }

	  if(!inf_lib_klass->IsInterface()) {
	    ProcessError(klass, "Classes cannot be implemented, however classes may be derived from other classes");
	  }
	  else {
	    // add interface
	    inf_lib_klass->SetCalled(true);
	    inf_lib_klass->AddChild(klass);
	    lib_interfaces.push_back(inf_lib_klass);
	  }
	}
	else {
	  ProcessError(klass, "Undefined interface: '" + interface_name + "'");
	}
      }
    }
    // save interfaces
    klass->SetInterfaces(interfaces);
    klass->SetLibraryInterfaces(lib_interfaces);
  }

  /****************************
   * Checks for virutal method
   * implementations
   ****************************/
  bool ContextAnalyzer::AnalyzeVirtualMethods(Class* impl_class, Class* virtual_class, int depth)
  {
    // get virutal methods
    bool virtual_methods_defined = true;
    vector<Method*> virtual_class_methods = virtual_class->GetMethods();
    for(size_t i = 0; i < virtual_class_methods.size(); i++) {
      if(virtual_class_methods[i]->IsVirtual()) {
	// validate that methods have been implemented
	Method* virtual_method = virtual_class_methods[i];
	string virtual_method_name = virtual_method->GetEncodedName();
      
	// search for implementation method via signature
	Method* impl_method = NULL;
	LibraryMethod* lib_impl_method = NULL;
	int offset = (int)virtual_method_name.find_first_of(':');
	if(offset > -1) {
	  string encoded_name = impl_class->GetName() + virtual_method_name.substr(offset);
	  impl_method = impl_class->GetMethod(encoded_name);	
	  if(!impl_method && impl_class->GetParent()) {
	    Class* parent_class = impl_class->GetParent();
	    while(!impl_method && !lib_impl_method && parent_class) {
	      encoded_name = parent_class->GetName() + virtual_method_name.substr(offset);
	      impl_method = parent_class->GetMethod(encoded_name);	      
	      // update	    
	      if(!impl_method && parent_class->GetLibraryParent()) {
		LibraryClass* lib_parent_class = parent_class->GetLibraryParent();
		encoded_name = lib_parent_class->GetName() + virtual_method_name.substr(offset);
		lib_impl_method = lib_parent_class->GetMethod(encoded_name);
		break;
	      }
	      parent_class = parent_class->GetParent();
	    }
	  }
	  else if(impl_class->GetLibraryParent()) {
	    LibraryClass* lib_parent_class = impl_class->GetLibraryParent();
	    encoded_name = lib_parent_class->GetName() + virtual_method_name.substr(offset);
	    lib_impl_method = lib_parent_class->GetMethod(encoded_name);
	  }
	}
      
	// validate method
	if(impl_method) {
	  AnalyzeVirtualMethod(impl_class, impl_method->GetMethodType(), impl_method->GetReturn(), 
			       impl_method->IsStatic(), impl_method->IsVirtual(), virtual_method);
	}
	else if(lib_impl_method) {
	  AnalyzeVirtualMethod(impl_class, lib_impl_method->GetMethodType(), lib_impl_method->GetReturn(), 
			       lib_impl_method->IsStatic(), lib_impl_method->IsVirtual(), virtual_method);
	}
	else {
	  // unable to find method via signature
	  virtual_methods_defined = false;
	}
      }
    }
  
    return virtual_methods_defined;
  }

  /****************************
   * Analyzes virtual method, which
   * are made when compiling shared 
   * libraries.
   ****************************/
  void ContextAnalyzer::AnalyzeVirtualMethod(Class* impl_class, MethodType impl_mthd_type, Type* impl_return, 
					     bool impl_is_static, bool impl_is_virtual, Method* virtual_method) {
    // check method types
    if(impl_mthd_type != virtual_method->GetMethodType()) {
      ProcessError(impl_class, "Not all virtual methods have been defined for class/interface: " +
		   virtual_method->GetClass()->GetName());
    }
    // check method returns
    Type* virtual_return = virtual_method->GetReturn();
    if(impl_return->GetType() != virtual_return->GetType()) {
      ProcessError(impl_class, "Not all virtual methods have been defined for class/interface: " +
		   virtual_method->GetClass()->GetName());
    }
    else if(impl_return->GetType() == CLASS_TYPE &&
	    impl_return->GetClassName() != virtual_return->GetClassName()) {
      Class* impl_cls = SearchProgramClasses(impl_return->GetClassName());
      Class* virtual_cls = SearchProgramClasses(virtual_return->GetClassName());
      if(impl_cls && virtual_cls && impl_cls != virtual_cls) {
	LibraryClass* impl_lib_cls = linker->SearchClassLibraries(impl_return->GetClassName(),
								  program->GetUses());
	LibraryClass* virtual_lib_cls = linker->SearchClassLibraries(virtual_return->GetClassName(),
								     program->GetUses());
	if(impl_lib_cls && virtual_lib_cls && impl_lib_cls != virtual_lib_cls) {
	  ProcessError(impl_class, "Not all virtual methods have been defined for class/interface: " +
		       virtual_method->GetClass()->GetName());
	}
      }
    }
    // check function vs. method
    if(impl_is_static != virtual_method->IsStatic()) {
      ProcessError(impl_class, "Not all virtual methods have been defined for class/interface: " +
		   virtual_method->GetClass()->GetName());
    }
    // check virtual
    if(impl_is_virtual) {
      ProcessError(impl_class, "Implementation method cannot be virtual");
    }
  }

  /****************************
   * Analyzes virtual method, which
   * are made when compiling shared 
   * libraries.
   ****************************/
  bool ContextAnalyzer::AnalyzeVirtualMethods(Class* impl_class, LibraryClass* lib_virtual_class, int depth)
  {
    bool virtual_methods_defined = true;

    // virutal methods
    map<const string, LibraryMethod*>::iterator iter;
    map<const string, LibraryMethod*> lib_virtual_class_methods = lib_virtual_class->GetMethods();
    for(iter = lib_virtual_class_methods.begin(); iter != lib_virtual_class_methods.end(); ++iter) {
      LibraryMethod* virtual_method = iter->second;
      if(virtual_method->IsVirtual()) {
	string virtual_method_name = virtual_method->GetName();
      
	// validate that methods have been implemented
	Method* impl_method = NULL;
	LibraryMethod* lib_impl_method = NULL;
	int offset = (int)virtual_method_name.find_first_of(':');
	if(offset > -1) {
	  string encoded_name = impl_class->GetName() + virtual_method_name.substr(offset);
	  impl_method = impl_class->GetMethod(encoded_name);	
	  if(!impl_method && impl_class->GetParent()) {
	    Class* parent_class = impl_class->GetParent();
	    while(!impl_method && !lib_impl_method && parent_class) {
	      encoded_name = parent_class->GetName() + virtual_method_name.substr(offset);
	      impl_method = parent_class->GetMethod(encoded_name);	    
	      // update	    
	      if(!impl_method && parent_class->GetLibraryParent()) {
		LibraryClass* lib_parent_class = parent_class->GetLibraryParent();
		encoded_name = lib_parent_class->GetName() + virtual_method_name.substr(offset);
		lib_impl_method = lib_parent_class->GetMethod(encoded_name);
		break;
	      }
	      parent_class = parent_class->GetParent();
	    }
	  }
	  else if(impl_class->GetLibraryParent()) {
	    LibraryClass* lib_parent_class = impl_class->GetLibraryParent();
	    encoded_name = lib_parent_class->GetName() + virtual_method_name.substr(offset);
	    lib_impl_method = lib_parent_class->GetMethod(encoded_name);
	  }
	}
      
	// validate method
	if(impl_method) {
	  AnalyzeVirtualMethod(impl_class, impl_method->GetMethodType(), impl_method->GetReturn(), 
			       impl_method->IsStatic(), impl_method->IsVirtual(), virtual_method);
	}
	else if(lib_impl_method) {
	  AnalyzeVirtualMethod(impl_class, lib_impl_method->GetMethodType(), lib_impl_method->GetReturn(), 
			       lib_impl_method->IsStatic(), lib_impl_method->IsVirtual(), virtual_method);
	}
	else {
	  // unable to find method via signature
	  virtual_methods_defined = false;
	} 
      }
    }

    return virtual_methods_defined;
  }

  /****************************
   * Analyzes virtual method, which
   * are made when compiling shared 
   * libraries.
   ****************************/
  void ContextAnalyzer::AnalyzeVirtualMethod(Class* impl_class, MethodType impl_mthd_type, Type* impl_return, 
					     bool impl_is_static, bool impl_is_virtual, LibraryMethod* virtual_method) {
    // check method types
    if(impl_mthd_type != virtual_method->GetMethodType()) {
      ProcessError(impl_class, "Not all virtual methods have been defined for class/interface: " +
		   virtual_method->GetLibraryClass()->GetName());
    }
    // check method returns
    Type* virtual_return = virtual_method->GetReturn();
    if(impl_return->GetType() != virtual_return->GetType()) {
      ProcessError(impl_class, "Not all virtual methods have been defined for class/interface: " +
		   virtual_method->GetLibraryClass()->GetName());
    }
    else if(impl_return->GetType() == CLASS_TYPE &&
	    impl_return->GetClassName() != virtual_return->GetClassName()) {
      Class* impl_cls = SearchProgramClasses(impl_return->GetClassName());
      Class* virtual_cls = SearchProgramClasses(virtual_return->GetClassName());
      if(impl_cls && virtual_cls && impl_cls != virtual_cls) {
	LibraryClass* impl_lib_cls = linker->SearchClassLibraries(impl_return->GetClassName(),
								  program->GetUses());
	LibraryClass* virtual_lib_cls = linker->SearchClassLibraries(virtual_return->GetClassName(),
								     program->GetUses());
	if(impl_lib_cls && virtual_lib_cls && impl_lib_cls != virtual_lib_cls) {
	  ProcessError(impl_class, "Not all virtual methods have been defined for class/interface: " +
		       virtual_method->GetLibraryClass()->GetName());
	}
      }
    }
    // check function vs. method
    if(impl_is_static != virtual_method->IsStatic()) {
      ProcessError(impl_class, "Not all virtual methods have been defined for class/interface: " +
		   virtual_method->GetLibraryClass()->GetName());
    }
    // check virtual
    if(impl_is_virtual) {
      ProcessError(impl_class, "Implementation method cannot be virtual");
    }
  }


  /****************************
   * Analyzes a method
   ****************************/
  void ContextAnalyzer::AnalyzeMethod(Method* method, int id, int depth)
  {
#ifdef _DEBUG
    string msg = "(method: name='" + method->GetName() +
      "; parsed='" + method->GetParsedName() + "')";
    Show(msg, method->GetLineNumber(), depth);
#endif

    method->SetId(id);
    current_method = method;
    current_table = symbol_table->GetSymbolTable(current_method->GetParsedName());
    method->SetSymbolTable(current_table);

    // declarations
    vector<Declaration*> declarations = current_method->GetDeclarations()->GetDeclarations();
    for(size_t i = 0; i < declarations.size(); i++) {
      AnalyzeDeclaration(declarations[i], depth + 1);
    }

    // process statements if function/method is not virtual
    if(!current_method->IsVirtual()) {
      // statements
      vector<Statement*> statements = current_method->GetStatements()->GetStatements();
      for(size_t i = 0; i < statements.size(); i++) {
	AnalyzeStatement(statements[i], depth + 1);
      }
      // check for parent call
      if((current_method->GetMethodType() == NEW_PUBLIC_METHOD ||
	  current_method->GetMethodType() == NEW_PRIVATE_METHOD) &&
	 (current_class->GetParent() || (current_class->GetLibraryParent() &&
					 current_class->GetLibraryParent()->GetName() != SYSTEM_BASE_NAME))) {
	if(statements.size() == 0 || statements.front()->GetStatementType() != METHOD_CALL_STMT) {
	  ProcessError(current_method, "Parent call required");
	}
	else {
	  MethodCall* mthd_call = static_cast<MethodCall*>(statements.front());
	  if(mthd_call->GetCallType() != PARENT_CALL) {
	    ProcessError(current_method, "Parent call required");
	  }
	}
      }

#ifndef _SYSTEM
      // check for return
      if(current_method->GetMethodType() != NEW_PUBLIC_METHOD &&
	 current_method->GetMethodType() != NEW_PRIVATE_METHOD &&
	 current_method->GetReturn()->GetType() != NIL_TYPE) {
	if(statements.size() == 0 || statements.back()->GetStatementType() != RETURN_STMT) {
	  ProcessError(current_method, "Method/function does not return a value");
	}
      }
#endif

      // check program main
      const string main_str = current_class->GetName() + ":Main:o.System.String*,";
      if(current_method->GetEncodedName() ==  main_str) {
	if(main_found) {
	  ProcessError(current_method, "The 'Main(args)' function has already been defined");
	}
	else if(current_method->IsStatic()) {
	  current_class->SetCalled(true);
	  program->SetStart(current_class, current_method);
	  main_found = true;
	}

	if(main_found && (is_lib | is_web)) {
	  ProcessError(current_method, "Libraries and web applications may not define a 'Main(args)' function");
	}
      }
      // web program
      else if(is_web) {
	const string web_str = current_class->GetName() + ":Request:o.FastCgi.Request,o.FastCgi.Response,";
	if(current_method->GetEncodedName() ==  web_str) {	
	  if(web_found) {
	    ProcessError(current_method, "The 'Request(args)' function has already been defined");
	  } 
	  else if(current_method->IsStatic()) {
	    current_class->SetCalled(true);
	    program->SetStart(current_class, current_method);
	    web_found = true;
	  }
	
	  if(web_found && (is_lib | main_found)) {
	    ProcessError(current_method, "Web applications may not be define a 'Main(args)' function or be compiled as a library");
	  }
	}
      }
    }
  }

  /****************************
   * Analyzes a statements
   ****************************/
  void ContextAnalyzer::AnalyzeStatements(StatementList* statement_list, int depth)
  {
    current_table->NewScope();
    vector<Statement*> statements = statement_list->GetStatements();
    for(size_t i = 0; i < statements.size(); i++) {
      AnalyzeStatement(statements[i], depth + 1);
    }
    current_table->PreviousScope();
  }

  /****************************
   * Analyzes a statement
   ****************************/
  void ContextAnalyzer::AnalyzeStatement(Statement* statement, int depth)
  {
    switch(statement->GetStatementType()) {
    case SYSTEM_STMT:
      break;

    case DECLARATION_STMT:
      AnalyzeDeclaration(static_cast<Declaration*>(statement), depth);
      break;

    case METHOD_CALL_STMT:
      AnalyzeMethodCall(static_cast<MethodCall*>(statement), depth);
      AnalyzeCast(static_cast<MethodCall*>(statement), depth + 1);
      break;


    case ADD_ASSIGN_STMT:
    case SUB_ASSIGN_STMT:
    case MUL_ASSIGN_STMT:
    case DIV_ASSIGN_STMT:
      AnalyzeAssignment(static_cast<Assignment*>(statement), depth);      
      break;

    case ASSIGN_STMT:
      AnalyzeAssignment(static_cast<Assignment*>(statement), depth);
      break;

    case SIMPLE_STMT:
      AnalyzeSimpleStatement(static_cast<SimpleStatement*>(statement), depth);
      break;

    case RETURN_STMT:
      AnalyzeReturn(static_cast<Return*>(statement), depth);
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
      if(!in_loop) {
	ProcessError(statement, "Breaks are only allowed in loops.");
      }
      break;

    case SELECT_STMT:
      AnalyzeSelect(static_cast<Select*>(statement), depth);
      break;

    case CRITICAL_STMT:
      AnalyzeCritical(static_cast<CriticalSection*>(statement), depth);
      break;

    default:
      ProcessError(statement, "Undefined statement");
      break;
    }
  }

  /****************************
   * Analyzes an expression
   ****************************/
  void ContextAnalyzer::AnalyzeExpression(Expression* expression, int depth)
  {
    switch(expression->GetExpressionType()) {
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
      break;

    case NIL_LIT_EXPR:
#ifdef _DEBUG
      Show("nil literal", expression->GetLineNumber(), depth);
#endif
      break;

    case BOOLEAN_LIT_EXPR:
#ifdef _DEBUG
      Show("boolean literal", expression->GetLineNumber(), depth);
#endif
      break;

    case CHAR_LIT_EXPR:
#ifdef _DEBUG
      Show("character literal", expression->GetLineNumber(), depth);
#endif
      break;

    case INT_LIT_EXPR:
#ifdef _DEBUG
      Show("integer literal", expression->GetLineNumber(), depth);
#endif
      break;

    case FLOAT_LIT_EXPR:
#ifdef _DEBUG
      Show("float literal", expression->GetLineNumber(), depth);
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
      ProcessError(expression, "Undefined expression");
      break;
    }

    // check expression method call
    AnalyzeExpressionMethodCall(expression, depth + 1);

    // check cast
    AnalyzeCast(expression, depth + 1);
  }

  /****************************
   * Analyzes a ternary
   * conditional
   ****************************/
  void ContextAnalyzer::AnalyzeConditional(Cond* conditional, int depth) {
#ifdef _DEBUG
    Show("conditional expression", conditional->GetLineNumber(), depth);
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
    if(if_type) {
      if(if_type->GetType() == CLASS_TYPE && else_type->GetType() == CLASS_TYPE) {
	AnalyzeClassCast(if_conditional->GetEvalType(), else_conditional, depth + 1);
      }
      else if(if_type->GetType() != else_type->GetType() ||
	      if_type->GetType() == NIL_TYPE ||
	      else_type->GetType() == NIL_TYPE) {
	ProcessError(conditional, "'?' invalid type mismatch");
      }
      // set eval type
      conditional->SetEvalType(if_conditional->GetEvalType(), true);
      current_method->SetAndOr(true);
    }
    else {
      ProcessError(conditional, "Invalid 'if' statement");
    }
  }

  void ContextAnalyzer::AnalyzeCharacterString(CharacterString* char_str, int depth) {
#ifdef _DEBUG
    Show("character string literal", char_str->GetLineNumber(), depth);
#endif
    const string &str = char_str->GetString();
    int id = program->GetCharStringId(str);
    if(id > -1) {
      char_str->SetId(id);
    }
    else {
      char_str->SetId(char_str_index);
      program->AddCharString(str, char_str_index);
      char_str_index++;
    }
    
#ifndef _SYSTEM
    LibraryClass* lib_klass = linker->SearchClassLibraries("System.String", program->GetUses());
    if(lib_klass) {
      lib_klass->SetCalled(true);
    }
    else {
      ProcessError(char_str, "Internal compiler error.");
    }
#endif
  }

  void ContextAnalyzer::AnalyzeStaticArray(StaticArray* array, int depth) {
    // TOOD: support for 3d or 4d initialization
    if(array->GetDimension() > 2) {
      ProcessError(array, "Invalid static array declaration.");
      return;
    }

    if(!array->IsMatchingTypes()) {
      ProcessError(array, "Array element types do not match.");
      return;
    }
   
    if(!array->IsMatchingLenghts()) {
      ProcessError(array, "Array dimension lengths do not match.");
      return;
    }
   
    Type* type = TypeFactory::Instance()->MakeType(array->GetType());  
    type->SetDimension(array->GetDimension());
    if(type->GetType() == CLASS_TYPE) {
      type->SetClassName("System.String");
    }
    array->SetEvalType(type, false);
      
    // ensure that element sizes match dimensions
    vector<Expression*> all_elements = array->GetAllElements()->GetExpressions();
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

    case CHAR_TYPE: {
      // copy string elements
      string str;
      for(size_t i = 0; i < all_elements.size(); i++) {
	str += static_cast<CharacterLiteral*>(all_elements[i])->GetValue();
      }
      // associate char string
      int id = program->GetCharStringId(str);
      if(id > -1) {
	array->SetId(id);
      }
      else {
	array->SetId(char_str_index);
	program->AddCharString(str, char_str_index);
	char_str_index++;
      }
    }
      break;

    case CLASS_TYPE:
      for(size_t i = 0; i < all_elements.size(); i++) {
	AnalyzeCharacterString(static_cast<CharacterString*>(all_elements[i]), depth + 1);
      }
      break;

    default:
      ProcessError(array, "Invalid type for static array.");
      break;
    }
  }

  /****************************
   * Analyzes a variable
   ****************************/
  void ContextAnalyzer::AnalyzeVariable(Variable* variable, int depth)
  {
    // explicitly defined variable
    SymbolEntry* entry = GetEntry(variable->GetName());
    if(entry) {
#ifdef _DEBUG
      string msg = "variable reference: name='" + variable->GetName() + "' local=" +
	(entry->IsLocal() ? "true" : "false");
      Show(msg, variable->GetLineNumber(), depth);
#endif

      const string& name = variable->GetName();
      if(SearchProgramClasses(name) || SearchProgramEnums(name) ||
	 linker->SearchClassLibraries(name, program->GetUses()) ||
	 linker->SearchEnumLibraries(name, program->GetUses())) {
	ProcessError(variable, "Variable name already used to define a class or enum");
      }

      // associate variable and entry
      if(!variable->GetEvalType()) {
	variable->SetTypes(entry->GetType());
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
	else {
	  ProcessError(variable, "Dimension size mismatch or uninitialized type");
	}
      }
      // static check
      if(InvalidStatic(entry)) {
	ProcessError(variable, "Cannot reference an instance variable from this context");
      }
    }
    // dynamic defined variable
    else if(current_method) {
      const string scope_name = current_method->GetName() + ":" + variable->GetName();
      SymbolEntry* entry = TreeFactory::Instance()->MakeSymbolEntry(variable->GetFileName(),
								    variable->GetLineNumber(),
								    scope_name,
								    TypeFactory::Instance()->MakeType(VAR_TYPE),
								    false, true);
      current_table->AddEntry(entry, true);

      // link entry and variable
      variable->SetTypes(entry->GetType());
      variable->SetEntry(entry);
      entry->AddVariable(variable);
    }
    // undefined variable (at class level)
    else {
      ProcessError(variable, "Undefined variable: '" +  variable->GetName() + "'");
    }
  }

  /****************************
   * Analyzes a method call
   ****************************/
  void ContextAnalyzer::AnalyzeMethodCall(MethodCall* method_call, int depth)
  {
#ifdef _DEBUG
    string msg = "method/function call: class=" + method_call->GetVariableName() +
      "; method=" + method_call->GetMethodName() + "; call_type=" +
      ToString(method_call->GetCallType());
    Show(msg, (static_cast<Expression*>(method_call))->GetLineNumber(), depth);
#endif

    // new array call
    if(method_call->GetCallType() == NEW_ARRAY_CALL) {
      AnalyzeNewArrayCall(method_call, depth);
    }
    // enum call
    else if(method_call->GetCallType() == ENUM_CALL) {
      Enum* eenum = SearchProgramEnums(method_call->GetVariableName());
      if(eenum) {
	EnumItem* item = eenum->GetItem(method_call->GetMethodName());
	if(item) {
	  method_call->SetEnumItem(item, eenum->GetName());
	} else {
	  ProcessError(static_cast<Expression*>(method_call), "Undefined enum item: '" +
		       method_call->GetMethodName() + "'");
	}
      } else {
	LibraryEnum* lib_eenum = linker->SearchEnumLibraries(method_call->GetVariableName(),
							     program->GetUses());
	if(lib_eenum) {
	  LibraryEnumItem* lib_item = lib_eenum->GetItem(method_call->GetMethodName());
	  if(lib_item) {
	    method_call->SetLibraryEnumItem(lib_item, lib_eenum->GetName());
	  } else {
	    ProcessError(static_cast<Expression*>(method_call), "Undefined enum item: '" +
			 method_call->GetMethodName() + "'");
	  }
	} else {
	  ProcessError(static_cast<Expression*>(method_call), "Undefined enum: '" +
		       method_call->GetVariableName() + "'");
	}
      }
      AnalyzeExpressionMethodCall(method_call, depth + 1);
    }
    // parent call
    else if(method_call->GetCallType() == PARENT_CALL) {
      AnalyzeParentCall(method_call, depth);
    }
    // method/function
    else { 
      string encoding;
      // local call
      string variable_name = method_call->GetVariableName();
      Class* klass = AnalyzeProgramMethodCall(method_call, encoding, depth);
      if(klass) {
	if(method_call->IsFunctionDefinition()) {
	  AnalyzeFunctionReference(klass, method_call, encoding, depth);
	}
	else {
	  AnalyzeMethodCall(klass, method_call, false, encoding, depth);
	}
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
	return;
      }

      SymbolEntry* entry = GetEntry(method_call, variable_name, depth);
      if(entry) {
	if(method_call->GetVariable()) {
	  bool is_enum_call = false;
	  if(!AnalyzeExpressionMethodCall(method_call->GetVariable(), encoding,
					  klass, lib_klass, is_enum_call)) {
	    ProcessError(static_cast<Expression*>(method_call), "Invalid class type or assignment");
	  }
	} else {
	  if(!AnalyzeExpressionMethodCall(entry, encoding, klass, lib_klass)) {
	    ProcessError(static_cast<Expression*>(method_call), "Invalid class type or assignment");
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
	  ProcessError(static_cast<Expression*>(method_call),
		       "Undefined class: '" + variable_name + "'");
	}
      } 
      else {
	ProcessError(static_cast<Expression*>(method_call),
		     "Undefined class: '" + variable_name + "'");
      }
    }
  }

  /****************************
   * Validates an expression
   * method call
   ****************************/
  bool ContextAnalyzer::AnalyzeExpressionMethodCall(Expression* expression, string &encoding,
						    Class* &klass, LibraryClass* &lib_klass,
						    bool &is_enum_call)
  {
    Type* type;
    // process cast
    if(expression->GetCastType()) {
      if(expression->GetExpressionType() == METHOD_CALL_EXPR && static_cast<MethodCall*>(expression)->GetVariable()) {
	while(expression->GetMethodCall()) {
	  expression = expression->GetMethodCall();
	}
	type = expression->GetEvalType();	
      }
      else {
	type = expression->GetCastType();
      }
    }
    // process non-cast
    else {
      type = expression->GetEvalType();
    }

    if(expression->GetExpressionType() == STAT_ARY_EXPR) {
      ProcessError(expression, "Unable to make method calls on static arrays");
      return false;
    }

    if(type) {
      const int dimension = IsScalar(expression) ? 0 : type->GetDimension();
      return AnalyzeExpressionMethodCall(type, dimension, encoding, klass, lib_klass, is_enum_call);
    }

    return false;
  }

  /****************************
   * Validates an expression
   * method call
   ****************************/
  bool ContextAnalyzer::AnalyzeExpressionMethodCall(SymbolEntry* entry, string &encoding,
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
						    string &encoding, Class* &klass,
						    LibraryClass* &lib_klass, bool &is_enum_call)
  {
    switch(type->GetType()) {
    case BOOLEAN_TYPE:
      klass = program->GetClass(BOOL_CLASS_ID);
      lib_klass = linker->SearchClassLibraries(BOOL_CLASS_ID, program->GetUses());
      encoding = "l";
      break;

    case VAR_TYPE:
    case NIL_TYPE:
      return false;

    case BYTE_TYPE:
      klass = program->GetClass(BYTE_CLASS_ID);
      lib_klass = linker->SearchClassLibraries(BYTE_CLASS_ID, program->GetUses());
      encoding = "b";
      break;

    case CHAR_TYPE:
      klass = program->GetClass(CHAR_CLASS_ID);
      lib_klass = linker->SearchClassLibraries(CHAR_CLASS_ID, program->GetUses());
      encoding = "c";
      break;

    case INT_TYPE:
      klass = program->GetClass(INT_CLASS_ID);
      lib_klass = linker->SearchClassLibraries(INT_CLASS_ID, program->GetUses());
      encoding = "i";
      break;

    case FLOAT_TYPE:
      klass = program->GetClass(FLOAT_CLASS_ID);
      lib_klass = linker->SearchClassLibraries(FLOAT_CLASS_ID, program->GetUses());
      encoding = "f";
      break;

    case CLASS_TYPE: {
      if(dimension > 0 && type->GetDimension() > 0) {
	klass = program->GetClass(BASE_ARRAY_CLASS_ID);
	lib_klass = linker->SearchClassLibraries(BASE_ARRAY_CLASS_ID, program->GetUses());
	encoding = "o.System.Base";
      } 
      else {
	const string &cls_name = type->GetClassName();
	klass = SearchProgramClasses(cls_name);
	lib_klass = linker->SearchClassLibraries(cls_name, program->GetUses());

	if(!klass && !lib_klass) {
	  if(SearchProgramEnums(cls_name) || linker->SearchEnumLibraries(cls_name, program->GetUses())) {
	    klass = program->GetClass(INT_CLASS_ID);
	    lib_klass = linker->SearchClassLibraries(INT_CLASS_ID, program->GetUses());
	    encoding = "i,";
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
    for(int i = 0; i < dimension; i++) {
      encoding += '*';
    }

    if(type->GetType() != CLASS_TYPE) {
      encoding += ",";
    }

    return true;
  }

  /****************************
   * Analyzes a new array method
   * call
   ****************************/
  void ContextAnalyzer::AnalyzeNewArrayCall(MethodCall* method_call, int depth)
  {
    // get parameters
    ExpressionList* call_params = method_call->GetCallingParameters();
    AnalyzeExpressions(call_params, depth + 1);
    // check indexes
    vector<Expression*> expressions = call_params->GetExpressions();
    if(expressions.size() == 0) {
      ProcessError(static_cast<Expression*>(method_call),
		   "Empty array index");
    }
    // validate array parameters
    for(size_t i = 0; i < expressions.size(); i++) {
      Type* eval_type = expressions[i]->GetEvalType();
      if(eval_type) {
	switch(eval_type->GetType()) {
	case BYTE_TYPE:
	case CHAR_TYPE:
	case INT_TYPE:
	  break;

	default:
	  ProcessError(expressions[i], "Invalid array index type");
	  break;
	}
      }
    }
  }

  /*********************************
   * Analyzes a parent method call
   *********************************/
  void ContextAnalyzer::AnalyzeParentCall(MethodCall* method_call, int depth)
  {
    // get parameters
    ExpressionList* call_params = method_call->GetCallingParameters();
    AnalyzeExpressions(call_params, depth + 1);
  
    Class* parent = current_class->GetParent();
    if(parent) {
      string encoding;
      AnalyzeMethodCall(parent, method_call, false, encoding, depth);
    } 
    else {
      LibraryClass* lib_parent = current_class->GetLibraryParent();
      if(lib_parent) {
	string encoding;
	AnalyzeMethodCall(lib_parent, method_call, false, encoding, true, depth);
      } 
      else {
	ProcessError(static_cast<Expression*>(method_call), "Class has no parent");
      }
    }
  }

  /****************************
   * Analyzes a method call
   ****************************/
  void ContextAnalyzer::AnalyzeExpressionMethodCall(Expression* expression, int depth)
  {
    MethodCall* method_call = expression->GetMethodCall();
    if(method_call) {
      string encoding;
      Class* klass = NULL;
      LibraryClass* lib_klass = NULL;

      // TODO: fix need to change intermediate emit code so that element value
      // is loaded instead of instance
      if(expression->GetEvalType() && expression->GetEvalType()->GetDimension() > 0) {
	//	ProcessError(expression, "Method call from element cast not allowed");
	AnalyzeClassCast(expression->GetCastType(), expression, depth + 1);
      }

      // check expression class
      bool is_enum_call = false;
      if(!AnalyzeExpressionMethodCall(expression, encoding, klass, lib_klass, is_enum_call)) {
	ProcessError(static_cast<Expression*>(method_call), "Invalid class type or assignment");
      }
      method_call->SetEnumCall(is_enum_call);
      // check methods
      if(klass) {
	AnalyzeMethodCall(klass, method_call, true, encoding, depth);
      } else if(lib_klass) {
	AnalyzeMethodCall(lib_klass, method_call, true, encoding, false, depth);
      } else {
	ProcessError(static_cast<Expression*>(method_call), "Undefined class");
      }
    }
  }

  /*********************************
   * Analyzes a method call.  This
   * is method call within the source
   * program.
   *********************************/
  Class* ContextAnalyzer::AnalyzeProgramMethodCall(MethodCall* method_call, string &encoding, int depth)
  {
    Class* klass = NULL;

    // method within the same class
    string variable_name = method_call->GetVariableName();
    if(method_call->GetMethodName().size() == 0) {
      klass = SearchProgramClasses(current_class->GetName());
    } else {
      // external method
      SymbolEntry* entry = GetEntry(method_call, variable_name, depth);
      if(entry && entry->GetType() && entry->GetType()->GetType() == CLASS_TYPE) {
	if(entry->GetType()->GetDimension() > 0 &&
	   (!method_call->GetVariable() ||
	    !method_call->GetVariable()->GetIndices())) {
	  klass = program->GetClass(BASE_ARRAY_CLASS_ID);
	  encoding = "o.System.Base*,";
	} else {
	  klass = SearchProgramClasses(entry->GetType()->GetClassName());
	}
      }
      // static method call
      if(!klass) {
	klass = SearchProgramClasses(variable_name);
      }
    }

    return klass;
  }

  /*********************************
   * Analyzes a method call.  This
   * is method call within a linked
   * library
   *********************************/
  LibraryClass* ContextAnalyzer::AnalyzeLibraryMethodCall(MethodCall* method_call, string &encoding, int depth)
  {
    LibraryClass* klass = NULL;
    string variable_name = method_call->GetVariableName();

    // external method
    SymbolEntry* entry = GetEntry(method_call, variable_name, depth);
    if(entry && entry->GetType() && entry->GetType()->GetType() == CLASS_TYPE) {
      // array type
      if(entry->GetType()->GetDimension() > 0 &&
	 (!method_call->GetVariable() ||
	  !method_call->GetVariable()->GetIndices())) {
	
	klass = linker->SearchClassLibraries(BASE_ARRAY_CLASS_ID, program->GetUses());
	encoding = "o.System.Base*,";
      } 
      // cast type
      else if(method_call->GetVariable() && method_call->GetVariable()->GetCastType() && 
	      method_call->GetVariable()->GetCastType()->GetType() == CLASS_TYPE) {
	klass = linker->SearchClassLibraries(method_call->GetVariable()->GetCastType()->GetClassName(), program->GetUses());	
	method_call->SetTypes(entry->GetType());
	AnalyzeClassCast(method_call->GetVariable()->GetCastType(), method_call, depth + 1);
      }
      // base type
      else {
	klass = linker->SearchClassLibraries(entry->GetType()->GetClassName(), program->GetUses());
      }
    }
    // static method call
    if(!klass) {
      klass = linker->SearchClassLibraries(variable_name, program->GetUses());
    }

    return klass;
  }
  
  /*********************************
   * Resolve method call parameter
   *********************************/
  int ContextAnalyzer::MatchCallingParameter(Expression* calling_param, Type* method_type,
					     Class* klass, LibraryClass* lib_klass, int depth) {
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
	if(method_type->GetDimension() == 0)  {
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
		return 0;
	      }	      
	      // calculate relative match
	      const string &from_klass_name = calling_type->GetClassName();
	      Class* from_klass = SearchProgramClasses(from_klass_name);
	      LibraryClass* from_lib_klass = linker->SearchClassLibraries(from_klass_name, 
									  program->GetUses());	      
	      Class* to_klass = SearchProgramClasses(method_type->GetClassName());
	      LibraryClass* to_lib_klass = linker->SearchClassLibraries(method_type->GetClassName(), 
									program->GetUses());
	      if(to_klass) {
		return ValidDownCast(to_klass->GetName(), from_klass, from_lib_klass) ? 1 : -1;
	      }
	      else if(to_lib_klass) {
		return ValidDownCast(to_lib_klass->GetName(), from_klass, from_lib_klass) ? 1 : -1;
	      }
	    }
	    
	    return -1;
	  }
	    
	  case FUNC_TYPE: {
	    const string &calling_type_name = "m." + calling_type->GetClassName();
	    string method_type_name = method_type->GetClassName();
	    if(method_type_name.size() == 0) {
	      method_type_name = "m." + EncodeFunctionType(method_type->GetFunctionParameters(),
							   method_type->GetFunctionReturn());
	      method_type->SetClassName(method_type_name);
	    }
	    
	    return calling_type_name == method_type_name ? 0 : -1;
	  }
	    
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
  Method* ContextAnalyzer::ResolveMethodCall(Class* klass, MethodCall* method_call, int depth) {
    const string &method_name = method_call->GetMethodName(); 				 
    ExpressionList* calling_params = method_call->GetCallingParameters();
    vector<Expression*> expr_params = calling_params->GetExpressions();
    vector<Method*> candidates = klass->GetAllUnqualifiedMethods(method_name);
    
    // save all valid candidates
    vector<MethodCallSelection*> matches;
    for(size_t i = 0; i < candidates.size(); i++) {
      // match parameter sizes
      vector<Type*> parms_types;
      vector<Declaration*> method_parms = candidates[i]->GetDeclarations()->GetDeclarations();
      if(expr_params.size() == method_parms.size()) {
	MethodCallSelection* match = new MethodCallSelection(candidates[i]);
	for(size_t j = 0; j < expr_params.size(); j++) {	  
	  // get method type
	  Type* method_type = NULL;
	  if(method_parms[j]->GetEntry() && method_parms[j]->GetEntry()->GetType()) {
	    method_type = method_parms[j]->GetEntry()->GetType();
	  }
	  // add poarameter match
	  match->AddParameterMatch(MatchCallingParameter(expr_params[j], method_type, klass, NULL, depth));
	}
	matches.push_back(match);
      }
    }
    
    // evaluate matches
    MethodCallSelector selector(method_call, matches);
    return selector.GetSelection();
  }
  
  /****************************
   * Analyzes a method call.  This
   * is method call within the source
   * program.
   ****************************/
  void ContextAnalyzer::AnalyzeMethodCall(Class* klass, MethodCall* method_call,
					  bool is_expr, string &encoding, int depth)
  {   
#ifdef _DEBUG
    cout << "Checking program class call: |" << klass->GetName() << ":" 
	 << (method_call->GetMethodName().size() > 0 ? 
	     method_call->GetMethodName() : method_call->GetVariableName())
	 << "|" << endl;
#endif
    
    // calling parameters
    ExpressionList* call_params = method_call->GetCallingParameters();
    AnalyzeExpressions(call_params, depth + 1);
    
    Method* method = ResolveMethodCall(klass, method_call, depth);
    
    // note: find system based methods and call with function parameters (i.e. $Int, $Float)
    if(!method) {
      const string encoded_name = klass->GetName() + ":" + method_call->GetMethodName() + ":" + encoding +
	EncodeMethodCall(method_call->GetCallingParameters(), depth);
      method = klass->GetMethod(encoded_name);
    }
    
    if(!method) {
      if(klass->GetParent()) {
	Class* parent = klass->GetParent();
	method_call->SetOriginalClass(klass);
	string encoding;
	AnalyzeMethodCall(parent, method_call, is_expr, encoding, depth + 1);
	return;
      }
      else if(klass->GetLibraryParent()) {
	LibraryClass* lib_parent = klass->GetLibraryParent();
	method_call->SetOriginalClass(klass);
	string encoding;
	AnalyzeMethodCall(lib_parent, method_call, is_expr, encoding, true, depth + 1);
	return;
      }
      else { 
	AnalyzeDynamicFunctionCall(method_call, depth + 1);
	return;
      }
    }
    
    // found program method
    if(method) {
      // look for implicit casts
      vector<Declaration*> mthd_params = method->GetDeclarations()->GetDeclarations();
      ExpressionList* call_params = method_call->GetCallingParameters();
      vector<Expression*> expressions = call_params->GetExpressions();
      
#ifndef _SYSTEM
      if(mthd_params.size() != expressions.size()) {
	ProcessError(static_cast<Expression*>(method_call), "Invalid method call context");
	return;
      }
#endif
      
      Expression* expression;
      for(size_t i = 0; i < expressions.size(); i++) {
	expression = expressions[i];
	// find eval type
	while(expression->GetMethodCall()) {
	  AnalyzeExpressionMethodCall(expression, depth + 1);
	  expression = expression->GetMethodCall();
	}
	// check cast
	if(mthd_params[i]->GetEntry()) {
	  AnalyzeRightCast(mthd_params[i]->GetEntry()->GetType(), expression->GetEvalType(), 
			   expression, IsScalar(expression), depth + 1);	
	}
      }
      
      // public/private check
      if(method->GetClass() != current_method->GetClass() && !method->IsStatic() &&
	 (method->GetMethodType() == PRIVATE_METHOD || method->GetMethodType() == NEW_PRIVATE_METHOD)) {
	bool found = false;
	Class* parent = current_method->GetClass()->GetParent();
	while(parent && !found) {
	  if(method->GetClass() == parent) {
	    found = true;
	  }
	  // update
	  parent = parent->GetParent();
	}
      
	if(!found) {
	  ProcessError(static_cast<Expression*>(method_call),
		       "Cannot reference a private method from this context");
	}
      }
      // static check
      if(!is_expr && InvalidStatic(method_call, method)) {
	ProcessError(static_cast<Expression*>(method_call),
		     "Cannot reference an instance method from this context");
      }
      // cannot create an instance of a virutal class
      if((method->GetMethodType() == NEW_PUBLIC_METHOD ||
	  method->GetMethodType() == NEW_PRIVATE_METHOD) &&
	 klass->IsVirtual() && current_class->GetParent() != klass) {
	ProcessError(static_cast<Expression*>(method_call),
		     "Cannot create an instance of a virutal class");
      }
      // associate method
      klass->SetCalled(true);
      method_call->SetOriginalClass(klass);
      method_call->SetMethod(method);
      if(method_call->GetMethodCall()) {
	method_call->GetMethodCall()->SetEvalType(method->GetReturn(), false);
      }
      // next call
      AnalyzeExpressionMethodCall(method_call, depth + 1);
    }
    else {
      const string &mthd_name = method_call->GetMethodName();
      const string &var_name = method_call->GetVariableName();

      if(mthd_name.size() > 0) {
	ProcessError(static_cast<Expression*>(method_call), "Undefined function/method call: '" +
		     mthd_name + "(..)'\n\tEnsure the object and it's calling parameters are properly casted");
      }
      else {
	ProcessError(static_cast<Expression*>(method_call), "Undefined function/method call: '" +
		     var_name + "(..)'\n\tEnsure the object and it's calling parameters are properly casted");
      }
    }
  }

  /****************************
   * Resolves library method calls
   ****************************/
  LibraryMethod* ContextAnalyzer::ResolveMethodCall(LibraryClass* klass, MethodCall* method_call, int depth) {
    const string &method_name = method_call->GetMethodName(); 				 
    ExpressionList* calling_params = method_call->GetCallingParameters();
    vector<Expression*> expr_params = calling_params->GetExpressions();
    vector<LibraryMethod*> candidates = klass->GetUnqualifiedMethods(method_name);
    
    // save all valid candidates
    vector<LibraryMethodCallSelection*> matches;
    for(size_t i = 0; i < candidates.size(); i++) {
      // match parameter sizes
      vector<Type*> method_parms = candidates[i]->GetDeclarationTypes();      
      if(expr_params.size() == method_parms.size()) {
	LibraryMethodCallSelection* match = new LibraryMethodCallSelection(candidates[i]);
	for(size_t j = 0; j < expr_params.size(); j++) {
	  match->AddParameterMatch(MatchCallingParameter(expr_params[j], method_parms[j], 
							 NULL, klass, depth));
	}
	matches.push_back(match);
      }
    }
    
    // evaluate matches
    LibraryMethodCallSelector selector(method_call, matches);
    return selector.GetSelection();
  }
  
  /****************************
   * Analyzes a method call.  This
   * is method call within a linked
   * library
   ****************************/
  void ContextAnalyzer::AnalyzeMethodCall(LibraryClass* klass, MethodCall* method_call,
					  bool is_expr, string &encoding, bool is_parent, int depth)
  {      
#ifdef _DEBUG
    cout << "Checking library encoded name: |" << klass->GetName() << ":" 
	 << method_call->GetMethodName() << "|" << endl;
#endif
    
    ExpressionList* call_params = method_call->GetCallingParameters();
    AnalyzeExpressions(call_params, depth + 1);
    LibraryMethod* lib_method = ResolveMethodCall(klass, method_call, depth);
    if(!lib_method) {  
      LibraryClass* parent = linker->SearchClassLibraries(klass->GetParentName(), program->GetUses());
      while(!lib_method && parent) {
	lib_method = ResolveMethodCall(parent, method_call, depth);
	parent = linker->SearchClassLibraries(parent->GetParentName(), program->GetUses());
      }
    }
    
    // note: last resort to find system based methods i.e. $Int, $Float, etc.
    if(!lib_method) {
      string encoded_name = klass->GetName() + ":" + method_call->GetMethodName() + ":" +
	encoding + EncodeMethodCall(method_call->GetCallingParameters(), depth);
      lib_method = klass->GetMethod(encoded_name);
    }

    method_call->SetOriginalLibraryClass(klass);
    AnalyzeMethodCall(lib_method, method_call, klass->IsVirtual() && !is_parent, is_expr, depth);
  }

  /****************************
   * Analyzes a method call.  This
   * is method call within a linked
   * library
   ****************************/
  void ContextAnalyzer::AnalyzeMethodCall(LibraryMethod* lib_method, MethodCall* method_call,
					  bool is_virtual, bool is_expr, int depth)
  {
    if(lib_method) {
      // public/private check
      if((lib_method->GetMethodType() == PRIVATE_METHOD || 
	  lib_method->GetMethodType() == NEW_PRIVATE_METHOD) &&
	 !lib_method->IsStatic()) {
	ProcessError(static_cast<Expression*>(method_call),
		     "Cannot reference a private method from this context");
      }
      // static check
      if(!is_expr && InvalidStatic(method_call, lib_method)) {
	ProcessError(static_cast<Expression*>(method_call),
		     "Cannot reference an instance method from this context");
      }
      // cannot create an instance of a virutal class
      if((lib_method->GetMethodType() == NEW_PUBLIC_METHOD ||
	  lib_method->GetMethodType() == NEW_PRIVATE_METHOD) && is_virtual) {
	ProcessError(static_cast<Expression*>(method_call),
		     "Cannot create an instance of a virutal class");
      }
      // associate method
      lib_method->GetLibraryClass()->SetCalled(true);
      method_call->SetLibraryMethod(lib_method);
      if(method_call->GetMethodCall()) {
	method_call->GetMethodCall()->SetEvalType(lib_method->GetReturn(), false);
      }
      // next call
      AnalyzeExpressionMethodCall(method_call, depth + 1);
    }
    else {
      AnalyzeDynamicFunctionCall(method_call, depth + 1);
    }
  }

  /********************************
   * Analyzes a dynamic function 
   * call
   ********************************/
  void ContextAnalyzer::AnalyzeDynamicFunctionCall(MethodCall* method_call, int depth) {
    // dynamic function call that is not bound to a class/function until runtime
    SymbolEntry* entry = GetEntry(method_call->GetMethodName());
    if(entry && entry->GetType() && entry->GetType()->GetType() == FUNC_TYPE) {
      // generate parameter strings
      Type* type = entry->GetType();
      string dyn_func_params = type->GetClassName();
      if(dyn_func_params.size() == 0) {
	vector<Type*>& func_params = type->GetFunctionParameters();
	for(size_t i = 0; i < func_params.size(); i++) {
	  dyn_func_params += EncodeType(func_params[i]);
	  // encode dimension
	  for(int i = 0; i < type->GetDimension(); i++) {
	    dyn_func_params += '*';
	  }
	  dyn_func_params += ',';
	}
      }
      else {
	// TODO: hackish!
	int start = dyn_func_params.find('(');
	int end = dyn_func_params.find(')', start + 1);
	if(start != (int)string::npos && end != (int)string::npos) {
	  dyn_func_params = dyn_func_params.substr(start + 1, end - start - 1);
	}
      }
      type->SetFunctionParameterCount(method_call->GetCallingParameters()->GetExpressions().size());
	
      // check parameters again dynamic definition
      const string call_params = EncodeMethodCall(method_call->GetCallingParameters(), depth);
      if(dyn_func_params != call_params) {
	ProcessError(static_cast<Expression*>(method_call),
		     "Undefined function/method call: '" + method_call->GetMethodName() +
		     "(..)'\n\tEnsure the object and it's calling parameters are properly casted");
      }
	
      //  set entry reference and return type
      method_call->SetDynamicFunctionCall(entry);
      method_call->SetEvalType(type->GetFunctionReturn(), true);
      if(method_call->GetMethodCall()) {
	method_call->GetMethodCall()->SetEvalType(type->GetFunctionReturn(), false);
      }

      // next call
      AnalyzeExpressionMethodCall(method_call, depth + 1);
    }
    else {
      const string &mthd_name = method_call->GetMethodName();
      const string &var_name = method_call->GetVariableName();

      if(mthd_name.size() > 0) {
	ProcessError(static_cast<Expression*>(method_call),
		     "Undefined function/method call: '" + mthd_name +
		     "(..)'\n\tEnsure the object and it's calling parameters are properly casted");
      }
      else {
	ProcessError(static_cast<Expression*>(method_call),
		     "Undefined function/method call: '" + var_name +
		     "(..)'\n\tEnsure the object and it's calling parameters are properly casted");
      }
    }
  }

  /********************************
   * Analyzes a function reference
   ********************************/
  void ContextAnalyzer::AnalyzeFunctionReference(Class* klass, MethodCall* method_call,
						 string &encoding, int depth) {
    const string func_encoding = EncodeFunctionReference(method_call->GetCallingParameters(), depth);;
    const string encoded_name = klass->GetName() + ":" + method_call->GetMethodName() +
      ":" + encoding + func_encoding;

    Method* method = klass->GetMethod(encoded_name);
    if(method) {
      const string func_type_id = "m.(" + func_encoding + ")~" + method->GetEncodedReturn();
      Type* type = TypeFactory::Instance()->MakeType(FUNC_TYPE, func_type_id);
      type->SetFunctionParameterCount(method_call->GetCallingParameters()->GetExpressions().size());
      type->SetFunctionReturn(method->GetReturn());
      method_call->SetEvalType(type, true);

      if(!method->IsStatic()) {
	ProcessError(static_cast<Expression*>(method_call), "References to methods are not allowed, only functions");
      }

      if(method->IsVirtual()) {
	ProcessError(static_cast<Expression*>(method_call), "References to methods cannot be virtual");
      }

      // check return type
      Type* rtrn_type = method_call->GetFunctionReturn();
      if(rtrn_type->GetType() != method->GetReturn()->GetType()) {
	ProcessError(static_cast<Expression*>(method_call), "Mismatch function return types");
      }
      else if(rtrn_type->GetType() == CLASS_TYPE) {
	if(ResolveClassEnumType(rtrn_type)) {
	  const string rtrn_encoded_name = "o."+ rtrn_type->GetClassName();
	  if(rtrn_encoded_name != method->GetEncodedReturn()) {
	    ProcessError(static_cast<Expression*>(method_call), "Mismatch function return types");
	  }
	}
	else {
	  ProcessError(static_cast<Expression*>(method_call),
		       "Undefined class or enum: '" + rtrn_type->GetClassName() + "'");
	}
      }
      method->GetClass()->SetCalled(true);
      method_call->SetOriginalClass(klass);
      method_call->SetMethod(method, false);
    }
    else {
      const string &mthd_name = method_call->GetMethodName();
      const string &var_name = method_call->GetVariableName();

      if(mthd_name.size() > 0) {
	ProcessError(static_cast<Expression*>(method_call), "Undefined function/method call: '" +
		     mthd_name + "(..)'\n\tEnsure the object and it's calling parameters are properly casted");
      }
      else {
	ProcessError(static_cast<Expression*>(method_call), "Undefined function/method call: '" +
		     var_name + "(..)'\n\tEnsure the object and it's calling parameters are properly casted");
      }
    }
  }

  /****************************
   * Checks a function reference
   ****************************/
  void ContextAnalyzer::AnalyzeFunctionReference(LibraryClass* klass, MethodCall* method_call,
						 string &encoding, int depth) {
    const string func_encoding = EncodeFunctionReference(method_call->GetCallingParameters(), depth);;
    const string encoded_name = klass->GetName() + ":" + method_call->GetMethodName() +
      ":" + encoding + func_encoding;

    LibraryMethod* method = klass->GetMethod(encoded_name);
    if(method) {
      const string func_type_id = '(' + func_encoding + ")~" + method->GetEncodedReturn();
      Type* type = TypeFactory::Instance()->MakeType(FUNC_TYPE, func_type_id);
      type->SetFunctionParameterCount(method_call->GetCallingParameters()->GetExpressions().size());
      type->SetFunctionReturn(method->GetReturn());
      method_call->SetEvalType(type, true);

      if(!method->IsStatic()) {
	ProcessError(static_cast<Expression*>(method_call), "References to methods are not allowed, only functions");
      }

      if(method->IsVirtual()) {
	ProcessError(static_cast<Expression*>(method_call), "References to methods cannot be virtual");
      }

      // check return type
      Type* rtrn_type = method_call->GetFunctionReturn();
      if(rtrn_type->GetType() != method->GetReturn()->GetType()) {
	ProcessError(static_cast<Expression*>(method_call), "Mismatch function return types");
      }
      else if(rtrn_type->GetType() == CLASS_TYPE) {
	if(ResolveClassEnumType(rtrn_type)) {
	  const string rtrn_encoded_name = "o."+ rtrn_type->GetClassName();
	  if(rtrn_encoded_name != method->GetEncodedReturn()) {
	    ProcessError(static_cast<Expression*>(method_call), "Mismatch function return types");
	  }
	}
	else {
	  ProcessError(static_cast<Expression*>(method_call),
		       "Undefined class or enum: '" + rtrn_type->GetClassName() + "'");
	}
      }
      method->GetLibraryClass()->SetCalled(true);
      method_call->SetOriginalLibraryClass(klass);
      method_call->SetLibraryMethod(method, false);
    }
    else {
      const string &mthd_name = method_call->GetMethodName();
      const string &var_name = method_call->GetVariableName();

      if(mthd_name.size() > 0) {
	ProcessError(static_cast<Expression*>(method_call), "Undefined function/method call: '" +
		     mthd_name + "(..)'\n\tEnsure the object and it's calling parameters are properly casted");
      }
      else {
	ProcessError(static_cast<Expression*>(method_call), "Undefined function/method call: '" +
		     var_name + "(..)'\n\tEnsure the object and it's calling parameters are properly casted");
      }
    }
  }

  /****************************
   * Analyzes a cast
   ****************************/
  void ContextAnalyzer::AnalyzeCast(Expression* expression, int depth)
  {
    // type cast
    if(expression->GetCastType()) {
      // get cast and root types
      Type* cast_type = expression->GetCastType();
      Type* root_type = expression->GetBaseType();
      if(!root_type) {
	root_type = expression->GetEvalType();
      }
      
      // cannot cast across different dimensions
      if(root_type && expression->GetExpressionType() == VAR_EXPR && 
	 !static_cast<Variable*>(expression)->GetIndices() &&
	 cast_type->GetDimension() != root_type->GetDimension()) {
	ProcessError(expression, "Dimension size mismatch");
      }
      
      // casts for variables are handled elsewhere
      if(expression->GetExpressionType() == METHOD_CALL_EXPR && !static_cast<MethodCall*>(expression)->GetVariable()) {
	AnalyzeRightCast(cast_type, root_type, expression, IsScalar(expression), depth + 1);
      }
    }
    // typeof check
    else if(expression->GetTypeOf()) {
      if(expression->GetTypeOf()->GetType() != CLASS_TYPE ||
	 expression->GetEvalType()->GetType() != CLASS_TYPE) {
	ProcessError(expression, "Invalid 'TypeOf' check, only complex classes are supported");
      }

      Type* type_of = expression->GetTypeOf();
      if(SearchProgramClasses(type_of->GetClassName())) {
	Class* klass = SearchProgramClasses(type_of->GetClassName());
	klass->SetCalled(true);
	type_of->SetClassName(klass->GetName());
      }
      else if(linker->SearchClassLibraries(type_of->GetClassName(), program->GetUses())) {
	LibraryClass* lib_klass = linker->SearchClassLibraries(type_of->GetClassName(), program->GetUses());
	lib_klass->SetCalled(true);
	type_of->SetClassName(lib_klass->GetName());
      }
      else {
	ProcessError(expression, "Invalid 'TypeOf' check, unknown class '" + type_of->GetClassName() + "'");
      }
      expression->SetEvalType(TypeFactory::Instance()->MakeType(BOOLEAN_TYPE), true);
    }
  }

  /****************************
   * Analyzes array indices
   ****************************/
  void ContextAnalyzer::AnalyzeIndices(ExpressionList* indices, int depth)
  {
    AnalyzeExpressions(indices, depth + 1);

    vector<Expression*> expressions = indices->GetExpressions();
    for(size_t i = 0; i < expressions.size(); i++) {
      Expression* expression = expressions[i];
      AnalyzeExpression(expression, depth + 1);
      Type* eval_type = expression->GetEvalType();
      if(eval_type) {
	switch(eval_type->GetType()) {
	case BYTE_TYPE:
	case CHAR_TYPE:
	case INT_TYPE:
	  break;

	default:
	  ProcessError(expression, "Expected Byte, Char or Int class");
	  break;
	}
      }
    }
  }

  /****************************
   * Analyzes a simple statement
   ****************************/
  void ContextAnalyzer::AnalyzeSimpleStatement(SimpleStatement* simple, int depth)
  {
    Expression* expression = simple->GetExpression();
    AnalyzeExpression(expression, depth + 1);
    AnalyzeExpressionMethodCall(expression, depth);

    // ensure it's a valid statement
    if(!expression->GetMethodCall()) {
      ProcessError(expression, "Invalid statement");
    }
  }

  /****************************
   * Analyzes a 'if' statement
   ****************************/
  void ContextAnalyzer::AnalyzeIf(If* if_stmt, int depth)
  {
#ifdef _DEBUG
    Show("if/else-if/else", if_stmt->GetLineNumber(), depth);
#endif

    // expression
    Expression* expression = if_stmt->GetExpression();
    AnalyzeExpression(expression, depth + 1);
    if(!IsBooleanExpression(expression)) {
      ProcessError(expression, "Expected Bool expression");
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
  void ContextAnalyzer::AnalyzeSelect(Select* select_stmt, int depth)
  {
    // expression
    Expression* expression = select_stmt->GetExpression();
    AnalyzeExpression(expression, depth + 1);
    if(!IsIntegerExpression(expression)) {
      ProcessError(expression, "Expected integer expression");
    }
    // labels and expressions
    map<ExpressionList*, StatementList*> statements = select_stmt->GetStatements();
    if(statements.size() == 0) {
      ProcessError(expression, "Select statement does not have labels");
    }

    map<ExpressionList*, StatementList*>::iterator iter;
    // duplicate value vector
    int value = 0;
    map<int, StatementList*> label_statements;
    for(iter = statements.begin(); iter != statements.end(); ++iter) {
      // expressions
      ExpressionList* expressions = iter->first;
      AnalyzeExpressions(expressions, depth + 1);
      // check expression type
      vector<Expression*> expression_list = expressions->GetExpressions();
      for(size_t i = 0; i < expression_list.size(); i++) {
	Expression* expression = expression_list[i];
	switch(expression->GetExpressionType()) {
	case CHAR_LIT_EXPR:
	  value = static_cast<CharacterLiteral*>(expression)->GetValue();
	  if(DuplicateCaseItem(label_statements, value)) {
	    ProcessError(expression, "Duplicate select value");
	  }
	  break;

	case INT_LIT_EXPR:
	  value = static_cast<IntegerLiteral*>(expression)->GetValue();
	  if(DuplicateCaseItem(label_statements, value)) {
	    ProcessError(expression, "Duplicate select value");
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
	      ProcessError(expression, "Duplicate select value");
	    }
	  }
	  else if(mthd_call->GetLibraryEnumItem()) {
	    value = mthd_call->GetLibraryEnumItem()->GetId();
	    if(DuplicateCaseItem(label_statements, value)) {
	      ProcessError(expression, "Duplicate select value");
	    }
	  }
	  else {
	    ProcessError(expression, "Expected integer literal or enum item");
	  }
	}
	  break;

	default:
	  ProcessError(expression, "Expected integer literal or enum item");
	  break;
	}
	// statements get assoicated here and validated below
	label_statements.insert(pair<int, StatementList*>(value, iter->second));
      }
    }
    select_stmt->SetLabelStatements(label_statements);
    
    // process statements (in parse order)
    vector<StatementList*> statement_lists = select_stmt->GetStatementLists();
    for(size_t i = 0; i < statement_lists.size(); i++) {
      AnalyzeStatements(statement_lists[i], depth + 1);
    }
  }

  /****************************
   * Analyzes a 'for' statement
   ****************************/
  void ContextAnalyzer::AnalyzeCritical(CriticalSection* mutex, int depth)
  {
    Variable* variable = mutex->GetVariable();
    AnalyzeVariable(variable, depth + 1);
    if(variable->GetEvalType() && variable->GetEvalType()->GetType() == CLASS_TYPE) {
      if(variable->GetEvalType()->GetClassName() != "System.Concurrency.ThreadMutex") {
	ProcessError(mutex, "Expected ThreadMutex type");
      }
    }
    else {
      ProcessError(mutex, "Expected ThreadMutex type");
    }
    AnalyzeStatements(mutex->GetStatements(), depth + 1);
  }

  /****************************
   * Analyzes a 'for' statement
   ****************************/
  void ContextAnalyzer::AnalyzeFor(For* for_stmt, int depth)
  {
    current_table->NewScope();
    // pre
    AnalyzeStatement(for_stmt->GetPreStatement(), depth + 1);
    // expression
    Expression* expression = for_stmt->GetExpression();
    AnalyzeExpression(expression, depth + 1);
    if(!IsBooleanExpression(expression)) {
      ProcessError(expression, "Expected Bool expression");
    }
    // update
    AnalyzeStatement(for_stmt->GetUpdateStatement(), depth + 1);
    // statements
    in_loop = true;
    AnalyzeStatements(for_stmt->GetStatements(), depth + 1);
    in_loop = false;
    current_table->PreviousScope();
  }

  /****************************
   * Analyzes a 'do/while' statement
   ****************************/
  void ContextAnalyzer::AnalyzeDoWhile(DoWhile* do_while_stmt, int depth)
  {
#ifdef _DEBUG
    Show("do/while", do_while_stmt->GetLineNumber(), depth);
#endif
    
    // 'do/while' statements
    current_table->NewScope();
    in_loop = true;
    vector<Statement*> statements = do_while_stmt->GetStatements()->GetStatements();
    for(size_t i = 0; i < statements.size(); i++) {
      AnalyzeStatement(statements[i], depth + 2);
    }
    in_loop = false;
    
    // expression
    Expression* expression = do_while_stmt->GetExpression();
    AnalyzeExpression(expression, depth + 1);
    if(!IsBooleanExpression(expression)) {
      ProcessError(expression, "Expected Bool expression");
    }
    current_table->PreviousScope();
  }

  /****************************
   * Analyzes a 'while' statement
   ****************************/
  void ContextAnalyzer::AnalyzeWhile(While* while_stmt, int depth)
  {
#ifdef _DEBUG
    Show("while", while_stmt->GetLineNumber(), depth);
#endif

    // expression
    Expression* expression = while_stmt->GetExpression();
    AnalyzeExpression(expression, depth + 1);
    if(!IsBooleanExpression(expression)) {
      ProcessError(expression, "Expected Bool expression");
    }
    // 'while' statements
    in_loop = true;
    AnalyzeStatements(while_stmt->GetStatements(), depth + 1);
    in_loop = false;
  }

  /****************************
   * Analyzes a return statement
   ****************************/
  void ContextAnalyzer::AnalyzeReturn(Return* rtrn, int depth)
  {
#ifdef _DEBUG
    Show("return", rtrn->GetLineNumber(), depth);
#endif

    Expression* expression = rtrn->GetExpression();
    Type* type = current_method->GetReturn();
    if(expression) {
      AnalyzeExpression(expression, depth + 1);
      AnalyzeRightCast(type, expression, (IsScalar(expression) && type->GetDimension() == 0), depth + 1);
      
      if(type->GetType() == CLASS_TYPE) {
	if(!ResolveClassEnumType(type)) {
	  ProcessError(rtrn, "Undefined class or enum: '" + type->GetClassName() + "'");
	}
      }
    }
    else if(type->GetType() != NIL_TYPE) {
      ProcessError(rtrn, "Invalid return statement");
    }
    
    if(current_method->GetMethodType() == NEW_PUBLIC_METHOD ||
       current_method->GetMethodType() == NEW_PRIVATE_METHOD) {
      ProcessError(rtrn, "Cannot return vaule from constructor");
    }
  }

  /****************************
   * Analyzes an assignment statement
   ****************************/
  void ContextAnalyzer::AnalyzeAssignment(Assignment* assignment, int depth)
  {
#ifdef _DEBUG
    Show("assignment", assignment->GetLineNumber(), depth);
#endif

    Variable* variable = assignment->GetVariable();
    AnalyzeVariable(variable, depth + 1);

    // get last expression for assignment
    Expression* expression = assignment->GetExpression();
    AnalyzeExpression(expression, depth + 1);
    while(expression->GetMethodCall()) {
      AnalyzeExpressionMethodCall(expression, depth + 1);
      expression = expression->GetMethodCall();
    }

    // if variable, bind it and update the instance and entry
    if(variable->GetEvalType() && variable->GetEvalType()->GetType() == VAR_TYPE) {
      if(variable->GetIndices()) {
	ProcessError(expression, "Invalid operation using Var type");
      } 
      
      SymbolEntry* entry = variable->GetEntry();
      if(entry) {
	if(expression->GetCastType()) {
	  variable->SetTypes(expression->GetCastType());
	  entry->SetType(expression->GetCastType());
	}
	else {
	  variable->SetTypes(expression->GetEvalType());
	  entry->SetType(expression->GetEvalType());
	}
	// set variable to scalar type if we're de-referencing an array variable
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

    Type* eval_type = variable->GetEvalType();
    AnalyzeRightCast(eval_type, expression, (IsScalar(variable) && IsScalar(expression)), depth + 1);

    if(expression->GetExpressionType() == METHOD_CALL_EXPR) {
      MethodCall* method_call = static_cast<MethodCall*>(expression);
      // 'Nil' return check
      if(method_call->GetMethod() && method_call->GetMethod()->GetReturn()->GetType() == NIL_TYPE &&
	 !method_call->IsFunctionDefinition()) {
	ProcessError(expression, "Invalid assignment method '" + method_call->GetMethod()->GetName() + "(..)' does not return a value");
      }

      // TODO: 'checked' return variable
      //...
    }
  }

  /****************************
   * Analyzes a logical or mathematical
   * operation.
   ****************************/
  void ContextAnalyzer::AnalyzeCalculation(CalculatedExpression* expression, int depth)
  {
    Type* cls_type = NULL;
    Expression* left = expression->GetLeft();
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

    Expression* right = expression->GetRight();
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
    switch(expression->GetExpressionType()) {
    case AND_EXPR:
    case OR_EXPR:
      if(!IsBooleanExpression(left) || !IsBooleanExpression(right)) {
	ProcessError(expression, "Invalid mathematical operation");
      }
      break;

    case EQL_EXPR:
    case NEQL_EXPR:
      if(IsBooleanExpression(left) && !IsBooleanExpression(right)) {
	ProcessError(expression, "Invalid mathematical operation");
      }
      else if(!IsBooleanExpression(left) && IsBooleanExpression(right)) {
	ProcessError(expression, "Invalid mathematical operation");
      }
      expression->SetEvalType(TypeFactory::Instance()->MakeType(BOOLEAN_TYPE), true);
      break;

    case LES_EXPR:
    case GTR_EXPR:
    case LES_EQL_EXPR:
    case GTR_EQL_EXPR:
      if(IsBooleanExpression(left) || IsBooleanExpression(right)) {
	ProcessError(expression, "Invalid mathematical operation");
      }
      else if(IsEnumExpression(left) && IsEnumExpression(right)) {
	ProcessError(expression, "Invalid mathematical operation");
      }
      else if(((cls_type = GetExpressionType(left, depth + 1)) && cls_type->GetType() == CLASS_TYPE) ||
	      ((cls_type = GetExpressionType(right, depth + 1)) && cls_type->GetType() == CLASS_TYPE)) {
	ProcessError(expression, "Invalid mathematical operation");
      }
      else if((left->GetEvalType() && left->GetEvalType()->GetType() == NIL_TYPE) ||
	      (right->GetEvalType() && right->GetEvalType()->GetType() == NIL_TYPE)) {
	ProcessError(expression, "Invalid mathematical operation");
      }
      expression->SetEvalType(TypeFactory::Instance()->MakeType(BOOLEAN_TYPE), true);
      break;

    case MOD_EXPR:
      if(IsBooleanExpression(left) || IsBooleanExpression(right)) {
	ProcessError(expression, "Invalid mathematical operation");
      }
      else if(IsEnumExpression(left) || IsEnumExpression(right)) {
	ProcessError(expression, "Invalid mathematical operation");
      }
      else if(((cls_type = GetExpressionType(left, depth + 1)) && cls_type->GetType() == CLASS_TYPE) ||
	      ((cls_type = GetExpressionType(right, depth + 1)) && cls_type->GetType() == CLASS_TYPE)) {
	ProcessError(expression, "Invalid mathematical operation");
      }
    
      if((left->GetEvalType() && left->GetEvalType()->GetType() == FLOAT_TYPE && left->GetCastType() && 
	  left->GetCastType()->GetType() != INT_TYPE) ||
	 (right->GetEvalType() && right->GetEvalType()->GetType() == FLOAT_TYPE && right->GetCastType() && 
	  right->GetCastType()->GetType() != INT_TYPE) ||
	 (left->GetCastType() && left->GetCastType()->GetType() == FLOAT_TYPE) ||
	 (right->GetCastType() && right->GetCastType()->GetType() == FLOAT_TYPE)) {
	ProcessError(expression, "Expected Byte, Char or Int class");
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
	ProcessError(expression, "Invalid mathematical operation");
      }
      else if(IsEnumExpression(left) || IsEnumExpression(right)) {
	ProcessError(expression, "Invalid mathematical operation");
      }
      else if(((cls_type = GetExpressionType(left, depth + 1)) && cls_type->GetType() == CLASS_TYPE) ||
	      ((cls_type = GetExpressionType(right, depth + 1)) && cls_type->GetType() == CLASS_TYPE)) {
	ProcessError(expression, "Invalid mathematical operation");
      }
      break;

    default:
      break;
    }
  }

  /****************************
   * Preforms type conversions
   * operational expressions.  This
   * method uses execution simulation.
   ****************************/
  void ContextAnalyzer::AnalyzeCalculationCast(CalculatedExpression* expression, int depth)
  {
    Expression* left_expr = expression->GetLeft();
    Expression* right_expr = expression->GetRight();

    Type* left = GetExpressionType(left_expr, depth + 1);
    Type* right = GetExpressionType(right_expr, depth + 1);

    if(!left || !right) {
      return;
    }

    if(!IsScalar(left_expr) || !IsScalar(right_expr)) {
      if(right->GetType() != NIL_TYPE) {
	ProcessError(left_expr, "Invalid array calculation");
      }
    }
    else {
      switch(left->GetType()) {
      case VAR_TYPE:
	// VAR
	switch(right->GetType()) {
	case FUNC_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Var and Function");
	  break;

	case VAR_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Var and Var");
	  break;

	case NIL_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Var and Nil");
	  break;

	case BYTE_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Var and Byte");
	  break;

	case CHAR_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Var and Char");
	  break;

	case INT_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Var and Int");
	  break;

	case FLOAT_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Var and Float");
	  break;

	case CLASS_TYPE:
	  if(SearchProgramEnums(right->GetClassName()) || linker->SearchEnumLibraries(right->GetClassName(), program->GetUses())) {
	    ProcessError(left_expr, "Invalid operation using classes: Var and Enum");
	  }
	  break;
	  
	case BOOLEAN_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Var and Bool");
	  break;
	}
	break;

      case NIL_TYPE:
	// NIL
	switch(right->GetType()) {
	case FUNC_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Nil and function reference");
	  break;

	case VAR_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Nil and Var");
	  break;

	case NIL_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Nil and Nil");
	  break;

	case BYTE_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Nil and Byte");
	  break;

	case CHAR_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Nil and Char");
	  break;

	case INT_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Nil and Int");
	  break;

	case FLOAT_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Nil and Float");
	  break;

	case CLASS_TYPE:
	  break;

	case BOOLEAN_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Nil and Bool");
	  break;
	}
	break;

      case BYTE_TYPE:
	// BYTE
	switch(right->GetType()) {
	case FUNC_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Byte and function reference");
	  break;

	case VAR_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Byte and Var");
	  break;

	case NIL_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Byte and Nil");
	  break;

	case CHAR_TYPE:
	case INT_TYPE:
	case BYTE_TYPE:
	  expression->SetEvalType(left, true);
	  break;

	case FLOAT_TYPE:
	  left_expr->SetCastType(right);
	  expression->SetEvalType(right, true);
	  break;

	case CLASS_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Byte and " +
		       right->GetClassName());
	  break;

	case BOOLEAN_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Byte and Bool");
	  break;
	}
	break;

      case CHAR_TYPE:
	// CHAR
	switch(right->GetType()) {
	case FUNC_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Char and function reference");
	  break;

	case VAR_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Char and Var");
	  break;

	case NIL_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Char and Nil");
	  break;

	case INT_TYPE:
	case CHAR_TYPE:
	case BYTE_TYPE:
	  expression->SetEvalType(left, true);
	  break;

	case FLOAT_TYPE:
	  left_expr->SetCastType(right);
	  expression->SetEvalType(right, true);
	  break;

	case CLASS_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Char and " +
		       right->GetClassName());
	  break;

	case BOOLEAN_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes:  Char and Bool");
	  break;
	}
	break;

      case INT_TYPE:
	// INT
	switch(right->GetType()) {
	case FUNC_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Int and function reference");
	  break;

	case VAR_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Int and Var");
	  break;

	case NIL_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Int and Nil");
	  break;

	case BYTE_TYPE:
	case CHAR_TYPE:
	case INT_TYPE:
	  expression->SetEvalType(left, true);
	  break;

	case FLOAT_TYPE:
	  left_expr->SetCastType(right);
	  expression->SetEvalType(right, true);
	  break;

	case CLASS_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Int and " + right->GetClassName());
	  break;

	case BOOLEAN_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Int and Bool");
	  break;
	}
	break;

      case FLOAT_TYPE:
	// FLOAT
	switch(right->GetType()) {
	case FUNC_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Float and function reference");
	  break;

	case VAR_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Float and Var");
	  break;

	case NIL_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Float and Nil");
	  break;

	case FLOAT_TYPE:
	  expression->SetEvalType(left, true);
	  break;

	case BYTE_TYPE:
	case CHAR_TYPE:
	case INT_TYPE:
	  right_expr->SetCastType(left);
	  expression->SetEvalType(left, true);
	  break;

	case CLASS_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Float and " +
		       right->GetClassName());
	  break;

	case BOOLEAN_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Float and Bool");
	  break;
	}
	break;

      case CLASS_TYPE:
	// CLASS
	switch(right->GetType()) {
	case FUNC_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: " +
		       left->GetClassName() + " and function reference");
	  break;

	case VAR_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: " +
		       left->GetClassName() + " and Var");
	  break;

	case NIL_TYPE:
	  break;

	case BYTE_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: " +
		       left->GetClassName() + " and Byte");
	  break;

	case CHAR_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: " +
		       left->GetClassName() + " and Char");
	  break;

	case INT_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: " +
		       left->GetClassName() + " and Int");
	  break;

	case FLOAT_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: " +
		       left->GetClassName() + " and Float");
	  break;

	case CLASS_TYPE:
	  if((SearchProgramEnums(left->GetClassName()) || linker->SearchEnumLibraries(left->GetClassName(), program->GetUses())) &&
	     (SearchProgramEnums(right->GetClassName()) || linker->SearchEnumLibraries(right->GetClassName(), program->GetUses()))) {
	    if(left->GetClassName() != right->GetClassName()) {
	      ProcessError(left_expr, "Invalid operation between mixed enums");
	    }
	  }
	  else if((!SearchProgramClasses(left->GetClassName()) && !linker->SearchClassLibraries(left->GetClassName(), program->GetUses())) ||
		  (!SearchProgramClasses(right->GetClassName()) && !linker->SearchClassLibraries(right->GetClassName(), program->GetUses()))) {
	    ProcessError(left_expr, "Invalid operation between classes or enums");
	  }
	  expression->SetEvalType(TypeFactory::Instance()->MakeType(BOOLEAN_TYPE), true);
	  break;

	case BOOLEAN_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: " +
		       left->GetClassName() + " and Bool");
	  break;
	}
	break;

      case BOOLEAN_TYPE:
	// BOOLEAN
	switch(right->GetType()) {
	case FUNC_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Bool and function reference");
	  break;

	case VAR_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Bool and Var");
	  break;

	case NIL_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Bool and Nil");
	  break;

	case BYTE_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Bool and Byte");
	  break;

	case CHAR_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Bool and Char");
	  break;

	case INT_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Bool and Int");
	  break;

	case FLOAT_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Bool and Float");
	  break;

	case CLASS_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: Bool and " +
		       right->GetClassName());
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
	  if(left->GetClassName().size() == 0) {
	    left->SetClassName("m." + EncodeFunctionType(left->GetFunctionParameters(),
						  left->GetFunctionReturn()));
	  }

	  if(right->GetClassName().size() == 0) {
	    right->SetClassName("m." + EncodeFunctionType(right->GetFunctionParameters(),
							  right->GetFunctionReturn()));
	  }

	  if(left->GetClassName() != right->GetClassName()) {
	    ProcessError(expression, "Invalid operation using functions: " +
			 left->GetClassName() + " and " + right->GetClassName());
	  }
	}
	  break;

	case VAR_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: function reference and Var");
	  break;

	case NIL_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: function reference and Nil");
	  break;

	case BYTE_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: function reference and Byte");
	  break;

	case CHAR_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: function reference and Char");
	  break;

	case INT_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: function reference and Int");
	  break;

	case FLOAT_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: function reference and Float");
	  break;

	case CLASS_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: function reference and " +
		       right->GetClassName());
	  break;

	case BOOLEAN_TYPE:
	  ProcessError(left_expr, "Invalid operation using classes: function reference and Bool");
	  break;
	}
	break;
      }
    }
  }

  /****************************
   * Preforms type conversions for
   * assignment statements.  This
   * method uses execution simulation.
   ****************************/
  void ContextAnalyzer::AnalyzeRightCast(Type* left, Expression* expression, bool is_scalar, int depth)
  {
    AnalyzeRightCast(left, GetExpressionType(expression, depth + 1), expression, is_scalar, depth);
  }

  void ContextAnalyzer::AnalyzeRightCast(Type* left, Type* right, Expression* expression, bool is_scalar, int depth)
  {
    // assert(left && right);
    if(!expression || !left || !right) {
      return;
    }

    /*
      if(expression->GetExpressionType() == METHOD_CALL_EXPR &&
      expression->GetEvalType() && expression->GetEvalType()->GetType() == NIL_TYPE) {
      ProcessError(expression, "Invalid operation method does not return a value");
      return;
      }
    */

    if(is_scalar) {
      switch(left->GetType()) {
      case VAR_TYPE:
	// VAR
	switch(right->GetType()) {
	case VAR_TYPE:
	  ProcessError(expression, "Invalid operation using classes: Var and Var");
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
	  ProcessError(expression, "Invalid operation using classes: Nil and function reference");
	  break;

	case VAR_TYPE:
	  ProcessError(expression, "Invalid operation using classes: Nil and Var");
	  break;

	case NIL_TYPE:
	  ProcessError(expression, "Invalid operation");
	  break;

	case BYTE_TYPE:
	  ProcessError(expression, "Invalid cast with classes: Nil and Byte");
	  break;

	case CHAR_TYPE:
	  ProcessError(expression, "Invalid cast with classes: Nil and Char");
	  break;

	case INT_TYPE:
	  ProcessError(expression, "Invalid cast with classes: Nil and Int");
	  break;

	case FLOAT_TYPE:
	  ProcessError(expression, "Invalid cast with classes: Nil and Float");
	  break;

	case CLASS_TYPE:
	  ProcessError(expression, "Invalid cast with classes: Nil and " + right->GetClassName());
	  break;

	case BOOLEAN_TYPE:
	  ProcessError(expression, "Invalid cast with classes: Nil and Bool");
	  break;
	}
	break;

      case BYTE_TYPE:
	// BYTE
	switch(right->GetType()) {
	case FUNC_TYPE:
	  ProcessError(expression, "Invalid operation using classes: Byte and function reference");
	  break;

	case VAR_TYPE:
	  ProcessError(expression, "Invalid operation using classes: Byte and Var");
	  break;

	case NIL_TYPE:
	  if(left->GetDimension() < 1) {
	    ProcessError(expression, "Invalid cast with classes: Byte and Nil");
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
	  expression->SetCastType(left);
	  expression->SetEvalType(right, false);
	  break;

	case CLASS_TYPE:
	  if(!SearchProgramEnums(right->GetClassName()) &&
	     !linker->SearchEnumLibraries(right->GetClassName(), program->GetUses())) {
	    ProcessError(expression, "Invalid cast with classes: Byte and " + right->GetClassName());
	  }
	  break;

	case BOOLEAN_TYPE:
	  ProcessError(expression, "Invalid cast with classes: Byte and Bool");
	  break;
	}
	break;

      case CHAR_TYPE:
	// CHAR
	switch(right->GetType()) {
	case FUNC_TYPE:
	  ProcessError(expression, "Invalid operation using classes: Char and function reference");
	  break;

	case VAR_TYPE:
	  ProcessError(expression, "Invalid operation using classes: Char and Var");
	  break;

	case NIL_TYPE:
	  if(left->GetDimension() < 1) {
	    ProcessError(expression, "Invalid cast with classes: Char and Nil");
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
	  expression->SetCastType(left);
	  expression->SetEvalType(right, false);
	  break;

	case CLASS_TYPE:
	  if(!SearchProgramEnums(right->GetClassName()) &&
	     !linker->SearchEnumLibraries(right->GetClassName(), program->GetUses())) {
	    ProcessError(expression, "Invalid cast with classes: Char and " + right->GetClassName());
	  }
	  break;

	case BOOLEAN_TYPE:
	  ProcessError(expression, "Invalid cast with classes: Char and Bool");
	  break;
	}
	break;

      case INT_TYPE:
	// INT
	switch(right->GetType()) {
	case FUNC_TYPE:
	  ProcessError(expression, "Invalid operation using classes: Int and function reference");
	  break;

	case VAR_TYPE:
	  ProcessError(expression, "Invalid operation using classes: Var and Int");
	  break;

	case NIL_TYPE:
	  if(left->GetDimension() < 1) {
	    ProcessError(expression, "Invalid cast with classes: Int and Nil");
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
	  expression->SetCastType(left);
	  expression->SetEvalType(right, false);
	  break;

	case CLASS_TYPE:
	  if(!SearchProgramEnums(right->GetClassName()) &&
	     !linker->SearchEnumLibraries(right->GetClassName(), program->GetUses())) {
	    ProcessError(expression, "Invalid cast with classes: Int and " +
			 right->GetClassName());
	  }
	  break;

	case BOOLEAN_TYPE:
	  ProcessError(expression, "Invalid cast with classes: Int and Bool");
	  break;
	}
	break;

      case FLOAT_TYPE:
	// FLOAT
	switch(right->GetType()) {
	case FUNC_TYPE:
	  ProcessError(expression, "Invalid operation using classes: Float and function reference");
	  break;

	case VAR_TYPE:
	  ProcessError(expression, "Invalid operation using classes: Nil and Var");
	  break;

	case NIL_TYPE:
	  if(left->GetDimension() < 1) {
	    ProcessError(expression, "Invalid cast with classes: Float and Nil");
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
	  expression->SetCastType(left);
	  expression->SetEvalType(right, false);
	  break;

	case CLASS_TYPE:
	  ProcessError(expression, "Invalid cast with classes: Float and " + right->GetClassName());
	  break;

	case BOOLEAN_TYPE:
	  ProcessError(expression, "Invalid cast with classes: Float and Bool");
	  break;
	}
	break;

      case CLASS_TYPE:
	// CLASS
	switch(right->GetType()) {
	case FUNC_TYPE:
	  ProcessError(expression, "Invalid operation using classes: " + left->GetClassName() + " and function reference");
	  break;

	case VAR_TYPE:
	  ProcessError(expression, "Invalid cast with classes: " + left->GetClassName() + " and Var");
	  break;

	case NIL_TYPE:
	  if(expression->GetExpressionType() != NIL_LIT_EXPR) {
	    ProcessError(expression, "Invalid cast with classes: " + left->GetClassName() + " and Nil");
	  }
	  break;

	case BYTE_TYPE:
	  if(!SearchProgramEnums(left->GetClassName()) &&
	     !linker->SearchEnumLibraries(left->GetClassName(), program->GetUses())) {
	    ProcessError(expression, "Invalid cast with classes: " + left->GetClassName() + " and Byte");
	  }
	  break;

	case CHAR_TYPE:
	  if(!SearchProgramEnums(left->GetClassName()) &&
	     !linker->SearchEnumLibraries(left->GetClassName(), program->GetUses())) {
	    ProcessError(expression, "Invalid cast with classes: " + left->GetClassName() + " and Char");
	  }
	  break;

	case INT_TYPE:
	  if(!SearchProgramEnums(left->GetClassName()) &&
	     !linker->SearchEnumLibraries(left->GetClassName(), program->GetUses())) {
	    ProcessError(expression, "Invalid cast with classes: " + left->GetClassName() + " and Int");
	  }
	  break;

	case FLOAT_TYPE:
	  ProcessError(expression, "Invalid cast with classes: " + left->GetClassName() + " and Float");
	  break;

	case CLASS_TYPE:
	  AnalyzeClassCast(left, expression, depth + 1);
	  break;

	case BOOLEAN_TYPE:
	  ProcessError(expression, "Invalid cast with classes: " + left->GetClassName() +
		       " and Bool");
	  break;
	}
	break;

      case BOOLEAN_TYPE:
	// BOOLEAN
	switch(right->GetType()) {
	case FUNC_TYPE:
	  ProcessError(expression, "Invalid operation using classes: Bool and function reference");
	  break;

	case VAR_TYPE:
	  ProcessError(expression, "Invalid operation using classes: Bool and Var");
	  break;

	case NIL_TYPE:
	  if(left->GetDimension() < 1) {
	    ProcessError(expression, "Invalid cast with classes: Bool and Nil");
	  }
	  break;

	case BYTE_TYPE:
	  ProcessError(expression, "Invalid cast with classes: Bool and Byte");
	  break;

	case CHAR_TYPE:
	  ProcessError(expression, "Invalid cast with classes: Bool and Char");
	  break;

	case INT_TYPE:
	  ProcessError(expression, "Invalid cast with classes: Bool and Int");
	  break;

	case FLOAT_TYPE:
	  ProcessError(expression, "Invalid cast with classes: Bool and Float");
	  break;

	case CLASS_TYPE:
	  ProcessError(expression, "Invalid cast with classes: Bool and " + right->GetClassName());
	  break;

	case BOOLEAN_TYPE:
	  break;
	}
	break;

      case FUNC_TYPE:
	// FUNCTION
	switch(right->GetType()) {
	case FUNC_TYPE: {
	  if(left->GetClassName().size() == 0) {
	    left->SetClassName("m." + EncodeFunctionType(left->GetFunctionParameters(),
							 left->GetFunctionReturn()));
	  }

	  if(right->GetClassName().size() == 0) {
	    right->SetClassName("m." + EncodeFunctionType(right->GetFunctionParameters(),
							  right->GetFunctionReturn()));
	  }

	  if(left->GetClassName() != right->GetClassName()) {
	    ProcessError(expression, "Invalid operation using mismatch functions: " +
			 left->GetClassName() + " and " + right->GetClassName());
	  }
	}
	  break;

	case VAR_TYPE:
	  ProcessError(expression, "Invalid operation using classes: function reference and Var");
	  break;

	case NIL_TYPE:
	  ProcessError(expression, "Invalid cast with classes: function reference and Nil");
	  break;

	case BYTE_TYPE:
	  ProcessError(expression, "Invalid cast with classes: function reference and Byte");
	  break;

	case CHAR_TYPE:
	  ProcessError(expression, "Invalid cast with classes: function reference and Char");
	  break;

	case INT_TYPE:
	  ProcessError(expression, "Invalid cast with classes: function reference and Int");
	  break;

	case FLOAT_TYPE:
	  ProcessError(expression, "Invalid cast with classes: function reference and Float");
	  break;

	case CLASS_TYPE:
	  ProcessError(expression, "Invalid cast with classes: function reference and " + right->GetClassName());
	  break;

	case BOOLEAN_TYPE:
	  ProcessError(expression, "Invalid cast with classes: function reference and Bool");
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
	ProcessError(expression, "Dimension size mismatch");
      }

      if(left->GetType() != right->GetType() &&
	 right->GetType() != NIL_TYPE) {
	ProcessError(expression, "Invalid array cast");
      }

      if(left->GetType() == CLASS_TYPE && right->GetType() == CLASS_TYPE) {
	AnalyzeClassCast(left, expression, depth + 1);
      }

      expression->SetEvalType(left, false);
    }
  }

  /****************************
   * Analyzes a class cast. Up
   * casting is resolved a runtime.
   ****************************/
  void ContextAnalyzer::AnalyzeClassCast(Type* left, Expression* expression, int depth)
  {
    Type* right = expression->GetCastType();
    if(!right) {
      right = expression->GetEvalType();
    }
    
    //
    // program enumt
    //
    if(left && right && SearchProgramEnums(left->GetClassName())) {
      Enum* left_enum = SearchProgramEnums(left->GetClassName());
      // program
      Enum* right_enum = SearchProgramEnums(right->GetClassName());
      if(right_enum) {
	if(left_enum->GetName() != right_enum->GetName()) {
	  ProcessError(expression, "Invalid cast between enums: '" +
		       left->GetClassName() + "' and '" +
		       right->GetClassName() + "'");
	}
      }
      // library
      else if(right && linker->SearchEnumLibraries(right->GetClassName(), program->GetUses())) {
	LibraryEnum* right_lib_enum = linker->SearchEnumLibraries(right->GetClassName(), program->GetUses());
	if(left_enum->GetName() != right_lib_enum->GetName()) {
	  ProcessError(expression, "Invalid cast between enums: '" +
		       left->GetClassName() + "' and '" +
		       right->GetClassName() + "'");
	}
      }
      else {
	ProcessError(expression, "Invalid cast between enum and class");
      }
    }
    //
    // program class
    //
    else if(left && right && SearchProgramClasses(left->GetClassName())) {
      Class* left_class = SearchProgramClasses(left->GetClassName());
      // program
      Class* right_class = SearchProgramClasses(right->GetClassName());
      if(right_class) {
	// downcast
	if(ValidDownCast(left_class->GetName(), right_class, NULL)) {
	  left_class->SetCalled(true);
	  right_class->SetCalled(true);
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
	  ProcessError(expression, "Invalid cast between classes: '" +
		       left->GetClassName() + "' and '" +
		       right->GetClassName() + "'");
	}
      }
      // library
      else if(linker->SearchClassLibraries(right->GetClassName(), program->GetUses())) {
	LibraryClass* right_lib_class = linker->SearchClassLibraries(right->GetClassName(), program->GetUses());
	// downcast
	if(ValidDownCast(left_class->GetName(), NULL, right_lib_class)) {
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
	  ProcessError(expression, "Invalid cast between classes: '" +
		       left->GetClassName() + "' and '" +
		       right->GetClassName() + "'");
	}
      } else {
	ProcessError(expression, "Invalid cast between class, enum or Nil");
      }
    }
    //
    // enum libary
    //
    else if(left && right && linker->SearchEnumLibraries(left->GetClassName(), program->GetUses())) {
      LibraryEnum* left_lib_enum = linker->SearchEnumLibraries(left->GetClassName(), program->GetUses());
      // program
      Enum* right_enum = SearchProgramEnums(right->GetClassName());
      if(right_enum) {
	if(left_lib_enum->GetName() != right_enum->GetName()) {
	  ProcessError(expression, "Invalid cast between enums: '" +
		       left_lib_enum->GetName() + "' and '" +
		       right_enum->GetName() + "'");
	}
      }
      // library
      else if(linker->SearchEnumLibraries(right->GetClassName(), program->GetUses())) {
	LibraryEnum* right_lib_enum = linker->SearchEnumLibraries(right->GetClassName(), program->GetUses());
	if(left_lib_enum->GetName() != right_lib_enum->GetName()) {
	  ProcessError(expression, "Invalid cast between enums: '" +
		       left_lib_enum->GetName() + "' and '" +
		       right_lib_enum->GetName() + "'");
	}
      } else {
	ProcessError(expression, "Invalid cast between enum and class");
      }
    }
    //
    // class libary
    //
    else if(left && right && linker->SearchClassLibraries(left->GetClassName(), program->GetUses())) {
      LibraryClass* left_lib_class = linker->SearchClassLibraries(left->GetClassName(), program->GetUses());
      // program
      Class* right_class = SearchProgramClasses(right->GetClassName());
      if(right_class) {
	// downcast
	if(ValidDownCast(left_lib_class->GetName(), right_class, NULL)) {
	  left_lib_class->SetCalled(true);
	  right_class->SetCalled(true);
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
	  ProcessError(expression, "Invalid cast between classes: '" + left->GetClassName() + "' and '" +
		       right->GetClassName() + "'");
	}
      }
      // libary
      else if(linker->SearchClassLibraries(right->GetClassName(), program->GetUses())) {
	LibraryClass* right_lib_class = linker->SearchClassLibraries(right->GetClassName(), program->GetUses());
	// downcast
	if(ValidDownCast(left_lib_class->GetName(), NULL, right_lib_class)) {
	  left_lib_class->SetCalled(true);
	  right_lib_class->SetCalled(true);
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
	else {
	  // TODO: workaround for class cast issue
	  ProcessError(expression, "Invalid cast between classes: '" +
		       left_lib_class->GetName() + "' and '" +
		       right_lib_class->GetName() + "'");        
	}
      } 
      else {
	ProcessError(expression, "Invalid cast between class, enum or Nil");
      }
    }
    else {
      ProcessError(expression, "Invalid class or enum");
    }
  }

  /****************************
   * Analyzes a declaration
   ****************************/
  void ContextAnalyzer::AnalyzeDeclaration(Declaration* declaration, int depth)
  {
    SymbolEntry* entry = declaration->GetEntry();
    if(entry) {
      if(entry->GetType() && entry->GetType()->GetType() == CLASS_TYPE) {
	// resolve class name
	if(!ResolveClassEnumType(entry->GetType())) {
	  ProcessError(entry, "Undefined class or enum: '" + entry->GetType()->GetClassName() + "'");
	}
      }
      else if(entry->GetType() && entry->GetType()->GetType() == FUNC_TYPE) {
	// resolve function name
	Type* type = entry->GetType();
	const string encoded_name = "m." + EncodeFunctionType(type->GetFunctionParameters(),
							      type->GetFunctionReturn());
#ifdef _DEBUG
	cout << "Encoded function declaration: |" << encoded_name << "|" << endl;
#endif
	type->SetClassName(encoded_name);
      }
      
      Statement* statement = declaration->GetAssignment();
      if(statement) {
	AnalyzeStatement(statement, depth);
      }
    }
    else {
      ProcessError(declaration, "Undefined variable entry");
    }
  }

  /****************************
   * Analyzes a declaration
   ****************************/
  void ContextAnalyzer::AnalyzeExpressions(ExpressionList* parameters, int depth)
  {
    vector<Expression*> expressions = parameters->GetExpressions();
    for(size_t i = 0; i < expressions.size(); i++) {
      AnalyzeExpression(expressions[i], depth);
    }
  }

  /********************************
   * Encodes a function definition
   ********************************/
  string ContextAnalyzer::EncodeFunctionReference(ExpressionList* calling_params, int depth)
  {
    // TODO: return direct types vs. string literals
    string encoded_name;

    vector<Expression*> expressions = calling_params->GetExpressions();
    for(size_t i = 0; i < expressions.size(); i++) {
      if(expressions[i]->GetExpressionType() == VAR_EXPR) {
	Variable* variable = static_cast<Variable*>(expressions[i]);
	if(variable->GetName() == BOOL_CLASS_ID) {
	  encoded_name += 'l';
	  variable->SetEvalType(TypeFactory::Instance()->MakeType(BOOLEAN_TYPE), true);
	}
	else if(variable->GetName() == BYTE_CLASS_ID) {
	  encoded_name += 'b';
	  variable->SetEvalType(TypeFactory::Instance()->MakeType(BYTE_TYPE), true);
	}
	else if(variable->GetName() == INT_CLASS_ID) {
	  encoded_name += 'i';
	  variable->SetEvalType(TypeFactory::Instance()->MakeType(INT_TYPE), true);
	}
	else if(variable->GetName() == FLOAT_CLASS_ID) {
	  encoded_name += 'f';
	  variable->SetEvalType(TypeFactory::Instance()->MakeType(FLOAT_TYPE), true);
	}
	else if(variable->GetName() == CHAR_CLASS_ID) {
	  encoded_name += 'c';
	  variable->SetEvalType(TypeFactory::Instance()->MakeType(CHAR_TYPE), true);
	}
	else if(variable->GetName() == NIL_CLASS_ID) {
	  encoded_name += 'n';
	  variable->SetEvalType(TypeFactory::Instance()->MakeType(NIL_TYPE), true);
	}
	else if(variable->GetName() == VAR_CLASS_ID) {
	  encoded_name += 'v';
	  variable->SetEvalType(TypeFactory::Instance()->MakeType(VAR_TYPE), true);
	}
	else {
	  encoded_name += "o.";
	  // search program
	  string klass_name = variable->GetName();
	  Class* klass = program->GetClass(klass_name);
	  if(!klass) {
	    vector<string> uses = program->GetUses();
	    for(size_t i = 0; !klass && i < uses.size(); i++) {
	      klass = program->GetClass(uses[i] + "." + klass_name);
	    }
	  }
	  if(klass) {
	    encoded_name += klass->GetName();
	    variable->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, klass->GetName()), true);
	  }
	  // search libaraires
	  else {
	    LibraryClass* lib_klass = linker->SearchClassLibraries(klass_name, program->GetUses());
	    if(lib_klass) {
	      encoded_name += lib_klass->GetName();
	      variable->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, lib_klass->GetName()), true);
	    }
	    else {
	      encoded_name += variable->GetName();
	      variable->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, variable->GetName()), true);
	    }
	  }
	}

	// dimension
	if(variable->GetIndices()) {
	  vector<Expression*> indices = variable->GetIndices()->GetExpressions();
	  variable->GetEvalType()->SetDimension(indices.size());
	  for(size_t i = 0; i < indices.size(); i++) {
	    encoded_name += '*';
	  }
	}

	encoded_name += ',';
      }
      else {
	// induce error condition
	encoded_name += '#';
      }
    }

    return encoded_name;
  }

  /****************************
   * Encodes a function type
   ****************************/
  string ContextAnalyzer::EncodeFunctionType(vector<Type*> func_params, Type* func_rtrn) {
    string encoded_name = "(";
    for(size_t i = 0; i < func_params.size(); i++) {
      // encode params
      encoded_name += EncodeType(func_params[i]);

      // encode dimension
      for(int i = 0; i < func_params[i]->GetDimension(); i++) {
	encoded_name += '*';
      }
      encoded_name += ',';
    }
    
    // encode return
    encoded_name += ")~";
    encoded_name += EncodeType(func_rtrn);
    // encode dimension
    for(int i = 0; func_rtrn && i < func_rtrn->GetDimension(); i++) {
      encoded_name += '*';
    }

    return encoded_name;
  }

  /****************************
   * Encodes a method call
   ****************************/
  string ContextAnalyzer::EncodeMethodCall(ExpressionList* calling_params, int depth)
  {
    AnalyzeExpressions(calling_params, depth + 1);

    string encoded_name;
    vector<Expression*> expressions = calling_params->GetExpressions();
    for(size_t i = 0; i < expressions.size(); i++) {
      Expression* expression = expressions[i];
      while(expression->GetMethodCall()) {
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
	for(int i = 0; !IsScalar(expression) && i < type->GetDimension(); i++) {
	  encoded_name += '*';
	}
	encoded_name += ',';
      }
    }

    return encoded_name;
  }

