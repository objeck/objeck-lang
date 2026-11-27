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
#include "parser.h"
#include "../shared/instrs.h"

namespace {
  // sets a pointer for a scope (when given one) and restores it on every
  // return: the node being analyzed, where a name it resolves is written
  // (ContextAnalyzer::CheckAmbiguousUse), or the bundle a name resolves in
  template<class T> struct PointerScope {
    T*& slot;
    T* saved;

    PointerScope(T*& s, T* n) : slot(s), saved(s) {
      if(n) {
        slot = n;
      }
    }

    ~PointerScope() {
      slot = saved;
    }
  };
  typedef PointerScope<ParseNode> ResolveSiteScope;

  // counts a scope in, and out on every return
  struct CountScope {
    int& count;
    const bool on;

    CountScope(int& c, bool o) : count(c), on(o) {
      if(on) {
        ++count;
      }
    }

    ~CountScope() {
      if(on) {
        --count;
      }
    }
  };

  // A name written in a class other than the one being analyzed -- a callee's
  // declared parameter or return type, an interface's return type -- resolves
  // in that class's bundle, where a name the bundle declares wins. Its
  // ambiguity check is left to that class's own analysis, in its own file.
  struct OtherClassScope {
    CountScope skip;
    PointerScope<ParsedBundle> bundle;

    OtherClassScope(int& skip_count, ParsedBundle*& bundle_slot, ParsedBundle* other_bundle) :
      skip(skip_count, other_bundle != nullptr), bundle(bundle_slot, other_bundle) {
    }
  };
}

#ifdef _SYSTEM
/****************************
 * Whether a system-library body
 * runs a trap (a trap leaves the
 * method's return value itself)
 ****************************/
static bool HasSystemStatement(StatementList* statement_list)
{
  if(!statement_list) {
    return false;
  }

  const std::vector<Statement*> statements = statement_list->GetStatements();
  for(Statement* statement : statements) {
    if(statement->GetStatementType() == SYSTEM_STMT) {
      return true;
    }
  }

  return false;
}
#endif

/****************************
 * Maps expression type to
 * operator symbol string
 ****************************/
static const std::wstring ExpressionTypeToOperator(ExpressionType type)
{
  switch(type) {
  case ADD_EXPR:
    return L"+";
  case SUB_EXPR:
    return L"-";
  case MUL_EXPR:
    return L"*";
  case DIV_EXPR:
    return L"/";
  case MOD_EXPR:
    return L"%";
  case EQL_EXPR:
    return L"=";
  case NEQL_EXPR:
    return L"<>";
  case LES_EXPR:
    return L"<";
  case GTR_EXPR:
    return L">";
  case LES_EQL_EXPR:
    return L"<=";
  case GTR_EQL_EXPR:
    return L">=";
  case AND_EXPR:
    return L"&";
  case OR_EXPR:
    return L"|";
  case BIT_AND_EXPR:
    return L"and";
  case BIT_OR_EXPR:
    return L"or";
  case BIT_XOR_EXPR:
    return L"xor";
  case SHL_EXPR:
    return L"<<";
  case SHR_EXPR:
    return L">>";
  default:
    return L"?";
  }
}

/****************************
 * Returns short type name
 * from frontend type enum
 ****************************/
static const std::wstring ShortTypeName(frontend::Type* type)
{
  switch(type->GetType()) {
  case BYTE_TYPE:
    return L"Byte";
  case CHAR_TYPE:
    return L"Char";
  case INT_TYPE:
    return L"Int";
  case FLOAT_TYPE:
    return L"Float";
  case BOOLEAN_TYPE:
    return L"Bool";
  case NIL_TYPE:
    return L"Nil";
  case VAR_TYPE:
    return L"Var";
  case FUNC_TYPE:
    return L"Function";
  case CLASS_TYPE:
    return type->GetName();
  default:
    return type->GetName();
  }
}

// Renders a type the way the user wrote it — e.g. "Hash<String, IntRef>" or
// "Vector<Pair<String, IntRef>>[]" — for diagnostics. Recurses through generic
// arguments and appends array dimensions. Used only in error text.
static std::wstring FormatGenericType(frontend::Type* type)
{
  if(!type) {
    return L"<unknown>";
  }
  std::wstring out = ShortTypeName(type);
  if(type->HasGenerics()) {
    out += L'<';
    const std::vector<frontend::Type*> args = type->GetGenerics();
    for(size_t i = 0; i < args.size(); ++i) {
      if(i > 0) {
        out += L", ";
      }
      out += FormatGenericType(args[i]);
    }
    out += L'>';
  }
  for(int i = 0; i < type->GetDimension(); ++i) {
    out += L"[]";
  }
  return out;
}

// Structural equality of two already-resolved (bound-/concrete-substituted)
// generic argument types: same base name, same array dimension, and recursively
// equal type arguments. Replaces the previous approach of building "<Name|Arg>"
// strings and comparing them — same acceptance, but robust to name formatting
// and free of per-comparison string allocation.
static bool StructuralGenericEquals(frontend::Type* a, frontend::Type* b)
{
  if(!a || !b) {
    return a == b;
  }
  if(a->GetName() != b->GetName()) {
    return false;
  }
  if(a->GetDimension() != b->GetDimension()) {
    return false;
  }
  const std::vector<frontend::Type*> a_args = a->GetGenerics();
  const std::vector<frontend::Type*> b_args = b->GetGenerics();
  if(a_args.size() != b_args.size()) {
    return false;
  }
  for(size_t i = 0; i < a_args.size(); ++i) {
    if(!StructuralGenericEquals(a_args[i], b_args[i])) {
      return false;
    }
  }
  return true;
}

/****************************
 * Emits an error
 ****************************/
void ContextAnalyzer::ProcessError(ParseNode* node, const std::wstring &msg)
{
  diagnostics.Error(node->GetFileName(), node->GetLineNumber(), node->GetLinePosition(), Elaborate(node, msg));
}

/****************************
 * An analysis that cannot continue: the error is recorded as usual and then
 * thrown, where it used to be recorded and the process ended without printing
 * it. The text is the located form ProcessError stores.
 ****************************/
void ContextAnalyzer::ProcessFatalError(ParseNode* node, const std::wstring& msg)
{
  ProcessError(node, msg);
  throw CompilerFatal(node->GetFileName() + L":(" + ToString(node->GetLineNumber()) + L',' + ToString(node->GetLinePosition()) + L"): " + msg);
}

/****************************
 * Emits an error
 ****************************/
void ContextAnalyzer::ProcessError(const std::wstring& fn, int ln, int lp, const std::wstring& msg)
{
  diagnostics.Error(fn, ln, lp, msg);
}

/****************************
 * Emits an error
 ****************************/
void ContextAnalyzer::ProcessError(const std::wstring& fn, const std::wstring& msg)
{
  diagnostics.Error(fn, msg);
}

/****************************
 * A bundle that defines an unresolved short class name, when one exists and is
 * not already in scope. "Undefined class: 'Vector'" is most often a missing
 * 'use', and the linker already knows which bundle every library class is in.
 ****************************/
std::wstring ContextAnalyzer::BundleSuggestion(const std::wstring& name)
{
  // an already-qualified name is a different mistake
  if(name.empty() || name.find(L'.') != std::wstring::npos || !linker) {
    return L"";
  }

  std::vector<std::wstring> uses;
  if(current_class) {
    uses = program->GetLibUses(current_class->GetFileName());
  }

  std::set<std::wstring> bundles;
  const std::unordered_map<std::wstring, LibraryClass*>& klasses = linker->GetAllClassesMap();
  for(std::unordered_map<std::wstring, LibraryClass*>::const_iterator iter = klasses.begin(); iter != klasses.end(); ++iter) {
    const std::wstring& full_name = iter->first;
    const size_t dot = full_name.find_last_of(L'.');
    if(dot == std::wstring::npos || full_name.substr(dot + 1) != name) {
      continue;
    }

    // Already used and still unresolved means the bundle is not the problem,
    // so suggesting it would send the reader the wrong way.
    const std::wstring bundle = full_name.substr(0, dot);
    if(std::find(uses.begin(), uses.end(), bundle) == uses.end()) {
      bundles.insert(bundle);
    }
  }

  if(bundles.empty()) {
    return L"";
  }

  std::wstring message = L"\n\t'" + name + L"' is defined in ";
  if(bundles.size() == 1) {
    message += L"bundle '" + *bundles.begin() + L"' -- add: use " + *bundles.begin() + L";";
  }
  else {
    message += L"bundles: ";
    for(std::set<std::wstring>::const_iterator iter = bundles.begin(); iter != bundles.end(); ++iter) {
      if(iter != bundles.begin()) {
        message += L", ";
      }
      message += *iter;
    }
    message += L" -- add a 'use' for the one you want.";
  }

  return message;
}

/****************************
 * Name suggestions: the names that
 * are one slip away from a misspelling
 ****************************/
static std::wstring LowerCase(std::wstring text)
{
  for(size_t i = 0; i < text.size(); ++i) {
    text[i] = (wchar_t)towlower(text[i]);
  }

  return text;
}

// optimal string alignment distance: an insertion, a deletion, a substitution or
// the swap of two neighbours each cost one
static size_t EditDistance(const std::wstring& a, const std::wstring& b)
{
  const size_t n = a.size();
  const size_t m = b.size();
  std::vector<std::vector<size_t> > d(n + 1, std::vector<size_t>(m + 1, 0));
  for(size_t i = 0; i <= n; ++i) {
    d[i][0] = i;
  }
  for(size_t j = 0; j <= m; ++j) {
    d[0][j] = j;
  }

  for(size_t i = 1; i <= n; ++i) {
    for(size_t j = 1; j <= m; ++j) {
      const size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
      // (std::min) in parentheses: <windows.h> defines min and max as macros
      size_t best = (std::min)((std::min)(d[i - 1][j] + 1, d[i][j - 1] + 1), d[i - 1][j - 1] + cost);
      if(i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
        best = (std::min)(best, d[i - 2][j - 2] + 1);
      }
      d[i][j] = best;
    }
  }

  return d[n][m];
}

// every letter of 'shorter' appears in 'longer', in order: how an abbreviation reads
static bool IsSubsequence(const std::wstring& shorter, const std::wstring& longer)
{
  size_t at = 0;
  for(size_t i = 0; i < longer.size() && at < shorter.size(); ++i) {
    if(longer[i] == shorter[at]) {
      at++;
    }
  }

  return at == shorter.size();
}

// up to three of the candidates that a misspelling of 'name' could be, closest first
static std::vector<std::wstring> CloseNames(const std::wstring& name, const std::vector<std::wstring>& candidates)
{
  std::vector<std::pair<size_t, std::wstring> > scored;
  const std::wstring lower_name = LowerCase(name);
  for(size_t i = 0; i < candidates.size(); ++i) {
    const std::wstring& candidate = candidates[i];
    if(candidate.empty() || candidate == name) {
      continue;
    }

    // a name more than three letters longer or shorter is no misspelling or abbreviation of this one
    if(candidate.size() + 3 < name.size() || name.size() + 3 < candidate.size()) {
      continue;
    }

    const std::wstring lower_candidate = LowerCase(candidate);
    size_t score = 100;
    if(lower_candidate == lower_name) {
      score = 0;
    }
    else {
      const size_t longest = (std::max)(name.size(), candidate.size());
      const size_t limit = longest >= 8 ? 3 : longest >= 5 ? 2 : longest >= 3 ? 1 : 0;
      const size_t distance = EditDistance(lower_name, lower_candidate);
      const std::wstring& shorter = lower_name.size() < lower_candidate.size() ? lower_name : lower_candidate;
      const std::wstring& longer = lower_name.size() < lower_candidate.size() ? lower_candidate : lower_name;
      if(distance <= limit) {
        score = distance;
      }
      // an abbreviation: 'cnt' for 'count'
      else if(shorter.size() >= 2 && longer.size() - shorter.size() <= 3 && IsSubsequence(shorter, longer)) {
        score = longer.size() - shorter.size() + 1;
      }
    }

    if(score < 100) {
      scored.push_back(std::make_pair(score, candidate));
    }
  }

  std::sort(scored.begin(), scored.end());
  std::vector<std::wstring> names;
  for(size_t i = 0; i < scored.size() && names.size() < 3; ++i) {
    if(std::find(names.begin(), names.end(), scored[i].second) == names.end()) {
      names.push_back(scored[i].second);
    }
  }

  return names;
}

static std::wstring FormatSuggestion(const std::wstring& name, const std::vector<std::wstring>& names)
{
  if(names.empty()) {
    return L"";
  }

  if(names.size() == 1) {
    if(LowerCase(names[0]) == LowerCase(name)) {
      return L"\n\tDid you mean '" + names[0] + L"'? Names are case-sensitive.";
    }
    return L"\n\tDid you mean '" + names[0] + L"'?";
  }

  std::wstring list;
  for(size_t i = 0; i < names.size(); ++i) {
    list += (i > 0 ? L", '" : L"'") + names[i] + L"'";
  }

  return L"\n\tDid you mean one of: " + list + L"?";
}

// the method name of an encoded 'Class:Method:params' signature
static std::wstring MethodNameOf(const std::wstring& encoded)
{
  const size_t first = encoded.find(L':');
  if(first == std::wstring::npos) {
    return encoded;
  }

  const size_t second = encoded.find(L':', first + 1);
  return encoded.substr(first + 1, second == std::wstring::npos ? std::wstring::npos : second - first - 1);
}

// the name between the first pair of quotes in a message
static std::wstring QuotedName(const std::wstring& msg)
{
  const size_t open = msg.find(L'\'');
  if(open == std::wstring::npos) {
    return L"";
  }

  const size_t close = msg.find(L'\'', open + 1);
  return close == std::wstring::npos ? L"" : msg.substr(open + 1, close - open - 1);
}

static bool StartsWith(const std::wstring& text, const wchar_t* prefix)
{
  return text.compare(0, wcslen(prefix), prefix) == 0;
}

static std::wstring PlainTypeName(const std::wstring& name)
{
  return StartsWith(name, L"System.") ? name.substr(7) : name;
}

/****************************
 * The variables in scope that a
 * misspelled name could be
 ****************************/
std::wstring ContextAnalyzer::VariableSuggestion(const std::wstring& name)
{
  std::vector<std::wstring> names;
  const bool wants_field = !name.empty() && name[0] == L'@';

  if(current_table && current_method && !wants_field) {
    const std::wstring prefix = current_method->GetName() + L':';
    const std::vector<SymbolEntry*> entries = current_table->GetEntries();
    for(size_t i = 0; i < entries.size(); ++i) {
      const std::wstring& entry_name = entries[i]->GetName();
      if(StartsWith(entry_name, prefix.c_str())) {
        const std::wstring local = entry_name.substr(prefix.size());
        if(!local.empty() && local[0] != L'#' && local[0] != L'@') {
          names.push_back(local);
        }
      }
    }
  }

  if(wants_field && current_class) {
    SymbolTable* table = symbol_table->GetSymbolTable(current_class->GetName());
    if(table) {
      const std::wstring prefix = current_class->GetName() + L':';
      const std::vector<SymbolEntry*> entries = table->GetEntries();
      for(size_t i = 0; i < entries.size(); ++i) {
        const std::wstring& entry_name = entries[i]->GetName();
        if(StartsWith(entry_name, prefix.c_str())) {
          const std::wstring field = entry_name.substr(prefix.size());
          if(!field.empty() && field[0] == L'@') {
            names.push_back(field);
          }
        }
      }
    }
  }

  return FormatSuggestion(name, CloseNames(name, names));
}

/****************************
 * The classes a misspelled
 * class name could be
 ****************************/
std::wstring ContextAnalyzer::ClassSuggestion(const std::wstring& name)
{
  if(name.empty() || name.find(L'.') != std::wstring::npos) {
    return L"";
  }

  std::vector<std::wstring> names;
  if(bundle) {
    const std::vector<Class*> classes = bundle->GetClasses();
    for(size_t i = 0; i < classes.size(); ++i) {
      const std::wstring& full = classes[i]->GetName();
      const size_t dot = full.find_last_of(L'.');
      names.push_back(dot == std::wstring::npos ? full : full.substr(dot + 1));
    }
  }

  if(linker) {
    const std::unordered_map<std::wstring, LibraryClass*>& klasses = linker->GetAllClassesMap();
    for(std::unordered_map<std::wstring, LibraryClass*>::const_iterator iter = klasses.begin(); iter != klasses.end(); ++iter) {
      const std::wstring& full = iter->first;
      const size_t dot = full.find_last_of(L'.');
      names.push_back(dot == std::wstring::npos ? full : full.substr(dot + 1));
    }
  }

  return FormatSuggestion(name, CloseNames(name, names));
}

/****************************
 * The class a failed call was
 * made on
 ****************************/
void ContextAnalyzer::DiagReceiver(Class*& klass, LibraryClass*& lib_klass)
{
  klass = diag_class;
  lib_klass = diag_lib_class;
  if(lib_klass && PlainTypeName(lib_klass->GetName()) == L"Base" && current_class) {
    klass = current_class;
    lib_klass = nullptr;
  }
}

/****************************
 * An error that only follows from
 * an earlier one
 ****************************/
bool ContextAnalyzer::IsCascade(SymbolEntry* entry)
{
  if(!entry) {
    return false;
  }

  if(poisoned_variables.find(entry->GetName()) != poisoned_variables.end()) {
    return true;
  }

  return entry->GetType() && entry->GetType()->GetType() == VAR_TYPE && diagnostics.HasErrors();
}

/****************************
 * The methods of the receiver
 * that a misspelled call could mean
 ****************************/
std::wstring ContextAnalyzer::MethodSuggestion(const std::wstring& name)
{
  Class* receiver_class = nullptr;
  LibraryClass* receiver_lib = nullptr;
  DiagReceiver(receiver_class, receiver_lib);

  std::vector<std::wstring> names;
  if(receiver_class) {
    const std::vector<Method*> methods = receiver_class->GetMethods();
    for(size_t i = 0; i < methods.size(); ++i) {
      names.push_back(MethodNameOf(methods[i]->GetName()));
    }
  }
  else if(receiver_lib) {
    const std::map<const std::wstring, LibraryMethod*>& methods = receiver_lib->GetMethods();
    for(std::map<const std::wstring, LibraryMethod*>::const_iterator iter = methods.begin(); iter != methods.end(); ++iter) {
      names.push_back(MethodNameOf(iter->first));
    }
  }

  return FormatSuggestion(name, CloseNames(name, names));
}

/****************************
 * How a type reads in a message
 ****************************/
std::wstring ContextAnalyzer::DescribeType(Type* type)
{
  if(!type) {
    return L"?";
  }

  std::wstring text;
  switch(type->GetType()) {
  case NIL_TYPE: text = L"Nil"; break;
  case BOOLEAN_TYPE: text = L"Bool"; break;
  case BYTE_TYPE: text = L"Byte"; break;
  case CHAR_TYPE: text = L"Char"; break;
  case INT_TYPE: text = L"Int"; break;
  case FLOAT_TYPE: text = L"Float"; break;
  case CLASS_TYPE: text = PlainTypeName(FormatTypeString(type->GetName())); break;
  case FUNC_TYPE: text = L"function"; break;
  default: text = L"?"; break;
  }

  for(int i = 0; i < type->GetDimension(); ++i) {
    text += L"[]";
  }

  return text;
}

/****************************
 * A failed call: how it was made,
 * and what the receiver offers
 ****************************/
std::wstring ContextAnalyzer::CallDetail(MethodCall* method_call)
{
  const std::wstring name = method_call->GetMethodName();
  if(name.empty()) {
    return L"";
  }

  Class* receiver_class = nullptr;
  LibraryClass* receiver_lib = nullptr;
  DiagReceiver(receiver_class, receiver_lib);

  std::wstring receiver;
  if(receiver_class) {
    receiver = PlainTypeName(receiver_class->GetName());
  }
  else if(receiver_lib) {
    receiver = PlainTypeName(receiver_lib->GetName());
  }

  // how it was called
  std::wstring args;
  ExpressionList* params = method_call->GetCallingParameters();
  if(params) {
    const std::vector<Expression*> expressions = params->GetExpressions();
    // a generated call can pass thousands: name the first few
    const size_t shown = (std::min)(expressions.size(), (size_t)8);
    for(size_t i = 0; i < shown; ++i) {
      args += (i > 0 ? L", " : L"") + DescribeType(expressions[i]->GetEvalType());
    }
    if(expressions.size() > shown) {
      args += L", ... (" + std::to_wstring(expressions.size()) + L" arguments)";
    }
  }

  std::wstring detail = L"\n\tCalled as " + (receiver.empty() ? L"" : receiver + L"->") + name + L"(" + args + L")";

  // the overloads the receiver does have under that name
  std::vector<std::wstring> overloads;
  if(receiver_class) {
    // the receiver's own methods and those it inherits from the program's classes
    for(Class* owner = receiver_class; owner; owner = owner->GetParent()) {
      const std::vector<Method*> methods = owner->GetMethods();
      for(size_t i = 0; i < methods.size(); ++i) {
        if(MethodNameOf(methods[i]->GetName()) == name) {
          overloads.push_back(methods[i]->GetUserName());
        }
      }
    }
  }
  else if(receiver_lib) {
    const std::map<const std::wstring, LibraryMethod*>& methods = receiver_lib->GetMethods();
    for(std::map<const std::wstring, LibraryMethod*>::const_iterator iter = methods.begin(); iter != methods.end(); ++iter) {
      if(MethodNameOf(iter->first) == name) {
        overloads.push_back(iter->second->GetUserName());
      }
    }
  }

  if(!overloads.empty()) {
    detail += L"\n\t" + (receiver.empty() ? L"The" : receiver) + L" has " + std::to_wstring(overloads.size()) +
              (overloads.size() == 1 ? L" method" : L" methods") + L" named '" + name + L"', none of which takes those arguments:";
    for(size_t i = 0; i < overloads.size() && i < 6; ++i) {
      detail += L"\n\t\t" + overloads[i];
    }
    if(overloads.size() > 6) {
      detail += L"\n\t\t...";
    }
  }
  else if(!receiver.empty()) {
    detail += L"\n\t" + receiver + L" has no method named '" + name + L"'" + MethodSuggestion(name);
  }

  return detail;
}

// what a value in a JSON literal can be (see Parser::ParseJsonObject)
static const wchar_t* const JSON_VALUE_KINDS = L"a String, Int, Float, Bool, Nil, JsonElement, or a nested { } or [ ]";

/****************************
 * Makes an error specific from what
 * the analyzer knows when it raises it
 ****************************/
std::wstring ContextAnalyzer::Elaborate(ParseNode* node, const std::wstring& msg)
{
  if(msg.find(L"Did you mean") != std::wstring::npos) {
    return msg;
  }

  // A Char or Byte among the values of a JSON literal matches the JsonElement
  // overload of the literal's Insert or Add and then fails that cast. The call
  // is the compiler's: the message is about the value.
  const std::wstring json_cast = L"Invalid cast with classes: Data.JSON.JsonElement and ";
  if(StartsWith(msg, json_cast.c_str()) && diag_call && StartsWith(diag_call->GetVariableName(), L"#json_") && diag_call->GetCallingParameters()) {
    const std::vector<Expression*> args = diag_call->GetCallingParameters()->GetExpressions();
    for(size_t i = 0; i < args.size(); ++i) {
      if(node == static_cast<ParseNode*>(args[i])) {
        const std::wstring found = PlainTypeName(msg.substr(json_cast.size(), msg.find(L'\n') == std::wstring::npos ? std::wstring::npos : msg.find(L'\n') - json_cast.size()));
        return std::wstring(diag_call->GetMethodName() == L"Add" ? L"An element of a JSON array is " : L"The value of a JSON member is ") +
          JSON_VALUE_KINDS + L"; this one is " + found;
      }
    }
  }

  // Only the first few errors are ever shown, and a suggestion searches every name in scope:
  // past this many a file of undefined names would spend its time on text nobody reads.
  if(diagnostics.ErrorCount() >= 200) {
    return msg;
  }

  if(StartsWith(msg, L"Undefined variable: '")) {
    return msg + VariableSuggestion(QuotedName(msg));
  }

  if(StartsWith(msg, L"Undefined class: '") || StartsWith(msg, L"Undefined class or interface: '") ||
     StartsWith(msg, L"Undefined class or enum: '")) {
    // the class exists in a bundle that is not in use: that is the answer, not a lookalike
    if(msg.find(L"is defined in bundle") != std::wstring::npos) {
      return msg;
    }
    return msg + ClassSuggestion(QuotedName(msg));
  }

  // This call was reported as ambiguous between boxed overloads
  // (ProcessAmbiguousCall). The paths that go on to look for it elsewhere end
  // by saying that no method takes its arguments, which is not so: they say
  // the same thing again instead, and the repeat is dropped.
  if(StartsWith(msg, L"Undefined function/method call: '") && ambiguous_call &&
     node == static_cast<ParseNode*>(static_cast<Expression*>(ambiguous_call))) {
    return ambiguous_call_error;
  }

  if(StartsWith(msg, L"Undefined function/method call: '") && diag_call &&
     node == static_cast<ParseNode*>(static_cast<Expression*>(diag_call))) {
    std::wstring head = msg;
    while(!head.empty() && head[head.size() - 1] == L'\n') {
      head.erase(head.size() - 1);
    }
    return head + CallDetail(diag_call);
  }

  // an operator between a String and a number: say how to convert
  if(StartsWith(msg, L"Invalid operation '") && msg.find(L'\n') == std::wstring::npos) {
    const size_t between = msg.find(L"' between types: ");
    if(between != std::wstring::npos) {
      const std::wstring op = msg.substr(19, between - 19);
      const std::wstring rest = msg.substr(between + 17);
      const size_t split = rest.find(L" and ");
      if(split != std::wstring::npos) {
        const std::wstring left = PlainTypeName(rest.substr(0, split));
        const std::wstring right = PlainTypeName(rest.substr(split + 5));
        const bool left_text = left == L"String";
        const bool right_text = right == L"String";
        const bool left_number = left == L"Int" || left == L"Float" || left == L"Char" || left == L"Byte";
        const bool right_number = right == L"Int" || right == L"Float" || right == L"Char" || right == L"Byte";
        if((left_text && right_number) || (left_number && right_text)) {
          const std::wstring number = left_text ? right : left;
          const std::wstring article = number == L"Int" ? L"an " : L"a ";
          if(op == L"+") {
            return msg + L"\n\t'+' cannot add a String and " + article + number +
                   L". To join them as text, convert the number: value->ToString(); to add numbers, convert the string: text->ToInt() or text->ToFloat()";
          }
          if(op == L"=" || op == L"<>" || op == L"<" || op == L">" || op == L"<=" || op == L">=") {
            return msg + L"\n\t'" + op + L"' cannot compare a String with " + article + number +
                   L". Convert one side to the other's type first: text->ToInt(), text->ToFloat() or number->ToString()";
          }
          return msg + L"\n\t'" + op + L"' cannot be applied to a String and " + article + number +
                 L". Convert the string with text->ToInt() or text->ToFloat(), or the number with number->ToString()";
        }
      }
    }
  }

  // a type mismatch says what was being assigned or returned
  if(cast_kind != 0 && StartsWith(msg, L"Invalid cast with classes: ") && msg.find(L'\n') == std::wstring::npos) {
    const std::wstring rest = msg.substr(27);
    const size_t split = rest.find(L" and ");
    if(split != std::wstring::npos) {
      const std::wstring left = PlainTypeName(rest.substr(0, split));
      const std::wstring right = PlainTypeName(rest.substr(split + 5));
      std::wstring detail;
      // an arm of a 'match' or 'select' used as a value is assigned to a hidden
      // local: the message is about the arms, not about a variable nobody wrote
      const bool is_value_arm = cast_kind == 1 && (StartsWith(cast_subject, L"#match_") || StartsWith(cast_subject, L"#select_"));
      if(is_value_arm) {
        const std::wstring keyword = StartsWith(cast_subject, L"#match_") ? L"match" : L"select";
        detail = L"The arms of a '" + keyword + L"' used as a value agree in type: this one is " + right + L", an earlier one is " + left;
      }
      else if(cast_kind == 1) {
        detail = L"\n\tCannot assign a value of type " + right + L" to '" + cast_subject + L"', which is " + left + L".";
      }
      else {
        detail = L"\n\t" + cast_subject + L" is declared to return " + left + L", but this returns " + right + L".";
      }

      // the conversions the language offers between the common types
      if(left == L"Int" && right == L"String") {
        detail += L"\n\tConvert the string explicitly, as in value->ToInt()";
      }
      else if(left == L"Float" && right == L"String") {
        detail += L"\n\tConvert the string explicitly, as in value->ToFloat()";
      }
      else if(left == L"String" && (right == L"Int" || right == L"Float")) {
        detail += L"\n\tConvert the number explicitly, as in value->ToString()";
      }
      else if((left == L"Int" && right == L"Float") || (left == L"Float" && right == L"Int")) {
        detail += L"\n\tConvert explicitly, as in value->As(" + left + L")";
      }

      return is_value_arm ? detail : msg + detail;
    }
  }

  return msg;
}

/****************************
 * The identifier behind an expression, when it is a plain variable reference.
 * Empty for anything else, so a caller can fall back to a type-only message.
 ****************************/
static std::wstring NamedVariable(Expression* expr)
{
  if(expr && expr->GetExpressionType() == VAR_EXPR) {
    return static_cast<Variable*>(expr)->GetName();
  }

  return L"";
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
 * A member or an element of a JSON literal that no 'Insert' or 'Add' of
 * JsonElement takes. The call on the literal's hidden local is the compiler's;
 * the error is about the name or value the programmer wrote, and is reported
 * there. False when the call is not a JSON literal's.
 ****************************/
bool ContextAnalyzer::ReportJsonLiteralValue(MethodCall* method_call)
{
  if(!StartsWith(method_call->GetVariableName(), L"#json_") || !method_call->GetCallingParameters()) {
    return false;
  }

  // the type at the end of a call chain is the value's
  auto value_type = [](Expression* expression) {
    while(expression->GetMethodCall()) {
      expression = expression->GetMethodCall();
    }
    return expression->GetCastType() ? expression->GetCastType() : expression->GetEvalType();
  };

  alt_error_method_names.clear();
  const std::wstring kinds = JSON_VALUE_KINDS;
  const std::vector<Expression*> args = method_call->GetCallingParameters()->GetExpressions();
  if(method_call->GetMethodName() == L"Insert" && args.size() == 2) {
    Type* name_type = value_type(args[0]);
    if(!name_type || name_type->GetType() != CLASS_TYPE || name_type->GetDimension() > 0 || name_type->GetName() != L"System.String") {
      ProcessError(args[0], L"The name of a JSON member is a String; this one is " + DescribeType(name_type));
    }
    else {
      ProcessError(args[1], L"The value of a JSON member is " + kinds + L"; this one is " + DescribeType(value_type(args[1])));
    }
    return true;
  }

  if(method_call->GetMethodName() == L"Add" && args.size() == 1) {
    ProcessError(args[0], L"An element of a JSON array is " + kinds + L"; this one is " + DescribeType(value_type(args[0])));
    return true;
  }

  return false;
}

/****************************
 * An entry of a map literal whose key or value the map does not take. The
 * 'Insert' on the literal's hidden local is the compiler's; the error is
 * about the entry the programmer wrote. False when the call is not a map
 * literal's.
 ****************************/
bool ContextAnalyzer::ReportMapLiteralEntry(MethodCall* method_call)
{
  if(!StartsWith(method_call->GetVariableName(), L"#map_") || method_call->GetMethodName() != L"Insert" || !method_call->GetCallingParameters()) {
    return false;
  }

  const std::vector<Expression*> args = method_call->GetCallingParameters()->GetExpressions();
  if(args.size() != 2) {
    return false;
  }

  // the type at the end of a call chain is the value's
  auto value_type = [](Expression* expression) {
    while(expression->GetMethodCall()) {
      expression = expression->GetMethodCall();
    }
    return expression->GetCastType() ? expression->GetCastType() : expression->GetEvalType();
  };

  alt_error_method_names.clear();
  std::wstring wanted;
  SymbolEntry* entry = GetEntry(method_call->GetVariableName());
  if(entry && entry->GetType()) {
    const std::vector<Type*> generics = entry->GetType()->GetGenerics();
    if(generics.size() == 2) {
      wanted = L"An entry of this map literal is " + DescribeType(generics[0]) + L": " + DescribeType(generics[1]) + L"; ";
    }
    else {
      wanted = L"An entry of this map literal is Int: Int; ";
    }
  }
  ProcessError(static_cast<Expression*>(method_call), wanted + L"this one is " + DescribeType(value_type(args[0])) + L": " + DescribeType(value_type(args[1])));

  return true;
}

/****************************
 * More than one method takes a
 * call's arguments once one is
 * boxed or unboxed, and none fits
 * better than the rest
 ****************************/
void ContextAnalyzer::ProcessAmbiguousCall(MethodCall* method_call, const std::vector<std::wstring> &names)
{
  std::wstring message = L"Ambiguous function/method call: '" + method_call->GetMethodName() +
    L"(..)' boxes or unboxes an argument to fit, and more than one method fits as well"
    L"\n\tWrite the conversion ('IntRef->New(..)' or '->Get()') to choose one of:\n";
  for(size_t i = 0; i < names.size(); ++i) {
    message += L"\t\t" + names[i] + L'\n';
  }

  ambiguous_call = method_call;
  ambiguous_call_error = message;
  ProcessError(static_cast<Expression*>(method_call), message);
}

/****************************
 * Emits an error
 ****************************/
void ContextAnalyzer::ProcessWarning(ParseNode* node, const std::wstring& msg)
{
  diagnostics.Warning(node->GetFileName(), node->GetLineNumber(), node->GetLinePosition(), msg);
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
  if(diagnostics.HasErrors()) {
    // Repeats are dropped before anything is shown (DiagnosticSink).
    const std::vector<Diagnostic> unique_errors = diagnostics.UniqueErrors();
    for(size_t i = 0; i < unique_errors.size(); ++i) {
#if defined(_DIAG_LIB) || defined(_MODULE)
      error_diagnostics.push_back(unique_errors[i]);
#else
      std::wcerr << unique_errors[i].Format() << std::endl;
#endif
    }

#ifdef _DIAG_LIB
    program->SetErrorDiagnostics(error_diagnostics);
#endif

    status = false;
  }

#ifndef _MODULE
  // check and process warnings
  if(diagnostics.HasWarnings()) {
    const std::vector<Diagnostic> unique_warnings = diagnostics.UniqueWarnings();
    for(size_t i = 0; i < unique_warnings.size(); ++i) {
#ifdef _DIAG_LIB
      warning_diagnostics.push_back(unique_warnings[i]);
#else
      std::wcerr << unique_warnings[i].Format() << std::endl;
#endif
    }

#ifdef _DIAG_LIB
    program->SetWarningDiagnostics(warning_diagnostics);
#endif
  }
#endif

  return status;
}

#ifdef _MODULE
std::vector<std::wstring> ContextAnalyzer::GetErrors()
{
  std::vector<std::wstring> lines;
  for(size_t i = 0; i < error_diagnostics.size(); ++i) {
    lines.push_back(error_diagnostics[i].Format());
  }

  return lines;
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

  // Analysis allocates types and nodes (generic instantiations, inferred types,
  // synthesized method calls) that belong to this program, not to whichever one
  // another thread happens to be analyzing (issue #659).
  ScopedProgramFactories scoped_factories(program);
  int class_id = 0;

  // The LSP diags layer re-runs Analyze on every request over the same
  // ParsedProgram. ScopeTable::child_pos advances during scope traversal
  // and never resets — without this, the second walk's NewScope() returns
  // nullptr and lookups silently fail. Reset every cached SymbolTable's
  // iter_ptr now. (This is necessary but not sufficient on its own — the
  // diags layer also needs to keep the previous ContextAnalyzer alive
  // across requests so that Linker-owned LibraryClass/LibraryMethod
  // pointers in the AST don't dangle.)
  std::vector<ParsedBundle*> reset_bundles = program->GetBundles();
  for(size_t i = 0; i < reset_bundles.size(); ++i) {
    SymbolTableManager* mgr = reset_bundles[i]->GetSymbolTableManager();
    if(mgr) {
      mgr->ResetAllIterations();
    }
  }

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
      std::wstring message = L"Bundle name '" + name + L"' not defined in program or linked libraries";

      // name the library that would supply it, as an unresolved class already does
      std::vector<std::wstring> supplying;
      try {
        supplying = linker->LibrariesDefiningBundle(name);
      }
      catch(const CompilerFatal&) {
        // a stale library in the path cannot be probed; the undefined bundle is still reported
      }

      if(!supplying.empty()) {
        message += L"\n\tAdd it with '-lib ";
        for(size_t j = 0; j < supplying.size(); ++j) {
          if(j) {
            message += L',';
          }
          message += supplying[j];
        }
        message += L"' (or to \"libs\" in build.json)";
      }

      ProcessError(file_name, message);
    }
  }

  // an alias's member types, in the alias's own bundle
  std::vector<ParsedBundle*> alias_bundles = program->GetBundles();
  for(size_t i = 0; i < alias_bundles.size(); ++i) {
    std::vector<Alias*> aliases = alias_bundles[i]->GetAliases();
    for(size_t j = 0; j < aliases.size(); ++j) {
      ResolveAliasTypes(aliases[j]);
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
        GenerateNamedForwarders(bundle, klass, methods[k]);
        // a library's callers name its arguments through one mask forwarder (#1244)
        if(is_lib) {
          GenerateMaskForwarder(bundle, klass, methods[k]);
        }
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
        // 'from Base' is written in this class: its scope decides an ambiguous name
        ResolveSiteScope site_scope(resolve_site, klass);
        current_class = klass;
        Class* parent = SearchProgramClasses(parent_name);
        current_class = nullptr;
        if(parent) {
          klass->SetParent(parent);
          parent->AddChild(klass);
        }
        else {
          LibraryClass* lib_parent = SearchLibraryClasses(parent_name, program->GetLibUses(klass->GetFileName()));
          if(lib_parent) {
            klass->SetLibraryParent(lib_parent);
            lib_parent->AddChild(klass);
          }
          else {
            ProcessError(klass, L"Cannot inherit from an undefined class type");
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
  ResolveSiteScope site_scope(resolve_site, eenum);
#ifdef _DEBUG
  std::wstring msg = L"[enum: name='" + eenum->GetName() + L"']";
  Debug(msg, eenum->GetLineNumber(), depth);
#endif

  // enums are analyzed before any class, so there is no current class: use the enum's own file
  if(!SearchProgramEnums(eenum->GetName()) &&
     !SearchLibraryEnums(eenum->GetName(), program->GetLibUses(eenum->GetFileName()))) {
    ProcessError(eenum, L"Undefined enum: '" + FormatTypeString(eenum->GetName()) + L"'"
                 L"\n\tEnsure the enum is declared or add a 'use' statement for external bundles.");
  }

  if(SearchLibraryClasses(eenum->GetName(), program->GetLibUses(eenum->GetFileName())) ||
     SearchLibraryEnums(eenum->GetName(), program->GetLibUses(eenum->GetFileName()))) {
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
            ProcessFatalError(declaration, L"Internal compiler error: Invalid entry name");
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
  // find initial parameter offset: the first parameter with a default, which may be the
  // first parameter (so 0 cannot mean 'not found', or 'F()' is never generated)
  std::vector<Declaration*> declarations = method->GetDeclarations()->GetDeclarations();
  size_t inital_param_offset = declarations.size();
  for(size_t i = 0; i < declarations.size(); ++i) {
    if(declarations[i]->GetAssignment()) {
      inital_param_offset = i;
      break;
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
 * A library method with two or
 * more default values gets a
 * mask forwarder, 'F$m' (#1244,
 * phase 2): every parameter, then
 * an Int whose bit j says the
 * caller gave parameter j. It
 * assigns each default the caller
 * skipped, in the callee's context,
 * and calls the original. One per
 * method, not one per subset: a
 * caller in another binary cannot
 * generate its own.
 ****************************/
void ContextAnalyzer::GenerateMaskForwarder(ParsedBundle* bundle, Class* klass, Method* method)
{
  if(method->IsAlt() || method->IsVirtual()) {
    return;
  }

  std::vector<Declaration*> declarations = method->GetDeclarations()->GetDeclarations();
  int defaults = 0;
  for(size_t i = 0; i < declarations.size(); ++i) {
    if(declarations[i]->GetAssignment()) {
      defaults++;
    }
  }
  if(defaults < 2 || declarations.size() > 62) {
    return;
  }

  const std::wstring forwarder_name = method->GetName() + L"$m";
  Method* forwarder = TreeFactory::Instance()->MakeMethod(method->GetFileName(), method->GetLineNumber(), method->GetLinePosition(),
                                                          method->GetEndLineNumber(), method->GetEndLinePosition(),
                                                          forwarder_name, method->GetMethodType(), method->IsStatic(), method->IsNative());
  forwarder->SetReturn(method->GetReturn());

  DeclarationList* forwarder_declarations = TreeFactory::Instance()->MakeDeclarationList();
  StatementList* forwarder_statements = TreeFactory::Instance()->MakeStatementList();
  std::vector<Declaration*> forwarded_declarations;
  std::vector<Assignment*> forwarded_defaults(declarations.size(), nullptr);

  bundle->GetSymbolTableManager()->NewParseScope();

  // every parameter, scoped to the forwarder so its body can read and set them
  const std::wstring file_name = method->GetFileName();
  const int line_num = method->GetLineNumber();
  const int line_pos = method->GetLinePosition();
  for(size_t i = 0; i < declarations.size(); ++i) {
    const std::wstring scoped = declarations[i]->GetEntry()->GetName();
    const size_t start = scoped.find_last_of(L':');
    const std::wstring bare = start == std::wstring::npos ? scoped : scoped.substr(start + 1);

    SymbolEntry* entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, forwarder_name + L':' + bare,
                                                                  TypeFactory::Instance()->MakeType(declarations[i]->GetEntry()->GetType()),
                                                                  false, true);
    Declaration* declaration = TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, line_num, line_pos,
                                                                        entry, (Declaration*)nullptr, (Assignment*)nullptr);
    forwarder_declarations->AddDeclaration(declaration);
    bundle->GetSymbolTableManager()->CurrentParseScope()->AddEntry(entry);
    forwarded_declarations.push_back(declaration);
  }

  // the mask
  SymbolEntry* mask_entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, forwarder_name + L":$mask",
                                                                     TypeFactory::Instance()->MakeType(INT_TYPE), false, true);
  forwarder_declarations->AddDeclaration(TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, line_num, line_pos,
                                                                                  mask_entry, (Declaration*)nullptr, (Assignment*)nullptr));
  bundle->GetSymbolTableManager()->CurrentParseScope()->AddEntry(mask_entry);

  // 'if(($mask and bit) = 0) { p := default; }' for each parameter with a default
  for(size_t i = 0; i < declarations.size(); ++i) {
    Assignment* original = declarations[i]->GetAssignment();
    if(!original) {
      continue;
    }

    const std::wstring scoped = declarations[i]->GetEntry()->GetName();
    const size_t start = scoped.find_last_of(L':');
    const std::wstring bare = start == std::wstring::npos ? scoped : scoped.substr(start + 1);

    Variable* mask = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, L"$mask");
    IntegerLiteral* bit = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, ((INT64_VALUE)1) << i);
    CalculatedExpression* masked = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, BIT_AND_EXPR, mask, bit);
    IntegerLiteral* zero = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 0);
    CalculatedExpression* skipped = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, EQL_EXPR, masked, zero);

    Variable* target = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, bare);
    Assignment* assignment = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, line_num, line_pos,
                                                                     target, original->GetExpression());
    // the if's block is a scope of its own, as the parser would make it: the
    // analyzer walks a method's scopes in the order they were made
    bundle->GetSymbolTableManager()->CurrentParseScope()->NewParseScope();
    StatementList* then_statements = TreeFactory::Instance()->MakeStatementList();
    then_statements->AddStatement(assignment);
    bundle->GetSymbolTableManager()->CurrentParseScope()->PreviousParseScope();
    forwarder_statements->AddStatement(TreeFactory::Instance()->MakeIf(file_name, line_num, line_pos, line_num, line_pos,
                                                                       skipped, then_statements));
  }

  forwarder->SetStatements(forwarder_statements);
  forwarder->SetDeclarations(forwarder_declarations);
  forwarder->SetOriginal(method);
  forwarder->SetForwardedParameters(forwarded_declarations, forwarded_defaults);
  bundle->GetSymbolTableManager()->PreviousParseScope(forwarder->GetParsedName());

  // one forwarder per original method, so its parsed name cannot repeat; a refusal here is a
  // compiler fault and would otherwise leave callers with a missing method
  if(!klass->AddMethod(forwarder)) {
    ProcessError(method, L"Mask forwarder already defined for '" + method->GetUserName() + L"'");
  }
}

/****************************
 * A parameter's name as written:
 * entries are scoped, e.g.
 * 'Class:Method:name'
 ****************************/
static std::wstring BareParameterName(Declaration* declaration)
{
  if(!declaration || !declaration->GetEntry()) {
    return L"";
  }
  const std::wstring name = declaration->GetEntry()->GetName();
  const size_t start = name.find_last_of(L':');
  return start == std::wstring::npos ? name : name.substr(start + 1);
}

/****************************
 * The method's parameter names,
 * for an error message
 ****************************/
std::wstring ContextAnalyzer::ParameterNames(Method* method)
{
  std::wstring names;
  std::vector<Declaration*> declarations = method->GetDeclarations()->GetDeclarations();
  for(size_t i = 0; i < declarations.size(); ++i) {
    if(i > 0) {
      names += L", ";
    }
    names += BareParameterName(declarations[i]);
  }
  return names.empty() ? L"none" : names;
}

/****************************
 * Maps a call's argument shape
 * ("" for each positional argument,
 * then the names written) onto a
 * method's parameters: 'order' is
 * the parameter each argument binds
 * to, in written order (#1244)
 ****************************/
bool ContextAnalyzer::MapNamedArguments(Method* method, const std::vector<std::wstring> &shape, std::vector<size_t> &order, std::wstring &error)
{
  std::vector<Declaration*> declarations = method->GetDeclarations()->GetDeclarations();
  std::vector<bool> given(declarations.size(), false);
  order.clear();

  for(size_t i = 0; i < shape.size(); ++i) {
    size_t index = declarations.size();
    if(shape[i].empty()) {
      if(i >= declarations.size()) {
        error = L"Too many arguments for '" + method->GetUserName() + L"'";
        return false;
      }
      index = i;
    }
    else {
      for(size_t j = 0; j < declarations.size(); ++j) {
        if(BareParameterName(declarations[j]) == shape[i]) {
          index = j;
          break;
        }
      }
      if(index == declarations.size()) {
        error = L"No parameter named '" + shape[i] + L"'; the parameters are: " + ParameterNames(method);
        return false;
      }
    }

    if(given[index]) {
      error = L"Parameter '" + BareParameterName(declarations[index]) + L"' is given both by position and by name";
      return false;
    }
    given[index] = true;
    order.push_back(index);
  }

  for(size_t j = 0; j < declarations.size(); ++j) {
    if(!given[j] && !declarations[j]->GetAssignment()) {
      error = L"Parameter '" + BareParameterName(declarations[j]) + L"' has no default value and is not given";
      return false;
    }
  }

  return true;
}

/****************************
 * The forwarder a named call
 * binds to: a name no source can
 * spell, so it never collides
 * with an overload
 ****************************/
std::wstring ContextAnalyzer::NamedForwarderName(const std::wstring &name, const std::vector<size_t> &order)
{
  std::wstring forwarder = name + L"$n";
  for(size_t i = 0; i < order.size(); ++i) {
    if(i > 0) {
      forwarder += L'_';
    }
    forwarder += ToString((int)order[i]);
  }
  return forwarder;
}

/****************************
 * Generates a forwarder for each
 * named-call shape this method
 * accepts (#1244). It takes the
 * arguments in written order,
 * assigns the defaults of those
 * not given in the callee's
 * context -- as a default
 * alternate does -- and calls
 * the original.
 ****************************/
void ContextAnalyzer::GenerateNamedForwarders(ParsedBundle* bundle, Class* klass, Method* method)
{
  if(method->IsAlt() || method->IsVirtual()) {
    return;
  }

  const std::wstring full_name = method->GetName();
  const size_t start = full_name.find(L':');
  const std::wstring name = start == std::wstring::npos ? full_name : full_name.substr(start + 1);

  const std::set<std::vector<std::wstring> > shapes = program->GetNamedCallShapes(name);
  for(std::set<std::vector<std::wstring> >::const_iterator iter = shapes.begin(); iter != shapes.end(); ++iter) {
    std::vector<size_t> order;
    std::wstring error;
    if(!MapNamedArguments(method, *iter, order, error)) {
      continue;
    }

    std::vector<Declaration*> declarations = method->GetDeclarations()->GetDeclarations();
    Method* forwarder = TreeFactory::Instance()->MakeMethod(method->GetFileName(), method->GetLineNumber(), method->GetLinePosition(),
                                                            method->GetEndLineNumber(), method->GetEndLinePosition(),
                                                            NamedForwarderName(full_name, order), method->GetMethodType(),
                                                            method->IsStatic(), method->IsNative());
    forwarder->SetReturn(method->GetReturn());

    DeclarationList* forwarder_declarations = TreeFactory::Instance()->MakeDeclarationList();
    StatementList* forwarder_statements = TreeFactory::Instance()->MakeStatementList();
    std::vector<Declaration*> forwarded_declarations(declarations.size(), nullptr);
    std::vector<Assignment*> forwarded_defaults(declarations.size(), nullptr);

    bundle->GetSymbolTableManager()->NewParseScope();

    // the arguments, in the order the call writes them
    std::vector<bool> given(declarations.size(), false);
    for(size_t i = 0; i < order.size(); ++i) {
      Declaration* declaration = declarations[order[i]]->Copy();
      forwarder_declarations->AddDeclaration(declaration);
      bundle->GetSymbolTableManager()->CurrentParseScope()->AddEntry(declaration->GetEntry());
      forwarded_declarations[order[i]] = declaration;
      given[order[i]] = true;
    }

    // the defaults of the rest, evaluated here as a default alternate does
    for(size_t j = 0; j < declarations.size(); ++j) {
      if(!given[j]) {
        Declaration* declaration = declarations[j]->Copy();
        // a fresh assignment and variable: Copy() shares the original's, so the
        // default alternates and every forwarder would bind one variable, and a
        // forwarder -- whose slots differ -- would store and load the default
        // through another method's entry. The default expression stays shared,
        // as it is between the alternates.
        Assignment* shared = declaration->GetAssignment();
        Variable* shared_variable = shared->GetVariable();
        Variable* variable = TreeFactory::Instance()->MakeVariable(shared_variable->GetFileName(), shared_variable->GetLineNumber(),
                                                                   shared_variable->GetLinePosition(), shared_variable->GetName());
        Assignment* assignment = TreeFactory::Instance()->MakeAssignment(shared->GetFileName(), shared->GetLineNumber(), shared->GetLinePosition(),
                                                                         shared->GetEndLineNumber(), shared->GetEndLinePosition(),
                                                                         variable, shared->GetExpression());
        assignment->GetVariable()->WasAlt();
        assignment->GetExpression()->SetEvalType(declaration->GetEntry()->GetType(), true);
        forwarder_statements->AddStatement(assignment);
        forwarded_defaults[j] = assignment;
      }
    }

    forwarder->SetStatements(forwarder_statements);
    forwarder->SetDeclarations(forwarder_declarations);
    forwarder->SetOriginal(method);
    forwarder->SetForwardedParameters(forwarded_declarations, forwarded_defaults);
    bundle->GetSymbolTableManager()->PreviousParseScope(forwarder->GetParsedName());

    // overloads with the same parameter types under one shape are one forwarder: a
    // refused duplicate is expected here, not an error to report
    (void)klass->AddMethod(forwarder);
  }
}

/****************************
 * Binds a call with named
 * arguments to the forwarder
 * generated for its shape
 * (#1244). False, with an error
 * reported, when no overload
 * takes it.
 ****************************/
bool ContextAnalyzer::BindNamedArguments(Class* klass, MethodCall* method_call)
{
  ExpressionList* call_params = method_call->GetCallingParameters();
  const std::vector<std::wstring> shape = call_params->GetNames();
  const std::wstring name = method_call->GetMethodName();

  std::set<std::wstring> forwarders;
  std::wstring first_error;
  bool virtual_target = false;
  std::vector<Method*> candidates = klass->GetAllUnqualifiedMethods(name);
  // not a method of this class (a function value, say): the call's own path
  // reports the named arguments it cannot take
  if(candidates.empty()) {
    return true;
  }

  for(size_t i = 0; i < candidates.size(); ++i) {
    Method* candidate = candidates[i];
    if(candidate->IsAlt()) {
      continue;
    }

    std::vector<size_t> order;
    std::wstring error;
    if(MapNamedArguments(candidate, shape, order, error)) {
      if(candidate->IsVirtual()) {
        virtual_target = true;
      }
      else {
        forwarders.insert(NamedForwarderName(name, order));
      }
    }
    else if(first_error.empty()) {
      first_error = error;
    }
  }

  call_params->ClearNames();
  if(forwarders.empty()) {
    if(virtual_target) {
      ProcessError(static_cast<Expression*>(method_call), L"Named arguments are not supported for virtual methods yet");
    }
    else if(!first_error.empty()) {
      ProcessError(static_cast<Expression*>(method_call), first_error);
    }
    else {
      ProcessError(static_cast<Expression*>(method_call), L"Undefined function/method call: '" + name + L"(..)'");
    }
    return false;
  }

  if(forwarders.size() > 1) {
    ProcessError(static_cast<Expression*>(method_call), L"Named arguments match more than one overload of '" + name +
                 L"'; pass the ambiguous ones by position");
    return false;
  }

  method_call->SetMethodName(*forwarders.begin());
  return true;
}

/****************************
 * Analyzes a class
 ****************************/
void ContextAnalyzer::AnalyzeClass(Class* klass, const int id, const int depth)
{
  ResolveSiteScope site_scope(resolve_site, klass);
#ifdef _DEBUG
  std::wstring msg = L"[class: name='" + klass->GetName() + L"'; id=" + ToString(id) +
    L"; virtual=" + (klass->IsVirtual() ? L"true" : L"false") + L"]";
  Debug(msg, klass->GetLineNumber(), depth);
#endif

  current_class = klass;
  current_class->SetCalled(true);

  klass->SetSymbolTable(symbol_table->GetSymbolTable(klass->GetName()));
  if(!HasProgramOrLibraryClass(klass->GetName())) {
    ProcessError(klass, L"Undefined class: '" + klass->GetName() + L"'" + BundleSuggestion(klass->GetName()));
    undefined_classes.insert(klass->GetName());
  }

  if(SearchLibraryClasses(klass->GetName(), program->GetLibUses(klass->GetFileName())) ||
     SearchLibraryEnums(klass->GetName(), program->GetLibUses(klass->GetFileName()))) {
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

void ContextAnalyzer::CheckParent(Class* klass, [[maybe_unused]] const int depth)
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
  ResolveSiteScope site_scope(resolve_site, klass);
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
    AnalyzeVirtualMethods(current_class, current_class->GetParent(), depth);
  }
  else if(current_class->GetLibraryParent() && current_class->GetLibraryParent()->IsVirtual()) {
    AnalyzeVirtualMethods(current_class, current_class->GetLibraryParent(), depth);
  }

  // collect anonymous classes
  if(klass->GetAnonymousCall()) {
    anonymous_classes.push_back(klass);
  }
}

/*****************************************************************
 * Check for generic classes and backing interfaces
 *****************************************************************/
void ContextAnalyzer::AnalyzeGenerics(Class* klass, [[maybe_unused]] const int depth)
{
  const std::vector<Class*> generic_classes = klass->GetGenericClasses();
  for(size_t i = 0; i < generic_classes.size(); ++i) {
    Class* generic_class = generic_classes[i];
    // check generic class
    const std::wstring generic_class_name = generic_class->GetName();
    if(HasProgramOrLibraryClass(generic_class_name)) {
      ProcessError(klass, L"Generic reference '" + generic_class_name + L"' previously defined as a class");
    }
    // check backing interface(s) — every bound of a compound constraint (A & B)
    if(generic_class->HasGenericInterface()) {
      const std::vector<Type*> bounds = generic_class->GetAllGenericInterfaces();
      for(size_t b = 0; b < bounds.size(); ++b) {
        Type* generic_inf_type = bounds[b];
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
}

/****************************
 * Checks for virtual method
 * implementations
 ****************************/
bool ContextAnalyzer::AnalyzeVirtualMethods(Class* impl_class, Class* virtual_class, [[maybe_unused]] const int depth)
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
    ProcessError(impl_class, L"Virtual methods/functions from '" + virtual_class->GetName() +
                 L"' have not been implemented:" + error_msg);
  }

  return success;
}

/****************************
 * Checks for interface
 * implementations
 ****************************/
void ContextAnalyzer::AnalyzeInterfaces(Class* klass, const int depth)
{
  ResolveSiteScope site_scope(resolve_site, klass);
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
      LibraryClass* inf_lib_klass = SearchLibraryClasses(interface_name, program->GetLibUses(current_class->GetFileName()));
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
                                           bool impl_is_static, [[maybe_unused]] bool impl_is_virtual, Method* virtual_method)
{
  // check method types
  const std::wstring virtual_class_name = (virtual_method->GetClass() ? virtual_method->GetClass()->GetName() : impl_class->GetName());
  if(impl_mthd_type != virtual_method->GetMethodType()) {
    ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                 virtual_class_name);
  }
  // check method returns
  Type* virtual_return = virtual_method->GetReturn();
  if(!virtual_return) {
    return;
  }
  if(impl_return->GetType() != virtual_return->GetType()) {
    ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                 virtual_class_name);
  }
  else if(impl_return->GetType() == CLASS_TYPE &&
          impl_return->GetName() != virtual_return->GetName()) {
    Class* impl_cls = SearchProgramClasses(impl_return->GetName());
    Class* virtual_cls = nullptr;
    {
      // the interface's return type, in the interface's bundle
      OtherClassScope other_scope(skip_use_check, bundle, OtherClassBundle(virtual_method->GetClass()));
      virtual_cls = SearchProgramClasses(virtual_return->GetName());
    }
    if(impl_cls && virtual_cls && impl_cls != virtual_cls) {
      LibraryClass* impl_lib_cls = SearchLibraryClasses(impl_return->GetName(),
                                                                program->GetLibUses(current_class->GetFileName()));
      LibraryClass* virtual_lib_cls = SearchLibraryClasses(virtual_return->GetName(),
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
bool ContextAnalyzer::AnalyzeVirtualMethods(Class* impl_class, LibraryClass* lib_virtual_class, [[maybe_unused]] const int depth)
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
          error_msg += L"\n\tMissing: '";
          error_msg += virtual_method->GetUserName();
          error_msg += L'\'';
        }
      }
    }
  }

  const bool success = error_msg.empty();
  if(!success) {
    ProcessError(impl_class, L"Virtual methods/functions from '" + lib_virtual_class->GetName() +
                 L"' have not been implemented:" + error_msg);
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
      LibraryClass* impl_lib_cls = SearchLibraryClasses(impl_return->GetName(),
                                                                program->GetLibUses(current_class->GetFileName()));
      LibraryClass* virtual_lib_cls = SearchLibraryClasses(virtual_return->GetName(),
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
  ResolveSiteScope site_scope(resolve_site, method);
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

    // check for return
    if(method->GetMethodType() != NEW_PUBLIC_METHOD && method->GetMethodType() != NEW_PRIVATE_METHOD &&
       method->GetReturn() && method->GetReturn()->GetType() != NIL_TYPE) {
#ifdef _SYSTEM
      // The system library is exempt only where the check cannot see the
      // return: a trap leaves the method's value itself, and a compiler
      // intrinsic (Base->Try) has no body. It used to be exempt outright, so
      // lang.obs shipped Runtime->CommandOutput(c), Alarm->GetInterval() and
      // UDPSocketServer->Bind() with a return type and no return, and a call
      // to the first crashed.
      const bool exempt = method->GetStatements()->GetStatements().empty() ||
        HasSystemStatement(method->GetStatements());
#else
      const bool exempt = false;
#endif
      // an alternate forwards to the original, which returns; a named-argument
      // forwarder that only reorders has no statements at all (#1244)
      if(!exempt && !method->IsAlt() && !AnalyzeReturnPaths(method->GetStatements(), depth + 1)) {
        ProcessError(method, L"All method/function paths must return a value");
      }

#ifndef _SYSTEM
      AnalyzeDeadReturns(method->GetStatements(), depth + 1);
#endif
    }

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
 * Auto-wraps a bare lambda `\(...) => body` into `FuncRef->New(lambda)<R>`
 * when it appears where a FuncRef<R> is expected (assignment, return, etc.),
 * so the lambda body's return type is inferred from R. Returns the original
 * expression unchanged when no wrapping applies.
 ****************************/
Expression* ContextAnalyzer::WrapBareLambdaInFuncRef(Expression* expression, Type* expected, const int depth)
{
  // the expected type may carry the short ("FuncRef") or qualified
  // ("System.FuncRef") name depending on where it was resolved
  if(expression && expression->GetExpressionType() == LAMBDA_EXPR && expected &&
     expected->GetType() == CLASS_TYPE &&
     (expected->GetName() == L"System.FuncRef" || expected->GetName() == L"FuncRef")) {
    Lambda* lambda = static_cast<Lambda*>(expression);
    // only bare lambdas: no explicit `~ R :` signature and no alias name
    if(!lambda->GetLambdaType() && lambda->GetName().empty()) {
      const std::vector<Type*> generics = expected->GetGenerics();
      if(generics.size() == 1) {
        const std::wstring file_name = lambda->GetFileName();
        const int line_num = lambda->GetLineNumber();
        const int line_pos = lambda->GetLinePosition();

        ExpressionList* args = TreeFactory::Instance()->MakeExpressionList();
        args->AddExpression(lambda);
        MethodCall* wrap = TreeFactory::Instance()->MakeMethodCall(
          file_name, line_num, line_pos, line_num, line_pos, NEW_INST_CALL, L"FuncRef", args);

        std::vector<Type*> concrete;
        concrete.push_back(TypeFactory::Instance()->MakeType(generics[0]));
        wrap->SetConcreteTypes(concrete);
        return wrap;
      }
    }
  }

  return expression;
}

/****************************
 * Method reference without ceremony: `Class->Function` (no argument list, no
 * `~ R`) where a function value is wanted becomes
 * `FuncNRef->New(Class->Function(T1, ...) ~ R)<T1, ..., R>`, the signature read
 * from the function itself. The rewrite is the long form the programmer would
 * have written, so everything after it is the existing, tested path.
 * `expected` is the target type when the context has one (assignment, return)
 * and null for a call argument; with a target it must be a FuncRef class.
 * Anything that is not clearly a reference to one static function of a class
 * is returned unchanged.
 ****************************/
Expression* ContextAnalyzer::WrapMethodReferenceInFuncRef(Expression* expression, Type* expected, const int depth)
{
  if(!expression || expression->GetExpressionType() != METHOD_CALL_EXPR) {
    return expression;
  }

  MethodCall* ref = static_cast<MethodCall*>(expression);
  if(ref->GetCallType() != ENUM_CALL || ref->GetCallingParameters() || ref->IsFunctionDefinition() ||
     ref->GetMethodCall() || ref->GetVariable() || ref->GetCastType() || ref->GetTypeOf() ||
     ref->GetMethodName().empty() || ref->GetVariableName().empty()) {
    return expression;
  }

  if(expected && !(expected->GetType() == CLASS_TYPE && expected->GetDimension() == 0 &&
     (expected->GetName() == L"System.FuncRef" || expected->GetName() == L"FuncRef" ||
      expected->GetName() == L"System.Func2Ref" || expected->GetName() == L"Func2Ref" ||
      expected->GetName() == L"System.Func3Ref" || expected->GetName() == L"Func3Ref" ||
      expected->GetName() == L"System.Func4Ref" || expected->GetName() == L"Func4Ref"))) {
    return expression;
  }

  // the left side must name a class (a variable of that name is a field or local access)
  Class* klass = nullptr;
  LibraryClass* lib_klass = nullptr;
  if(!GetProgramOrLibraryClass(ref->GetVariableName(), klass, lib_klass)) {
    return expression;
  }

  // the candidate functions: those named so, in the class itself
  std::vector<Type*> param_types;
  Type* rtrn_type = nullptr;
  size_t named = 0;
  bool all_static = true;
  if(klass) {
    const std::vector<Method*> methods = klass->GetUnqualifiedMethods(ref->GetMethodName());
    named = methods.size();
    for(Method* method : methods) {
      all_static = all_static && method->IsStatic();
    }
    if(named == 1 && all_static && !methods[0]->IsVirtual() && !methods[0]->IsLambda()) {
      for(Declaration* dclr : methods[0]->GetDeclarations()->GetDeclarations()) {
        param_types.push_back(dclr->GetEntry() ? dclr->GetEntry()->GetType() : nullptr);
      }
      rtrn_type = methods[0]->GetReturn();
    }
  }
  else if(lib_klass) {
    const std::vector<LibraryMethod*> methods = lib_klass->GetUnqualifiedMethods(ref->GetMethodName());
    named = methods.size();
    for(LibraryMethod* method : methods) {
      all_static = all_static && method->IsStatic();
    }
    if(named == 1 && all_static && !methods[0]->IsVirtual()) {
      param_types = methods[0]->GetDeclarationTypes();
      rtrn_type = methods[0]->GetReturn();
    }
  }

  // not a function of that class: an enum item, a constant, ... as before
  if(named == 0 || !all_static) {
    return expression;
  }

  const std::wstring long_form = L"write the full reference: Func2Ref->New(" + ref->GetVariableName() + L"->" +
    ref->GetMethodName() + L"(<types>) ~ <type>)<<types>>";
  if(named > 1) {
    ProcessError(expression, L"'" + ref->GetVariableName() + L"->" + ref->GetMethodName() +
                 L"' is overloaded, so the reference is ambiguous; " + long_form);
    return expression;
  }

  // a FuncRef takes plain class types (the long form cannot spell a generic one
  // either): not primitives, arrays, generics or Nil, and at most 3 arguments
  bool shaped = rtrn_type && param_types.size() <= 3 && rtrn_type->GetType() == CLASS_TYPE && rtrn_type->GetDimension() == 0 &&
    rtrn_type->GetGenerics().empty();
  for(size_t i = 0; shaped && i < param_types.size(); ++i) {
    shaped = param_types[i] && param_types[i]->GetType() == CLASS_TYPE && param_types[i]->GetDimension() == 0 &&
      param_types[i]->GetGenerics().empty();
  }
  if(!shaped) {
    ProcessError(expression, L"Cannot take '" + ref->GetVariableName() + L"->" + ref->GetMethodName() +
                 L"' as a function reference: its arguments and return must be plain classes, not Int/Float/arrays/generics (at most 3 arguments); " + long_form);
    return expression;
  }

  const std::wstring file_name = expression->GetFileName();
  const int line_num = expression->GetLineNumber();
  const int line_pos = expression->GetLinePosition();

  std::vector<Type*> concrete;
  ExpressionList* func_params = TreeFactory::Instance()->MakeExpressionList();
  for(Type* param_type : param_types) {
    Type* copy = TypeFactory::Instance()->MakeType(param_type);
    ResolveClassEnumType(copy);
    Variable* name = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, copy->GetName());
    func_params->AddExpression(name);
    concrete.push_back(copy);
  }
  Type* rtrn_copy = TypeFactory::Instance()->MakeType(rtrn_type);
  ResolveClassEnumType(rtrn_copy);
  concrete.push_back(rtrn_copy);

  MethodCall* func_def = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, line_num, line_pos,
                           line_num, line_pos,
                           ref->GetVariableName(), ref->GetMethodName(), func_params);
  func_def->SetFunctionalReturn(TypeFactory::Instance()->MakeType(rtrn_copy));

  static const wchar_t* const wrappers[] = { L"FuncRef", L"Func2Ref", L"Func3Ref", L"Func4Ref" };
  ExpressionList* args = TreeFactory::Instance()->MakeExpressionList();
  args->AddExpression(func_def);
  MethodCall* wrap = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, line_num, line_pos,
                                                             NEW_INST_CALL, wrappers[param_types.size()], args);
  wrap->SetConcreteTypes(concrete);
  return wrap;
}

/****************************
 * Collection literal: an array literal `[a, b, c]` where a Vector, CompareVector,
 * IntVector or FloatVector is wanted (a declaration or assignment target, a
 * `return`) is `Vector->New([a, b, c])<T>`, the constructor those classes already
 * have for an array. The literal is typed by the target, so `v : Vector<IntRef> := [x, y]`
 * needs neither the `->New(..)<IntRef>` nor an AddBack per element. The result is
 * the long form the programmer would have written; anything else is unchanged.
 ****************************/
Expression* ContextAnalyzer::WrapCollectionLiteral(Expression* expression, Type* expected, const int depth)
{
  if(!expression || expression->GetExpressionType() != STAT_ARY_EXPR || !expected ||
     expected->GetType() != CLASS_TYPE || expected->GetDimension() != 0) {
    return expression;
  }

  const std::wstring name = expected->GetName();
  const size_t dot = name.rfind(L'.');
  const std::wstring bare = dot == std::wstring::npos ? name : name.substr(dot + 1);
  const std::wstring scope = dot == std::wstring::npos ? L"" : name.substr(0, dot);
  if(!(scope.empty() || scope == L"Collection") ||
     !(bare == L"Vector" || bare == L"CompareVector" || bare == L"IntVector" || bare == L"FloatVector")) {
    return expression;
  }

  const bool generic = bare == L"Vector" || bare == L"CompareVector";
  const std::vector<Type*> generics = expected->GetGenerics();
  if(generic != (generics.size() == 1)) {
    return expression;
  }

  // Two things the elements of a literal are not, said here: left to the constructor
  // call the literal stands for, each is that call's error about an array and says
  // nothing of the element that was written. The literal is replaced by Nil so that
  // the one error is all there is.
  if(generic) {
    Type* element_type = TypeFactory::Instance()->MakeType(generics[0]);
    EntryType held = NIL_TYPE;
    const bool holds_primitive = HolderPrimitive(element_type, held);
    const std::wstring& element_name = element_type->GetName();
    const bool holds_function = element_name == L"System.FuncRef" || element_name == L"FuncRef" ||
      element_name == L"System.Func2Ref" || element_name == L"Func2Ref" ||
      element_name == L"System.Func3Ref" || element_name == L"Func3Ref" ||
      element_name == L"System.Func4Ref" || element_name == L"Func4Ref";
    ExpressionList* element_list = static_cast<StaticArray*>(expression)->GetElements();
    const std::vector<Expression*> elements = element_list ? element_list->GetExpressions() : std::vector<Expression*>();
    for(size_t i = 0; (holds_primitive || holds_function) && i < elements.size(); ++i) {
      Expression* element = elements[i];
      if(!element || element->GetMethodCall() || element->GetCastType()) {
        continue;
      }

      if(holds_primitive) {
        // a number, character or boolean: a literal, or a variable declared as one
        Type* primitive = nullptr;
        switch(element->GetExpressionType()) {
        case INT_LIT_EXPR:
          primitive = TypeFactory::Instance()->MakeType(INT_TYPE);
          break;

        case FLOAT_LIT_EXPR:
          primitive = TypeFactory::Instance()->MakeType(FLOAT_TYPE);
          break;

        case CHAR_LIT_EXPR:
          primitive = TypeFactory::Instance()->MakeType(CHAR_TYPE);
          break;

        case BOOLEAN_LIT_EXPR:
          primitive = TypeFactory::Instance()->MakeType(BOOLEAN_TYPE);
          break;

        case VAR_EXPR:
          if(!static_cast<Variable*>(element)->GetIndices()) {
            SymbolEntry* entry = GetEntry(static_cast<Variable*>(element)->GetName());
            if(entry && entry->GetType() && entry->GetType()->GetDimension() == 0) {
              switch(entry->GetType()->GetType()) {
              case BOOLEAN_TYPE:
              case BYTE_TYPE:
              case CHAR_TYPE:
              case INT_TYPE:
              case FLOAT_TYPE:
                primitive = entry->GetType();
                break;

              default:
                break;
              }
            }
          }
          break;

        default:
          break;
        }

        if(primitive) {
          const std::wstring holder = DescribeType(element_type);
          std::wstring plain;
          if(held == INT_TYPE) {
            plain = L", declare the variable as an IntVector, which holds plain Ints";
          }
          else if(held == FLOAT_TYPE) {
            plain = L", declare the variable as a FloatVector, which holds plain Floats";
          }
          ProcessError(element, L"The elements of a collection literal are not boxed: this one is " + DescribeType(primitive) +
                       L", and a " + bare + L"<" + holder + L"> holds " + holder +
                       L"\n\tWrite the holder ('" + holder + L"->New(..)')" + plain +
                       L", or add the values with AddBack(..), which boxes each one");
          return TreeFactory::Instance()->MakeNilLiteral(expression->GetFileName(), expression->GetLineNumber(), expression->GetLinePosition());
        }
      }
      else if(element->GetExpressionType() == METHOD_CALL_EXPR) {
        // 'Class->Function' with no argument list: a method reference (WrapMethodReferenceInFuncRef)
        MethodCall* ref = static_cast<MethodCall*>(element);
        if(ref->GetCallType() == ENUM_CALL && !ref->GetCallingParameters() && !ref->IsFunctionDefinition() &&
           !ref->GetVariable() && !ref->GetTypeOf() && !ref->GetMethodName().empty() && !ref->GetVariableName().empty()) {
          const std::wstring written = ref->GetVariableName() + L"->" + ref->GetMethodName();
          ProcessError(element, L"A method reference is not typed inside a collection literal: '" + written +
                       L"' takes its type from a parameter, a declared variable or a return, not from its place in '[ ]'"
                       L"\n\tAdd it with AddBack(" + written + L"), or write the full reference: " + DescribeType(element_type) +
                       L"->New(" + written + L"(<types>) ~ <type>)<<types>>");
          return TreeFactory::Instance()->MakeNilLiteral(expression->GetFileName(), expression->GetLineNumber(), expression->GetLinePosition());
        }
      }
    }
  }

  ExpressionList* args = TreeFactory::Instance()->MakeExpressionList();
  args->AddExpression(expression);
  MethodCall* wrap = TreeFactory::Instance()->MakeMethodCall(expression->GetFileName(), expression->GetLineNumber(),
    expression->GetLinePosition(), expression->GetLineNumber(), expression->GetLinePosition(), NEW_INST_CALL, name, args);
  if(generic) {
    std::vector<Type*> concrete;
    concrete.push_back(TypeFactory::Instance()->MakeType(generics[0]));
    wrap->SetConcreteTypes(concrete);
  }
  return wrap;
}

/****************************
 * The constructor call of a map literal ('{key: value, ...}', see
 * Parser::ParseMapLiteral): the call its hidden local is declared with. Nil
 * when the expression is not a map literal.
 ****************************/
MethodCall* ContextAnalyzer::MapLiteralConstructor(Expression* expression)
{
  if(!expression || expression->GetExpressionType() != STMT_SEQ_EXPR) {
    return nullptr;
  }

  const std::vector<Statement*>& statements = static_cast<SequenceExpression*>(expression)->GetStatements();
  if(statements.empty() || statements.front()->GetStatementType() != DECLARATION_STMT) {
    return nullptr;
  }

  Assignment* assignment = static_cast<Declaration*>(statements.front())->GetAssignment();
  if(!assignment || !assignment->GetExpression() || assignment->GetExpression()->GetExpressionType() != METHOD_CALL_EXPR) {
    return nullptr;
  }

  MethodCall* constructor = static_cast<MethodCall*>(assignment->GetExpression());
  if(constructor->GetCallType() != NEW_INST_CALL) {
    return nullptr;
  }

  const std::wstring& name = constructor->GetVariableName();
  return name == MAP_LITERAL_MARKER || name == MAP_LITERAL_REJECTED || constructor->IsMapLiteral() ? constructor : nullptr;
}

/****************************
 * Gives a map literal its type: '{key: value, ...}' where a Map, Hash,
 * OrderedHash, MultiMap, MultiHash, BiMap or IntMap is wanted (a declaration
 * or assignment target, a 'return') constructs that class with the target's
 * type arguments, as 'Map->New()<K, V>' written out would. The Insert calls
 * are then resolved against it like any others, so a key or value of the
 * wrong type is the error the long form gives. A map that keeps one value per
 * key rejects a literal that writes the same constant key twice.
 ****************************/
void ContextAnalyzer::TypeMapLiteral(Expression* expression, Type* expected)
{
  MethodCall* constructor = MapLiteralConstructor(expression);
  if(!constructor || constructor->GetVariableName() != MAP_LITERAL_MARKER || !expected || expected->GetType() == VAR_TYPE) {
    return;
  }

  std::wstring bare;
  bool generic = true;
  if(expected->GetType() == CLASS_TYPE && expected->GetDimension() == 0) {
    const std::wstring name = expected->GetName();
    const size_t dot = name.rfind(L'.');
    const std::wstring scope = dot == std::wstring::npos ? L"" : name.substr(0, dot);
    if(scope.empty() || scope == L"Collection") {
      bare = dot == std::wstring::npos ? name : name.substr(dot + 1);
    }
  }

  bool unique_keys = true;
  if(bare == L"MultiMap" || bare == L"MultiHash") {
    unique_keys = false;
  }
  else if(bare == L"IntMap") {
    generic = false;
  }
  else if(bare != L"Map" && bare != L"Hash" && bare != L"OrderedHash" && bare != L"BiMap") {
    bare.clear();
  }

  const std::vector<Type*> generics = expected->GetGenerics();
  if(bare.empty() || (generic ? generics.size() != 2 : !generics.empty())) {
    constructor->SetVariableName(MAP_LITERAL_REJECTED);
    ProcessError(expression, L"A map literal makes a Map, Hash, OrderedHash, MultiMap, MultiHash, BiMap or IntMap; here it is wanted as " +
                 DescribeType(expected));
    // typed as what was wanted, so the assignment has nothing to add
    expression->SetEvalType(expected, false);
    return;
  }

  constructor->SetVariableName(expected->GetName());
  constructor->SetMapLiteral();
  if(generic) {
    std::vector<Type*> concrete;
    concrete.push_back(TypeFactory::Instance()->MakeType(generics[0]));
    concrete.push_back(TypeFactory::Instance()->MakeType(generics[1]));
    constructor->SetConcreteTypes(concrete);
  }

  // A constructor call with no type arguments and no arguments, as the value of an entry
  // ('{"k": Vector->New()}'): an entry is an argument of Insert, which gives a constructor
  // call nothing to infer from. Said here, at the value, and the type arguments the map
  // wants are filled in so that this is the only error.
  if(generic && generics[1]->HasGenerics()) {
    const std::vector<Statement*>& entries = static_cast<SequenceExpression*>(expression)->GetStatements();
    for(size_t i = 1; i < entries.size(); ++i) {
      if(entries[i]->GetStatementType() != METHOD_CALL_STMT) {
        continue;
      }
      ExpressionList* entry_args = static_cast<MethodCall*>(entries[i])->GetCallingParameters();
      if(!entry_args || entry_args->GetExpressions().size() != 2 || entry_args->GetExpressions()[1]->GetExpressionType() != METHOD_CALL_EXPR) {
        continue;
      }

      MethodCall* value = static_cast<MethodCall*>(entry_args->GetExpressions()[1]);
      if(value->GetCallType() != NEW_INST_CALL || !value->GetConcreteTypes().empty() || value->GetMethodCall() || value->GetCastType() ||
         (value->GetCallingParameters() && !value->GetCallingParameters()->GetExpressions().empty())) {
        continue;
      }

      std::wstring written;
      std::vector<Type*> value_concrete;
      const std::vector<Type*> value_generics = generics[1]->GetGenerics();
      for(size_t j = 0; j < value_generics.size(); ++j) {
        written += (j > 0 ? L", " : L"") + DescribeType(value_generics[j]);
        value_concrete.push_back(TypeFactory::Instance()->MakeType(value_generics[j]));
      }
      ProcessError(static_cast<Expression*>(value), L"A constructor call in an entry of a map literal is not given the map's value type, so its type arguments cannot be left out"
                   L"\n\tWrite them: '" + value->GetVariableName() + L"->New()<" + written +
                   L">', or assign the object to a declared variable and put the variable in the entry");
      value->SetConcreteTypes(value_concrete);
    }
  }

  // the same constant key twice: the second would replace the first without a word
  if(unique_keys) {
    std::set<std::wstring> seen;
    const std::vector<Statement*>& statements = static_cast<SequenceExpression*>(expression)->GetStatements();
    for(size_t i = 1; i < statements.size(); ++i) {
      if(statements[i]->GetStatementType() != METHOD_CALL_STMT) {
        continue;
      }
      ExpressionList* args = static_cast<MethodCall*>(statements[i])->GetCallingParameters();
      if(!args || args->GetExpressions().empty()) {
        continue;
      }

      Expression* key = args->GetExpressions().front();
      if(key->GetMethodCall() || key->GetCastType()) {
        continue;
      }

      std::wstring text;
      switch(key->GetExpressionType()) {
      case CHAR_STR_EXPR:
        // an interpolated string is not a constant
        if(static_cast<CharacterString*>(key)->GetString().find(L"{$") == std::wstring::npos) {
          text = L'"' + static_cast<CharacterString*>(key)->GetString() + L'"';
        }
        break;

      case INT_LIT_EXPR:
        text = std::to_wstring(static_cast<IntegerLiteral*>(key)->GetValue());
        break;

      case CHAR_LIT_EXPR:
        text = std::wstring(L"'") + static_cast<CharacterLiteral*>(key)->GetValue() + L"'";
        break;

      default:
        break;
      }

      if(!text.empty() && !seen.insert(text).second) {
        ProcessError(key, L"This map literal has the key " + text + L" twice");
      }
    }
  }
}

/****************************
 * Analyzes a lambda function
 ****************************/
void ContextAnalyzer::AnalyzeLambda(Lambda* lambda, const int depth)
{
  ResolveSiteScope site_scope(resolve_site, lambda);
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
  // a bare lambda `\(...) => body` whose type cannot be inferred here (a ternary
  // branch, one of several call arguments, a function-typed variable, ...);
  // resolving its empty alias name only produced "Invalid alias"
  else if(!is_inferred && lambda_name.empty()) {
    ProcessError(lambda, L"Cannot infer the type of a bare lambda here; wrap it as FuncRef->New(\\(...) => ...)<R> or give it an explicit signature \\(...) ~ R : (...) => ...");
    return;
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

  // the alias as written ('Bundle.Funcs', or one in the unnamed bundle), then
  // the current bundle's, then through the file's uses: two of those is an
  // ambiguous name, as for a class or an enum (CheckAmbiguousUse)
  Alias* alias = program->GetAlias(alias_name);
  LibraryAlias* lib_alias = nullptr;
  if(!alias) {
    const std::unordered_map<std::wstring, LibraryAlias*>& lib_aliases = linker->GetAllAliasesMap();
    std::unordered_map<std::wstring, LibraryAlias*>::const_iterator lib_hit = lib_aliases.find(alias_name);
    if(lib_hit != lib_aliases.end()) {
      lib_alias = lib_hit->second;
    }
    else if(!alias_name.empty() && alias_name.find(L'.') == std::wstring::npos) {
      ParsedBundle* own_bundle = GetBundleAt(fn, ln);
      if(own_bundle && !own_bundle->GetName().empty()) {
        alias = program->GetAlias(own_bundle->GetName() + L'.' + alias_name);
      }

      if(!alias) {
        std::vector<std::wstring> candidates;
        std::set<std::wstring> seen;
        const std::vector<std::wstring>& uses = program->GetLibUses(fn);
        for(size_t i = 0; i < uses.size(); ++i) {
          const std::wstring full_name = uses[i] + L'.' + alias_name;
          if(uses[i].empty() || uses[i].back() == L'#' || !seen.insert(full_name).second) {
            continue;
          }

          Alias* use_alias = program->GetAlias(full_name);
          lib_hit = lib_aliases.find(full_name);
          if(use_alias) {
            alias = alias ? alias : use_alias;
            candidates.push_back(L"'" + full_name + L"' (" + use_alias->GetFileName() + L':' + ToString(use_alias->GetLineNumber()) + L')');
          }
          else if(lib_hit != lib_aliases.end()) {
            lib_alias = lib_alias ? lib_alias : lib_hit->second;
            candidates.push_back(L"'" + full_name + L"' (library)");
          }
        }

        if(candidates.size() > 1) {
          ProcessError(fn, ln, lp, AmbiguousUseMessage(alias_name, candidates));
          return nullptr;
        }
        else if(alias) {
          lib_alias = nullptr;
        }
      }
    }
  }

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

/****************************
 * Wraps the deferred bare-lambda argument of a method call into
 * FuncRef->New(lambda)<R>, replaces it in the call's parameters, and analyzes
 * the wrap (which builds the lambda via the normal FuncRef->New(...)<R> path).
 * Returns true when the argument was wrapped.
 ****************************/
bool ContextAnalyzer::DerivedFuncRefLambdaArg(MethodCall* lambda_inferred_call, Type* resolved_param)
{
  Expression* bare_lambda = static_cast<Expression*>(lambda_inferred.first);
  Expression* wrap = WrapBareLambdaInFuncRef(bare_lambda, resolved_param, 0);
  if(wrap == bare_lambda) {
    return false;
  }

  ExpressionList* call_params = lambda_inferred_call->GetCallingParameters();
  if(!call_params) {
    return false;
  }

  const std::vector<Expression*> exprs = call_params->GetExpressions();
  for(size_t i = 0; i < exprs.size(); ++i) {
    if(exprs[i] == bare_lambda) {
      call_params->SetExpression(wrap, i);
      // re-analyzing the wrap re-enters the inferred-lambda machinery for the
      // FuncRef->New(...) call, which builds the lambda from its `<R>` generic
      lambda_inferred.first = nullptr;
      lambda_inferred.second = nullptr;
      AnalyzeExpression(wrap, 0);
      // consumed: a later bare lambda must not see this call as its context
      lambda_inferred.first = nullptr;
      lambda_inferred.second = nullptr;
      return true;
    }
  }

  return false;
}

Method* ContextAnalyzer::DerivedLambdaFunction(std::vector<Method*>& alt_mthds)
{
  if(lambda_inferred.first && lambda_inferred.second && alt_mthds.size() == 1) {
    MethodCall* lambda_inferred_call = lambda_inferred.second;
    Method* alt_mthd = alt_mthds[0];
    std::vector<Declaration*> alt_mthd_types = alt_mthd->GetDeclarations()->GetDeclarations();
    // a bare lambda passed where the sole parameter resolves to FuncRef<R>
    // auto-wraps into FuncRef->New(lambda)<R> (mirrors the assign/return wrap).
    // Resolve generics first -- a collection element type is often a generic
    // placeholder (`@T`) that resolves to FuncRef<R>.
    if(alt_mthd_types.size() == 1 && alt_mthd_types[0]->GetEntry()) {
      Type* resolved_param = ResolveGenericType(alt_mthd_types[0]->GetEntry()->GetType(), lambda_inferred_call, alt_mthd->GetClass(), nullptr);
      if(resolved_param && resolved_param->GetType() == CLASS_TYPE &&
         (resolved_param->GetName() == L"System.FuncRef" || resolved_param->GetName() == L"FuncRef")) {
        if(DerivedFuncRefLambdaArg(lambda_inferred_call, resolved_param)) {
          return alt_mthd;
        }
      }
    }
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

      // build lambda function; the inferred state is consumed first so the
      // lambda body (and any later bare lambda) starts from a clean state
      Lambda* inferred_lambda = lambda_inferred.first;
      lambda_inferred.first = nullptr;
      lambda_inferred.second = nullptr;
      BuildLambdaFunction(inferred_lambda, inferred_type, 0);
      return alt_mthd;
    }
  }

  return nullptr;
}

LibraryMethod*ContextAnalyzer::DerivedLambdaFunction(std::vector<LibraryMethod*>& alt_mthds)
{
  if(lambda_inferred.first && lambda_inferred.second && alt_mthds.size() == 1) {
    MethodCall* lambda_inferred_call = lambda_inferred.second;
    LibraryMethod* alt_mthd = alt_mthds[0];
    std::vector<frontend::Type*> alt_mthd_types = alt_mthd->GetDeclarationTypes();
    // bare lambda passed where the sole parameter resolves to FuncRef<R>
    // auto-wraps (resolve generics first -- collection element types are often
    // a generic placeholder `@T` that resolves to FuncRef<R>)
    if(alt_mthd_types.size() == 1) {
      Type* resolved_param = ResolveGenericType(alt_mthd_types[0], lambda_inferred_call, nullptr, alt_mthd->GetLibraryClass());
      if(resolved_param && resolved_param->GetType() == CLASS_TYPE &&
         (resolved_param->GetName() == L"System.FuncRef" || resolved_param->GetName() == L"FuncRef")) {
        if(DerivedFuncRefLambdaArg(lambda_inferred_call, resolved_param)) {
          return alt_mthd;
        }
      }
    }
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

      // build lambda function (inferred state consumed first, as above)
      Lambda* inferred_lambda = lambda_inferred.first;
      lambda_inferred.first = nullptr;
      lambda_inferred.second = nullptr;
      BuildLambdaFunction(inferred_lambda, inferred_type, 0);

      // a result typed by the method's own type parameter -- Map<R>((H) ~ R) --
      // is bound by what the lambda's body returns (#1242)
      Type* declared_rtrn = alt_mthd_type->GetFunctionReturn();
      if(declared_rtrn && !declared_rtrn->GetMethodGeneric().empty()) {
        Type* result_type = LambdaResultType(inferred_lambda);
        if(result_type) {
          std::map<std::wstring, Type*> bindings = lambda_inferred_call->GetMethodGenericBindings();
          Type* binding = TypeFactory::Instance()->MakeType(BoxedBindingType(result_type));
          binding->SetMethodGeneric(L"");
          ResolveClassEnumType(binding);
          bindings[declared_rtrn->GetMethodGeneric()] = binding;
          lambda_inferred_call->SetMethodGenericBindings(bindings);
        }
      }
      return alt_mthd;
    }
  }

  return nullptr;
}

/****************************
 * The type a lambda's body returns:
 * the expression of its last return
 * (#1242)
 ****************************/
Type* ContextAnalyzer::LambdaResultType(Lambda* lambda)
{
  Method* method = lambda->GetMethod();
  if(!method || !method->GetStatements()) {
    return nullptr;
  }
  std::vector<Statement*> statements = method->GetStatements()->GetStatements();
  for(size_t i = statements.size(); i > 0; --i) {
    if(statements[i - 1]->GetStatementType() == RETURN_STMT) {
      Expression* expression = static_cast<Return*>(statements[i - 1])->GetExpression();
      if(expression) {
        Expression* last = expression;
        while(last->GetMethodCall()) {
          last = last->GetMethodCall();
        }
        return last->GetEvalType();
      }
      return nullptr;
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

    // AddMethod returns false when the signature collides. Ignoring it meant the
    // lambda was silently not registered while the code below went on to encode
    // and associate it, so a duplicate produced no diagnostic at all.
    if(!current_class->AddMethod(method)) {
      ProcessError(lambda, L"Method/function already defined or overloaded '" + method->GetName() + L"'");
      return;
    }
    method->EncodeSignature(current_class, program, linker);
    current_class->AssociateMethod(method);

    // check method and restore context; a lambda nested in a lambda body re-enters
    // here, so the enclosing lambda's capture state is saved and restored
    Lambda* prev_capture_lambda = capture_lambda;
    Method* prev_capture_method = capture_method;
    SymbolTable* prev_capture_table = capture_table;
    capture_lambda = lambda;
    capture_method = current_method;
    capture_table = current_table;
    capture_frames.push_back({ lambda, current_method, current_table });

#ifdef _DIAG_LIB
    // AnalyzeMethod clears diagnostic_expressions on entry and hands the list
    // to whichever method it is analyzing. Analyzing a lambda body re-enters
    // it, so without this the enclosing method loses every expression recorded
    // before the lambda -- go-to-definition, references and rename then miss
    // those statements. current_method is already saved and restored here; the
    // expression list needs the same treatment.
    std::vector<Expression*> capture_expressions = diagnostic_expressions;
#endif

    // The lambda body is its own method, but it is analyzed from inside the
    // enclosing statement (a return, an assignment or a call argument). Left
    // set, those flags made RogueReturn() treat every standalone call in the
    // body as a used value, so its result was never popped and the
    // interpreter's operand stack came back unbalanced from the lambda.
    const int capture_in_loop = in_loop;
    const bool capture_in_assignment = in_assignment;
    const bool capture_in_return = in_return;
    const int capture_expression_depth = expression_depth;
    const int capture_nested_call_depth = nested_call_depth;
    in_loop = expression_depth = nested_call_depth = 0;
    in_assignment = in_return = false;

    AnalyzeMethod(method, depth + 1);

    in_loop = capture_in_loop;
    in_assignment = capture_in_assignment;
    in_return = capture_in_return;
    expression_depth = capture_expression_depth;
    nested_call_depth = capture_nested_call_depth;

#ifdef _DIAG_LIB
    diagnostic_expressions = capture_expressions;
#endif

    capture_frames.pop_back();
    current_table = capture_table;
    capture_table = prev_capture_table;

    current_method = capture_method;
    capture_method = prev_capture_method;
    capture_lambda = prev_capture_lambda;

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
      throw CompilerFatal(L"Internal compiler error: Invalid method name.");
    }
  }
  else {
    ProcessError(lambda, L"Declaration and parameter size mismatch");
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
      ident = dclr_type->GetName();
      break;

    // A function-typed parameter, e.g. \((Int) ~ Int, Int) ~ Int : (fn, a) => ...
    // A parsed `(Int) ~ Int` has no name, so the parameter was silently dropped,
    // the lookup key lost an argument and the lambda's own method '#{Ln}#' was
    // reported undefined. Pass the `m.(...)~R` encoding instead, which
    // EncodeFunctionReference maps back to the function type; it matches the
    // "m." encoding Method::EncodeType gives the parameter in the signature.
    case FUNC_TYPE: {
      const std::wstring func_name = dclr_type->GetName();
      if(func_name.empty()) {
        ident = L"m." + EncodeFunctionType(dclr_type->GetFunctionParameters(), dclr_type->GetFunctionReturn());
      }
      else if(func_name.compare(0, 2, L"m.") == 0) {
        ident = func_name;
      }
      else {
        ident = L"m." + func_name;
      }
    }
      break;

    case ALIAS_TYPE:
      break;
    }

    if(!ident.empty()) {
      Variable* param = TreeFactory::Instance()->MakeVariable(dclrs[i]->GetFileName(), dclrs[i]->GetLineNumber(), dclrs[i]->GetLinePosition(), ident);
      // an array parameter keeps its dimension, as `Int[]` parses: one empty
      // index slot per dimension. Without it \(Int[]) ~ Int looked up
      // '#{L0}#(i)' for a method encoded '(i*)' and was undefined (#1183).
      if(dclr_type->GetType() != FUNC_TYPE && dclr_type->GetDimension() > 0) {
        ExpressionList* slots = TreeFactory::Instance()->MakeExpressionList();
        for(int j = 0; j < dclr_type->GetDimension(); ++j) {
          slots->AddExpression(TreeFactory::Instance()->MakeVariable(dclrs[i]->GetFileName(), dclrs[i]->GetLineNumber(), dclrs[i]->GetLinePosition(), L"#"));
        }
        param->SetIndices(slots);
      }
      expressions->AddExpression(param);
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
 * A subscripted jagged array
 * (#1246): 'rows[i]' is a row,
 * an array value; each further
 * group, 'rows[i][j]', indexes
 * into the array before it.
 ****************************/
void ContextAnalyzer::AnalyzeJaggedAccess(Variable* variable, Type* array_type, const int depth)
{
  if(!array_type || array_type->GetType() != CLASS_TYPE || !IsRowTypeName(array_type->GetName())) {
    if(!variable->GetChainedIndices().empty()) {
      ProcessError(variable, L"Only a jagged array's element takes a second subscript");
    }
    return;
  }

  // the row the first group selects
  Type* current = RowElementType(array_type);
  ResolveClassEnumType(current);

  std::vector<Type*> chained_types;
  const std::vector<ExpressionList*> chained = variable->GetChainedIndices();
  for(size_t i = 0; i < chained.size(); ++i) {
    ExpressionList* group = chained[i];
    if(current->GetDimension() == 0) {
      ProcessError(variable, L"Too many subscripts for this jagged array");
      return;
    }
    if((int)group->GetExpressions().size() != current->GetDimension()) {
      ProcessError(variable, L"Dimension size mismatch in a jagged array subscript");
      return;
    }
    AnalyzeIndices(group, depth + 1);
    chained_types.push_back(current);

    if(current->GetType() == CLASS_TYPE && IsRowTypeName(current->GetName())) {
      current = RowElementType(current);
      ResolveClassEnumType(current);
    }
    else {
      current = TypeFactory::Instance()->MakeType(current);
      current->SetDimension(0);
    }
  }
  variable->SetChainedTypes(chained_types);

  // A row (an array) is a value of its own type; an element read through
  // chained groups is a scalar of the last array's element type.
  if(current->GetDimension() > 0) {
    variable->SetRowValue(true);
    variable->SetEvalType(current, false);
  }
  else {
    Type* last = chained_types.back();
    variable->SetEvalType(last, false);
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

    // the body is the statement's value: it closes on the way out and returns
    case USING_STMT:
      return AnalyzeReturnPaths(static_cast<Using*>(last_statement)->GetBody(), depth + 1);

    case TRY_STMT: {
      // an error no handler takes is raised again, so only the body and the
      // handlers need to return (#1247)
      Try* try_stmt = static_cast<Try*>(last_statement);
      bool returns = AnalyzeReturnPaths(try_stmt->GetBody(), depth + 1);
      const std::vector<TryHandler*> &handlers = try_stmt->GetHandlers();
      for(size_t i = 0; returns && i < handlers.size(); ++i) {
        returns = AnalyzeReturnPaths(handlers[i]->statements, depth + 1);
      }
      return returns;
    }

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
  ResolveSiteScope site_scope(resolve_site, statement);
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
      if(!finally_loops.empty()) {
        ProcessError(statement, L"A 'finally' block cannot be left with 'return'");
      }
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
      else if(!finally_loops.empty() && in_loop <= finally_loops.back()) {
        ProcessError(statement, L"A 'finally' block cannot be left with 'break' or 'continue'");
      }
      break;

    case SELECT_STMT:
      current_method->SetAndOr(true);
      AnalyzeSelect(static_cast<Select*>(statement), depth);
      break;

    case CRITICAL_STMT:
      AnalyzeCritical(static_cast<CriticalSection*>(statement), depth);
      break;

    case TRY_STMT:
      AnalyzeTry(static_cast<Try*>(statement), depth);
      break;

    case USING_STMT:
      AnalyzeUsing(static_cast<Using*>(statement), depth);
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
  ResolveSiteScope site_scope(resolve_site, expression);
  if(expression) {
    // Already lowered to a concatenation that carries the call chain: a second
    // visit (AnalyzeStringConcat probes an operand before AnalyzeCalculation
    // visits it again) would build a fresh concatenation WITHOUT the chain,
    // which had already been moved off this node.
    if(expression->GetPreviousExpression() && expression->GetPreviousExpression()->GetExpressionType() == STR_CONCAT_EXPR &&
       expression->GetPreviousExpression()->GetMethodCall()) {
      return;
    }

    ++expression_depth;

    StringConcat* str_concat = AnalyzeStringConcat(expression, depth + 1);
    if(str_concat) {
      expression->SetPreviousExpression(str_concat);
      // `("a" + b)->ToUpper()`: the chain hangs off the '+' node, which never gets
      // a type of its own -- the concatenation is a separate node that callers
      // substitute for it. Resolve the chain against that node, a String, and
      // move it there so it is emitted after the concatenation. Left on the '+'
      // node it failed as "Invalid class type or assignment".
      MethodCall* chain = expression->GetMethodCall();
      if(chain) {
        str_concat->SetMethodCall(chain);
        expression->ClearMethodCall();
        AnalyzeExpressionMethodCall(str_concat, depth + 1);
      }
    }
    else {
      switch(expression->GetExpressionType()) {
      case LAMBDA_EXPR:
        AnalyzeLambda(static_cast<Lambda*>(expression), depth);
        break;

      case STAT_ARY_EXPR:
        AnalyzeStaticArray(static_cast<StaticArray*>(expression), depth);
        break;

      case CHAR_STR_EXPR: {
        CharacterString* char_str = static_cast<CharacterString*>(expression);
        AnalyzeCharacterString(char_str, depth + 1);
        // Handle method calls attached to the string literal (e.g., "alice"->ToUpper())
        if(char_str->GetMethodCall()) {
          Expression* expr = char_str;
          while(expr->GetMethodCall()) {
            AnalyzeExpressionMethodCall(expr, depth + 1);
            expr = expr->GetMethodCall();
          }
        }
      }
        break;

      case COND_EXPR:
        AnalyzeConditional(static_cast<Cond*>(expression), depth);
        break;

      case STMT_SEQ_EXPR:
        AnalyzeSequence(static_cast<SequenceExpression*>(expression), depth);
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
 * Analyzes a 'Try()' intrinsic
 * method call
 ****************************/
void ContextAnalyzer::AnalyzeTryIntrinsic(MethodCall* method_call, Expression* expression, const int depth)
{
#ifdef _DEBUG
  Debug(L"Try() intrinsic", static_cast<Expression*>(method_call)->GetLineNumber(), depth);
#endif

  method_call->SetTryIntrinsic(true);

  Type* recv_type = expression->GetEvalType();
  if(!recv_type || recv_type->GetType() != CLASS_TYPE) {
    ProcessError(static_cast<Expression*>(method_call), L"'Try()' can only be called on object types");
    return;
  }

  // Try() must be followed by a method call in the chain
  MethodCall* next_call = method_call->GetMethodCall();
  if(!next_call) {
    ProcessError(static_cast<Expression*>(method_call), L"'Try()' must be followed by a method call");
    return;
  }

  // Set return type to receiver's type so the chain continues with the correct class
  method_call->SetEvalType(recv_type, false);
  current_method->SetAndOr(true);

  // Continue resolving the chain from Try() - AnalyzeExpressionMethodCall will
  // look at method_call->GetMethodCall() (Size(), etc.) and resolve it using
  // method_call's eval type (the receiver's class, not Base)
  AnalyzeExpressionMethodCall(static_cast<Expression*>(method_call), depth + 1);
}

/****************************
 * Analyzes an 'Otherwise()'
 * intrinsic method call
 ****************************/
void ContextAnalyzer::AnalyzeOtherwiseIntrinsic(MethodCall* method_call, Expression* expression, const int depth)
{
#ifdef _DEBUG
  Debug(L"Otherwise() intrinsic", static_cast<Expression*>(method_call)->GetLineNumber(), depth);
#endif

  method_call->SetOtherwiseIntrinsic(true);

  // Analyze the default argument
  const std::vector<Expression*>& args = method_call->GetCallingParameters()->GetExpressions();
  Expression* default_expr = args[0];
  AnalyzeExpression(default_expr, depth + 1);

  Type* recv_type = expression->GetEvalType();
  Type* default_type = GetExpressionType(default_expr, depth + 1);

  if(recv_type && default_type) {
    if(recv_type->GetType() == CLASS_TYPE && default_type->GetType() == CLASS_TYPE) {
      AnalyzeClassCast(recv_type, default_expr, depth + 1);
    }
    else if(recv_type->GetType() == NIL_TYPE) {
      // Nil->Otherwise(default): use default type
    }
    else if(default_type->GetType() == NIL_TYPE) {
      // expr->Otherwise(Nil): use expr type
    }
    else if(recv_type->GetType() != default_type->GetType() &&
            !((recv_type->GetType() == CLASS_TYPE && default_type->GetType() == NIL_TYPE) ||
            (recv_type->GetType() == NIL_TYPE && default_type->GetType() == CLASS_TYPE))) {
      ProcessError(static_cast<Expression*>(method_call), L"'Otherwise()' type mismatch between receiver and default");
    }

    // set eval type from receiver (or default if receiver is Nil)
    if(recv_type->GetType() == NIL_TYPE && default_type->GetType() != NIL_TYPE) {
      method_call->SetEvalType(default_type, false);
    }
    else {
      method_call->SetEvalType(recv_type, false);
    }
    current_method->SetAndOr(true);

    // resolve any chain after Otherwise()
    if(method_call->GetMethodCall()) {
      AnalyzeExpressionMethodCall(static_cast<Expression*>(method_call), depth + 1);
    }
  }
  else {
    ProcessError(static_cast<Expression*>(method_call), L"Invalid 'Otherwise()' expression");
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
  // The dimension of the value a branch yields. An indexed element (p[1]) is a
  // scalar even though its variable's type carries the array's dimension; a call
  // chain or a cast is already resolved into the branch's type.
  auto value_dimension = [](Expression* branch, Type* branch_type) {
    if(!branch->GetCastType() && !branch->GetMethodCall() && branch->GetExpressionType() == VAR_EXPR &&
       static_cast<Variable*>(branch)->GetIndices()) {
      return 0;
    }
    return branch_type->GetDimension();
  };

  if(if_type && else_type) {
    const int if_dimension = value_dimension(if_conditional, if_type);
    // an array branch paired with a scalar branch (or arrays of different rank)
    // is a mismatch like any other; checked first because the class-type branch
    // below never compares dimensions. Nil stays assignable to either kind.
    if(if_type->GetType() != NIL_TYPE && else_type->GetType() != NIL_TYPE &&
       if_dimension != value_dimension(else_conditional, else_type)) {
      ProcessError(conditional, L"'?' invalid type mismatch");
    }
    else if(if_type->GetType() == CLASS_TYPE && else_type->GetType() == CLASS_TYPE) {
      AnalyzeClassCast(if_type, else_conditional, depth + 1);
    }
    else if(if_type->GetType() != else_type->GetType() &&
            !((if_type->GetType() == CLASS_TYPE && else_type->GetType() == NIL_TYPE) ||
            (if_type->GetType() == NIL_TYPE && else_type->GetType() == CLASS_TYPE))) {
      ProcessError(conditional, L"'?' invalid type mismatch");
    }
    // set eval type with the dimension of the value the branches yield. Zeroing it
    // unconditionally typed a conditional over primitive arrays as a scalar, so a
    // call on it resolved to the scalar helper and returned garbage (#867).
    // The branch's TYPE, not its eval type: for a call chain the eval type is the
    // first call's, so 'c ? cols->Get(0)->GetType() : ColumnType->INT' was typed
    // a Col, and returning it as a ColumnType failed "Invalid cast between enum
    // and class". A Nil first branch takes the other branch's type.
    conditional->SetEvalType(if_type->GetType() == NIL_TYPE ? else_type : if_type, true);
    conditional->GetEvalType()->SetDimension(if_type->GetType() == NIL_TYPE ? value_dimension(else_conditional, else_type) : if_dimension);
    current_method->SetAndOr(true);

    // A function reference is two words, and the branches' value used to meet
    // at the join in the one-word and/or temporary: the store took the method
    // id and left each branch's closure word on the operand stack. The
    // interpreter's stack happened to line both up again, but a JIT-compiled
    // method kept the closure word of the branch it compiled LAST, so taking
    // the other branch called through the wrong closure -- an access violation
    // once that branch captured anything (jit_conditional_value_types.obs).
    // Such a conditional gets a hidden two-word local of its own instead.
    Type* value_type = conditional->GetEvalType();
    if(!conditional->GetFuncTemp() && value_type->GetType() == FUNC_TYPE && value_type->GetDimension() == 0 &&
       current_table) {
      static int cond_tmp_id = 0;
      const std::wstring tmp_name = current_method->GetName() + L":#cond_tmp#" + std::to_wstring(cond_tmp_id++);
      SymbolEntry* entry = TreeFactory::Instance()->MakeSymbolEntry(tmp_name, TypeFactory::Instance()->MakeType(value_type), false, true);
      if(current_table->AddEntry(entry, true)) {
        entry->WasLoaded();
        conditional->SetFuncTemp(entry);
      }
      else {
        ProcessError(conditional, L"Internal error: unable to allocate a function-reference conditional's local");
      }
    }
  }
  else {
    ProcessError(conditional, L"Invalid 'if' statement");
  }
}

/****************************
 * The first index i for which 'prefix' + i names nothing in the scopes in view.
 *
 * An interpolated string and a '+' concatenation each take a hidden local named
 * by the lowest index not in use, so that the name is fresh where two of them
 * are live at once (#750). The names in view are therefore always 0..n-1 -- a
 * scope sees its ancestors' and its own, and each new one extends the run -- so
 * the first free index is found by doubling and then bisecting. Probing upward
 * from 0 made the k-th one in a method cost k lookups: 4,000 interpolated
 * strings in one method took 1.6 s to reach the "Local space has been exceeded"
 * error. Should the run ever have a gap, the result is still a name nothing in
 * view has.
 ****************************/
static int FirstFreeHiddenIndex(SymbolTable* table, const std::wstring& prefix)
{
  auto taken = [&](int index) {
    return table->GetEntry(prefix + std::to_wstring(index)) != nullptr;
  };

  if(!taken(0)) {
    return 0;
  }

  // taken(low) holds, taken(high) does not
  int low = 0;
  int high = 1;
  while(taken(high)) {
    low = high;
    high <<= 1;
  }
  while(high - low > 1) {
    const int middle = low + (high - low) / 2;
    if(taken(middle)) {
      low = middle;
    }
    else {
      high = middle;
    }
  }

  return high;
}

/****************************
 * Analyzes statements that run where
 * an expression is evaluated, and the
 * value they leave in a hidden local
 ****************************/
void ContextAnalyzer::AnalyzeSequence(SequenceExpression* sequence, const int depth)
{
#ifdef _DEBUG
  Debug(L"sequence expression", sequence->GetLineNumber(), depth);
#endif

  // once: the statements declare and assign, which a second visit would repeat
  if(sequence->IsAnalyzed()) {
    return;
  }
  sequence->SetAnalyzed();

  // A map literal that nothing gave a type (or that an error has already been
  // reported for) has no class to construct: say so once, and give the value a
  // type so what uses it has nothing to add.
  MethodCall* map_constructor = MapLiteralConstructor(sequence);
  if(map_constructor && !map_constructor->IsMapLiteral()) {
    if(map_constructor->GetVariableName() == MAP_LITERAL_MARKER) {
      ProcessError(sequence, L"A map literal takes its type from the variable it is assigned to or the method it is returned from; "
                   L"here it has neither, so declare one first, as in 'm : Map<String, IntRef> := {\"a\": 1};'");
    }
    if(!sequence->GetEvalType()) {
      sequence->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.Base"), false);
    }
    return;
  }

  // The statements are analyzed from inside the enclosing statement (an
  // assignment, a return, a call argument). Left set, its flags would make
  // RogueReturn() treat a standalone call among them as a used value, and the
  // result would stay on the operand stack (as for a lambda body).
  const bool outer_in_assignment = in_assignment;
  const bool outer_in_return = in_return;
  const int outer_expression_depth = expression_depth;
  const int outer_nested_call_depth = nested_call_depth;
  in_assignment = in_return = false;
  expression_depth = nested_call_depth = 0;

  const std::vector<Statement*>& statements = sequence->GetStatements();
  for(size_t i = 0; i < statements.size(); ++i) {
    AnalyzeStatement(statements[i], depth + 1);
  }

  in_assignment = outer_in_assignment;
  in_return = outer_in_return;
  expression_depth = outer_expression_depth;
  nested_call_depth = outer_nested_call_depth;

  Expression* value = sequence->GetValue();
  AnalyzeExpression(value, depth + 1);
  if(value->GetEvalType()) {
    sequence->SetEvalType(value->GetEvalType(), false);
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

  // Analysis of a literal is not idempotent: every visit appends its segments,
  // and an interpolated entry appended twice is emitted twice. The segments
  // are a pure function of the literal's text, so a second visit has nothing
  // to add; the ids and the concatenation temporary below were assigned on the
  // first. Nothing should visit twice (see AnalyzeStringConcat), but if
  // something does, the literal must still mean what it says (#752).
  if(!char_str->GetSegments().empty()) {
    return;
  }

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
          AnalyzeInterpolatedToken(token, char_str, depth);
          // update
          var_start = -1;
          str_start = (int)i + 1;
        }
        else if(i + 1 == str.size()) {
          const std::wstring token = str.substr(static_cast<std::basic_string<wchar_t, std::char_traits<wchar_t>, std::allocator<wchar_t>>::size_type>(var_start) + 1, i - var_start);
          AnalyzeInterpolatedToken(token, char_str, depth);
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
    // A fresh accumulator per interpolated string, for the reason spelled out at
    // the '+' concatenation site below: one slot per method breaks as soon as two
    // of these are live at once, e.g. "{$a}" + Id("{$b}") (#750).
    const std::wstring scope_prefix = current_method->GetName() + L":#_var_concat_#";
    const std::wstring scope_name = scope_prefix + std::to_wstring(FirstFreeHiddenIndex(current_table, scope_prefix));

    SymbolEntry* entry = TreeFactory::Instance()->MakeSymbolEntry(scope_name, type, false, true);
    current_table->AddEntry(entry, true);
    char_str->SetConcat(entry);

    // An object interpolated as Nil reads as "Nil" rather than stopping the program: the
    // emitter tests it through the method's interpolation temporary and appends this
    // literal instead.
    // Expressions are included because their type is final only when emitted.
    bool has_object = false;
    for(size_t i = 0; i < segments.size(); ++i) {
      if(segments[i]->GetType() == EXPRESSION) {
        has_object = true;
      }
      else if(segments[i]->GetType() == ENTRY) {
        Type* entry_type = segments[i]->GetEntry()->GetType();
        if(entry_type && entry_type->GetType() == CLASS_TYPE && entry_type->GetDimension() == 0) {
          has_object = true;
        }
      }
    }

    if(has_object) {
      int nil_id = program->GetCharStringId(L"Nil");
      if(nil_id < 0) {
        nil_id = char_str_index;
        program->AddCharString(L"Nil", char_str_index);
        char_str_index++;
      }

      // One temporary per method: an interpolation holds it only between storing a value and
      // loading it back, and a nested one finishes inside the outer value's expression, so
      // they never overlap. One per interpolation ran a long method out of local space.
      SymbolEntry* nil_entry = current_method->GetInterpTemp();
      if(!nil_entry) {
        Type* nil_type = TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.Base");
        const std::wstring nil_name = current_method->GetName() + L":#_var_interp_#";
        nil_entry = TreeFactory::Instance()->MakeSymbolEntry(nil_name, nil_type, false, true);
        current_table->AddEntry(nil_entry, true);
        current_method->SetInterpTemp(nil_entry);
      }
      char_str->SetNilCheck(nil_entry, nil_id);
    }
  }

#ifndef _SYSTEM
  LibraryClass* lib_klass = SearchLibraryClasses(L"System.String", program->GetLibUses(current_class->GetFileName()));
  if(lib_klass) {
    lib_klass->SetCalled(true);
  }
  else {
    ProcessError(char_str, L"Invalid class name='System.String' system bundle 'lang.obl' excluded");
  }
#endif

  char_str->SetProcessed();
}

// an element type, as an error message names it
static std::wstring ElementTypeName(Type* type)
{
  switch(type->GetType()) {
  case BOOLEAN_TYPE:
    return L"Bool";
  case BYTE_TYPE:
    return L"Byte";
  case CHAR_TYPE:
    return L"Char";
  case INT_TYPE:
    return L"Int";
  case FLOAT_TYPE:
    return L"Float";
  default:
    return type->GetName();
  }
}

/****************************
 * Analyzes an array literal
 * whose elements are not all
 * literals: '[x, y + 1, F()]'
 * (#1245). It is built when it
 * is evaluated, in a hidden
 * local.
 ****************************/
void ContextAnalyzer::AnalyzeDynamicArray(StaticArray* array, const int depth)
{
  array->SetDynamic();

  if(array->GetDimension() > 1) {
    ProcessError(array, L"A multi-dimensional array literal must hold only literals; build it with ->New[..]");
    return;
  }

  if(!current_method || !current_table) {
    ProcessError(array, L"An array literal with variables must be inside a method or function");
    return;
  }

  // Every element is the same primitive type, or every element is an object --
  // of one class, or System.Base for a mix. Primitives among objects are boxed
  // ('["a", 1]' is a Base[] of a String and an IntRef); two different primitive
  // types are an error, as there is no implicit numeric promotion.
  Type* element_type = nullptr;
  Type* primitive_type = nullptr;
  Type* class_type = nullptr;
  Type* mismatched_primitive = nullptr;
  Type* row_type = nullptr;
  bool mixed_classes = false;
  bool has_nil = false;
  std::vector<Expression*> elements = array->GetElements()->GetExpressions();
  std::vector<Type*> types;
  for(size_t i = 0; i < elements.size(); ++i) {
    Expression* element = elements[i];
    AnalyzeExpression(element, depth + 1);

    Type* type = GetExpressionType(element, depth + 1);
    if(!type) {
      return;
    }
    // Nil: an empty slot of an object array, or a missing row; the type comes
    // from the other elements, and a literal of nothing else is refused below
    if(type->GetType() == NIL_TYPE) {
      has_nil = true;
      types.push_back(type);
      continue;
    }

    // a subscripted element, '[a[i]]', is a single value: its eval type keeps the
    // array's dimension, which callers take the subscripts from
    if(type->GetDimension() > 0 && IsScalar(element)) {
      type = TypeFactory::Instance()->MakeType(type);
      type->SetDimension(0);
    }

    // an array element makes the literal an array of rows: '[a, b]' with a, b
    // Int[] is an Int[][] (#1246); every row must be the same array type
    if(type->GetDimension() > 0) {
      if(type->GetDimension() > 1) {
        ProcessError(element, L"A jagged array's rows must be one-dimensional");
        return;
      }
      if(row_type && (row_type->GetType() != type->GetType() || row_type->GetName() != type->GetName())) {
        ProcessError(array, L"Jagged array rows must be of one type");
        return;
      }
      row_type = type;
      continue;
    }
    if(row_type) {
      ProcessError(element, L"An array literal cannot mix rows and single values");
      return;
    }

    switch(type->GetType()) {
    case BOOLEAN_TYPE:
    case BYTE_TYPE:
    case CHAR_TYPE:
    case INT_TYPE:
    case FLOAT_TYPE:
      if(primitive_type && primitive_type->GetType() != type->GetType() && !mismatched_primitive) {
        mismatched_primitive = type;
      }
      else if(!primitive_type) {
        primitive_type = type;
      }
      break;

    case CLASS_TYPE:
      // compared by full name: an enum value's type is written as the code
      // names it, 'ColumnType', a method's return in full, 'QLib.ColumnType'.
      // Unresolved, '[c->GetType(), ColumnType->INT]' was a mix, so a Base[],
      // and then "Invalid cast between enum and class" where a ColumnType[]
      // was wanted.
      type = TypeFactory::Instance()->MakeType(type);
      ResolveClassEnumType(type);
      if(class_type && class_type->GetName() != type->GetName()) {
        mixed_classes = true;
      }
      class_type = type;
      break;

    default:
      ProcessError(element, L"Invalid array literal element");
      return;
    }
    types.push_back(type);
  }

  // an enum value is an Int at run time: it neither boxes into a Base[] nor
  // has a Nil
  const bool is_enum = class_type && HasProgramOrLibraryEnum(class_type->GetName());
  if(is_enum && (primitive_type || mixed_classes)) {
    ProcessError(array, L"Array element types do not match: an enum array literal holds values of one enum");
    return;
  }

  if(has_nil) {
    if(is_enum) {
      ProcessError(array, L"An enum array literal cannot hold Nil");
      return;
    }
    if(!row_type && !class_type) {
      if(primitive_type) {
        ProcessError(array, L"An array literal of '" + ElementTypeName(primitive_type) + L"' values cannot hold Nil");
      }
      else {
        ProcessError(array, L"An array literal element cannot be Nil alone; give the array a type with ->New[..]");
      }
      return;
    }
  }

  if(row_type) {
    if(primitive_type || class_type) {
      ProcessError(array, L"An array literal cannot mix rows and single values");
      return;
    }
    // the array of rows: '$Int[]' for Int[] rows, '$Int[][]' for rows that are Int[][]
    ResolveClassEnumType(row_type);
    int levels = 1;
    std::wstring base = RowBaseName(row_type);
    if(row_type->GetType() == CLASS_TYPE && IsRowTypeName(row_type->GetName())) {
      base = RowTypeBase(row_type->GetName(), levels);
      levels++;
    }
    element_type = TypeFactory::Instance()->MakeType(CLASS_TYPE, RowTypeName(base, levels));
  }
  else if(class_type && primitive_type) {
    // box the primitives
    for(size_t i = 0; i < elements.size(); ++i) {
      std::wstring holder;
      switch(types[i]->GetType()) {
      case BOOLEAN_TYPE:
        holder = L"System.BoolRef";
        break;
      case BYTE_TYPE:
        holder = L"System.ByteRef";
        break;
      case CHAR_TYPE:
        holder = L"System.CharRef";
        break;
      case INT_TYPE:
        holder = L"System.IntRef";
        break;
      case FLOAT_TYPE:
        holder = L"System.FloatRef";
        break;
      default:
        break;
      }

      if(!holder.empty()) {
        Expression* boxed = BoxExpression(TypeFactory::Instance()->MakeType(CLASS_TYPE, holder), elements[i], depth + 1);
        if(!boxed) {
          ProcessError(elements[i], L"Cannot box this array literal element");
          return;
        }
        array->GetElements()->SetExpression(boxed, i);
      }
    }
    element_type = TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.Base");
    mixed_classes = false;
  }
  else if(mismatched_primitive) {
    ProcessError(array, L"Array element types do not match: '" + ElementTypeName(primitive_type) +
                 L"' and '" + ElementTypeName(mismatched_primitive) + L"'");
    return;
  }
  else {
    element_type = class_type ? class_type : primitive_type;
  }

  if(!element_type) {
    ProcessError(array, L"Empty array literal");
    return;
  }

  Type* array_type = TypeFactory::Instance()->MakeType(element_type);
  if(array_type->GetType() == CLASS_TYPE && mixed_classes) {
    array_type->SetName(L"System.Base");
  }
  array_type->SetDimension(1);

  // the array is built in a hidden local, then loaded
  static int dynamic_array_id = 0;
  const std::wstring tmp_name = current_method->GetName() + L":#array_tmp#" + std::to_wstring(dynamic_array_id++);
  SymbolEntry* entry = TreeFactory::Instance()->MakeSymbolEntry(tmp_name, array_type, false, true);
  current_table->AddEntry(entry, true);
  entry->WasLoaded();
  array->SetDynamicEntry(entry);

  // '[x, y][1]': the subscripted literal is one element
  ExpressionList* indices = array->GetIndices();
  if(indices) {
    if(indices->GetExpressions().size() != 1) {
      ProcessError(array, L"Dimension size mismatch: the literal has 1 dimension");
    }
    AnalyzeIndices(indices, depth + 1);

    Type* element = TypeFactory::Instance()->MakeType(array_type);
    element->SetDimension(0);
    array->SetEvalType(element, false);
  }
  else {
    array->SetEvalType(array_type, false);
  }
}

/****************************
 * Analyzes a static array
 ****************************/
void ContextAnalyzer::AnalyzeStaticArray(StaticArray* array, const int depth)
{
  // a ragged literal, '[[1], [1, 1]]', is an array of rows (#1246)
  if(array->GetDimension() == 2 && !array->IsMatchingLenghts()) {
    array->SetJaggedRows();
  }

  // an element that is not a literal, or literals of an object and a primitive
  // ('["a", 1]'): built at run time (#1245)
  if(array->IsDynamic() || (!array->IsMatchingTypes() && array->GetDimension() == 1)) {
    AnalyzeDynamicArray(array, depth);
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

  // '["a", "b"][1]': the subscripted literal is one element (#1184)
  ExpressionList* indices = array->GetIndices();
  if(indices) {
    std::vector<Expression*> index_exprs = indices->GetExpressions();
    if((int)index_exprs.size() != array->GetDimension()) {
      ProcessError(array, L"Dimension size mismatch: the literal has " + ToString(array->GetDimension()) +
                   L" dimension(s), the subscript " + ToString((int)index_exprs.size()));
    }
    AnalyzeIndices(indices, depth + 1);

    left_type = TypeFactory::Instance()->MakeType(left_type);
    left_type->SetDimension(0);
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
  // taken once, so a name read in the target's own subscript is not a target
  const bool is_target = in_assignment_target;
  in_assignment_target = false;

  // explicitly defined variable
  if(entry) {
    entry->WasLoaded();

    // a 'ref' parameter (#1243): the name is element 0 of the caller's cell
    if(entry->IsReference() && !variable->GetIndices()) {
      ExpressionList* zero = TreeFactory::Instance()->MakeExpressionList();
      zero->AddExpression(TreeFactory::Instance()->MakeIntegerLiteral(variable->GetFileName(), variable->GetLineNumber(),
                                                                      variable->GetLinePosition(), 0));
      variable->SetIndices(zero);
    }
#ifdef _DEBUG
    std::wstring msg = L"variable reference: name='" + variable->GetName() + L"' local=" + (entry->IsLocal() ? L"true" : L"false") + L"' loaded=" + (entry->IsLoaded() ? L"true" : L"false");;
    Debug(msg, variable->GetLineNumber(), depth);
#endif

    const std::wstring &name = variable->GetName();
    if(HasProgramOrLibraryEnum(name) || HasProgramOrLibraryClass(name)) {
      ProcessError(variable, L"Variable '" + name + L"' already used to define a class, enum or function\n\tIf passing a function reference ensure the full signature is provided");
    }

    // A jagged array's row, 'rows[i]', is an array value (#1246). Its type is
    // settled first: the calls attached to it below are made on the row.
    bool jagged_done = false;
    if(!variable->GetEvalType() && variable->GetIndices() && entry->GetType() &&
       entry->GetType()->GetType() == CLASS_TYPE && IsRowTypeName(entry->GetType()->GetName()) &&
       entry->GetType()->GetDimension() == (int)variable->GetIndices()->GetExpressions().size()) {
      variable->SetTypes(entry->GetType());
      variable->SetEntry(entry);
      entry->AddVariable(variable);
      AnalyzeIndices(variable->GetIndices(), depth + 1);
      AnalyzeJaggedAccess(variable, entry->GetType(), depth);
      jagged_done = true;

      Expression* expression = variable;
      while(expression->GetMethodCall()) {
        AnalyzeExpressionMethodCall(expression, depth + 1);
        expression = expression->GetMethodCall();
      }
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
    if(indices && jagged_done) {
      // analyzed above
    }
    else if(indices) {
      // check dimensions
      if(entry->GetType() && entry->GetType()->GetDimension() == (int)indices->GetExpressions().size()) {
        AnalyzeIndices(indices, depth + 1);
        AnalyzeJaggedAccess(variable, entry->GetType(), depth);
      }
      // note: internal edge case
      else if(!variable->IsInternalVariable()) {
        ProcessError(variable, L"Dimension size mismatch or uninitialized type");
      }
    }
    else if(!variable->GetChainedIndices().empty()) {
      ProcessError(variable, L"Only a jagged array's element takes a second subscript");
    }

    // static check
    if(InvalidStatic(entry) && !CaptureLambdaSelf(variable, entry)) {
      ProcessError(variable, L"Cannot reference an instance variable from this context");
    }
  }
  // lambda expressions
  else if(current_method && current_method->IsLambda()) {
    // resolved through every enclosing lambda: a variable captured by the
    // enclosing lambda lives in that lambda's closure, not its method table
    SymbolEntry* capture_entry = capture_frames.empty() ? nullptr : ResolveCaptureEntry(variable->GetName(), capture_frames.size() - 1);
    if(capture_entry) {
      // capturing a variable in a closure is a use of it; mark the enclosing
      // entry loaded so it isn't reported as unreferenced (see CheckUnreferencedVariables)
      capture_entry->WasLoaded();
      // a captured 'ref' parameter (#1243): the closure holds the cell, the name is its element
      if(capture_entry->IsReference() && !variable->GetIndices()) {
        ExpressionList* zero = TreeFactory::Instance()->MakeExpressionList();
        zero->AddExpression(TreeFactory::Instance()->MakeIntegerLiteral(variable->GetFileName(), variable->GetLineNumber(),
                                                                        variable->GetLinePosition(), 0));
        variable->SetIndices(zero);
      }
      if(capture_lambda->HasClosure(capture_entry)) {
        SymbolEntry* copy_entry = capture_lambda->GetClosure(capture_entry);
        variable->SetTypes(copy_entry->GetType());
        variable->SetEntry(copy_entry);
        copy_entry->AddVariable(variable);
      }
      else {
        const std::wstring var_scope_name = current_method->GetName() + L':' + variable->GetName();
        SymbolEntry* copy_entry = TreeFactory::Instance()->MakeSymbolEntry(var_scope_name, capture_entry->GetType(), false, false);
        copy_entry->SetClosureEntry();
        // a lambda capturing a 'ref' parameter captures its cell (#1243)
        copy_entry->SetReference(capture_entry->IsReference());
        symbol_table->GetSymbolTable(current_class->GetName())->AddEntry(copy_entry, true);

        variable->SetTypes(copy_entry->GetType());
        variable->SetEntry(copy_entry);
        copy_entry->AddVariable(variable);
        capture_lambda->AddClosure(copy_entry, capture_entry);
      }

      // array indices of a captured array: analyzed here exactly as for an
      // explicitly defined variable above. Skipping them left an index that is
      // a variable (or any non-literal expression) without a symbol entry or
      // type, and the emitter dereferenced it (obc crashed with 0xC0000005).
      ExpressionList* indices = variable->GetIndices();
      if(indices) {
        Type* capture_type = capture_entry->GetType();
        if(capture_type && capture_type->GetDimension() == (int)indices->GetExpressions().size()) {
          AnalyzeIndices(indices, depth + 1);
        }
        else if(!variable->IsInternalVariable()) {
          ProcessError(variable, L"Dimension size mismatch or uninitialized type");
        }
      }
    }
    else {
      // not a capture -- a new type-inferred local declared inside the lambda
      // body (e.g. `a := ...` in a `{ }` block body). Create it in the lambda's
      // own scope, mirroring the normal type-inferred-variable path below.
      CheckImplicitDeclaration(variable, is_target);
      const std::wstring scope_name = current_method->GetName() + L':' + variable->GetName();
      SymbolEntry* var_entry = TreeFactory::Instance()->MakeSymbolEntry(scope_name, TypeFactory::Instance()->MakeType(VAR_TYPE), false, true);
      current_table->AddEntry(var_entry, true);
      if(variable->IsAlt()) {
        var_entry->WasLoaded();
      }
      variable->SetTypes(var_entry->GetType());
      variable->SetEntry(var_entry);
      var_entry->AddVariable(variable);
    }
  }
  // type inferred variable
  else if(current_method) {
    CheckImplicitDeclaration(variable, is_target);
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
    ProcessError(variable, L"Undefined variable: '" + variable->GetName() + L"'"
                 L"\n\tCheck spelling, ensure the variable is declared in the current scope, or add a 'use' statement for external bundles.");
  }

  if(variable->GetPreStatement() && variable->GetPostStatement()) {
    ProcessError(variable, L"Variable cannot have pre and post operations");
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
  ResolveSiteScope site_scope(resolve_site, static_cast<Expression*>(method_call));
  const std::wstring variable_name = method_call->GetVariableName();
  const std::wstring method_name = method_call->GetMethodName();

  //
  // the class's own enum, or its nearest parent's, before a used one: 'Type->R'
  //
  const std::wstring scoped_name = SearchScopedEnumName(variable_name, current_class);
  Enum* scoped_eenum = scoped_name.empty() ? nullptr : program->GetEnum(scoped_name);

  // then a program enum before a library one, as for a class: a program's
  // 'ColumnType' with 'use Query.Structured' is its own
  if(!scoped_eenum && scoped_name.empty() && variable_name != current_class->GetName()) {
    scoped_eenum = SearchProgramEnums(variable_name);
  }

  //
  // check library enum reference; fully qualified name
  //
  LibraryEnum* lib_eenum = nullptr;
  if(!scoped_eenum) {
    if(!scoped_name.empty()) {
      lib_eenum = SearchLibraryEnums(scoped_name, program->GetLibUses(current_class->GetFileName()));
    }
    if(!lib_eenum) {
      lib_eenum = SearchLibraryEnums(variable_name + L"#" + method_name,
                                              program->GetLibUses(current_class->GetFileName()));
    }
    if(!lib_eenum) {
      lib_eenum = SearchLibraryEnums(variable_name, program->GetLibUses(current_class->GetFileName()));
    }
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
    Enum* eenum = scoped_eenum;
    if(!eenum) {
      eenum = SearchProgramEnums(enum_name + L"#" + item_name);
      if(eenum && method_call->GetMethodCall()) {
        item_name = method_call->GetMethodCall()->GetVariableName();
      }
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
            // a bundle-level enum is named with its bundle
            const std::wstring enum_child_name = enum_name.substr(result + 1);
            eenum = bundle->GetEnum(var_name + L'.' + enum_child_name);
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
      else if(SearchLibraryClasses(type_of->GetName(), program->GetLibUses(current_class->GetFileName()))) {
        LibraryClass* lib_klass = SearchLibraryClasses(type_of->GetName(), program->GetLibUses(current_class->GetFileName()));
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
 * A subscript on a call's result, 'GetItems()[i]'. The index expressions
 * are ordinary expressions and must be analyzed, or they reach the emitter
 * with no resolved entry -- which segfaults rather than diagnosing, and marks
 * any variable used only as an index as unreferenced.
 *
 * Called for the FIRST link of a chain from AnalyzeMethodCall and for every
 * later link from AnalyzeExpressionMethodCall: the later links never passed
 * through AnalyzeMethodCall, so 'v->Get(r)->Get()[c]' analyzed its receiver
 * and callee but not 'c', and the emitter dereferenced the index variable's
 * missing entry (an access violation in EmitVariable, with no diagnostic).
 ****************************/
void ContextAnalyzer::AnalyzeCallIndices(MethodCall* method_call, const int depth)
{
  ExpressionList* call_indices = method_call->GetCallIndices();
  if(!call_indices) {
    return;
  }

  std::vector<Expression*> index_exprs = call_indices->GetExpressions();
  if(index_exprs.size() != 1) {
    ProcessError(static_cast<Expression*>(method_call),
                 L"Only a single dimension may be indexed on a method call result");
  }

  for(size_t i = 0; i < index_exprs.size(); ++i) {
    AnalyzeExpression(index_exprs[i], depth + 1);

    Type* index_type = GetExpressionType(index_exprs[i], depth + 1);
    if(index_type && index_type->GetType() != INT_TYPE && index_type->GetType() != BYTE_TYPE &&
       index_type->GetType() != CHAR_TYPE) {
      ProcessError(static_cast<Expression*>(method_call), L"Expected an integer index");
    }
  }
}

/****************************
 * True once, for the parent-class attempt at a call whose
 * arguments the class attempt analyzed (see params_analyzed)
 ****************************/
bool ContextAnalyzer::TakeParamsAnalyzed(MethodCall* method_call)
{
  const bool analyzed = params_analyzed == method_call;
  params_analyzed = nullptr;
  if(analyzed) {
    const std::vector<Expression*> expressions = method_call->GetCallingParameters()->GetExpressions();
    for(size_t i = 0; i < expressions.size(); ++i) {
      if(expressions[i]->GetExpressionType() == LAMBDA_EXPR) {
        return false;
      }
    }
  }

  return analyzed;
}

/****************************
 * Analyzes a method call
 ****************************/
void ContextAnalyzer::AnalyzeMethodCall(MethodCall* method_call, const int depth)
{
  ResolveSiteScope site_scope(resolve_site, static_cast<Expression*>(method_call));
#ifdef _DEBUG
  std::wstring msg = L"method/function call: class=" + method_call->GetVariableName() +
    L"; method=" + method_call->GetMethodName() + L"; call_type=" +
    ToString(method_call->GetCallType());
  Debug(msg, (static_cast<Expression*>(method_call))->GetLineNumber(), depth);
#endif

  // Named arguments are consumed where a call binds to a program method
  // (BindNamedArguments) or rejected for a library one. Any other kind of call
  // -- a function value, a lambda, a curried link -- has no parameter names, and
  // must not quietly take them by position (#1244).
  struct NamedArgumentGuard {
    ContextAnalyzer* analyzer;
    MethodCall* call;
    ~NamedArgumentGuard() {
      ExpressionList* params = call->GetCallingParameters();
      if(params && params->HasNames()) {
        params->ClearNames();
        analyzer->ProcessError(static_cast<Expression*>(call), L"Named arguments need a method or function of a class; this call has no parameter names");
      }
    }
  } named_argument_guard = { this, method_call };

  AnalyzeCallIndices(method_call, depth);

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

    // A method's own type parameter is not a class (#1242): 'T->New()' or
    // 'T->F()' in 'Id<T>(..)' took whatever class named 'T' the uses reached.
    if(!entry && !method_call->GetVariable() && current_method && current_method->HasGenericName(variable_name)) {
      ProcessError(static_cast<Expression*>(method_call), L"Type parameter '" + variable_name +
                   L"' is not a class; it has no constructor or functions of its own");
      --nested_call_depth;
      return;
    }

    // Try()/Otherwise() are resolved here, before any class lookup. Neither
    // intrinsic needs one -- both work off the receiver's type -- and the
    // lookup does not resolve for an indexed receiver, so the checks that used
    // to live inside the program/library branches below were never reached for
    // 'arr[i]->Try()->m()'. It fell through to the variable path and reported
    // "Invalid class type or assignment", even though 'arr[i]->m()' resolves.
    if(method_call->GetCallType() == METHOD_CALL) {
      const std::wstring intrinsic_name = method_call->GetMethodName();
      const bool has_params = method_call->GetCallingParameters() != nullptr;
      const size_t param_count = has_params ? method_call->GetCallingParameters()->GetExpressions().size() : 0;
      const bool is_try = intrinsic_name == L"Try" && has_params && param_count == 0;
      const bool is_otherwise = intrinsic_name == L"Otherwise" && has_params && param_count == 1;

      if(is_try || is_otherwise) {
        if(entry && entry->GetType()) {
          Variable* recv_variable = method_call->GetVariable();
          if(recv_variable && recv_variable->GetIndices()) {
            // an indexed receiver is one element, so the chain has to resolve
            // against the element type rather than the array type
            Type* element_type = TypeFactory::Instance()->MakeType(entry->GetType());
            element_type->SetDimension(0);
            method_call->SetEvalType(element_type, false);
          }
          else {
            method_call->SetEvalType(entry->GetType(), false);
          }
        }

        if(is_try) {
          AnalyzeTryIntrinsic(method_call, static_cast<Expression*>(method_call), depth);
        }
        else {
          AnalyzeOtherwiseIntrinsic(method_call, static_cast<Expression*>(method_call), depth);
        }

        --nested_call_depth;
        RogueReturn(method_call);
        return;
      }
    }

    // local call
    std::wstring encoding;
    Class* klass = AnalyzeProgramMethodCall(method_call, encoding, depth);
    if(klass) {
      // check for Try()/Otherwise() intrinsics
      if(method_call->GetCallType() == METHOD_CALL) {
        const std::wstring mthd_name = method_call->GetMethodName();
        const bool has_params = method_call->GetCallingParameters() != nullptr;
        const size_t param_count = has_params ? method_call->GetCallingParameters()->GetExpressions().size() : 0;

        if(mthd_name == L"Try" && has_params && param_count == 0) {
          // Set receiver type from entry
          if(entry && entry->GetType()) {
            method_call->SetEvalType(entry->GetType(), false);
          }
          AnalyzeTryIntrinsic(method_call, static_cast<Expression*>(method_call), depth);
          --nested_call_depth;
          RogueReturn(method_call);
          return;
        }
        if(mthd_name == L"Otherwise" && has_params && param_count == 1) {
          if(entry && entry->GetType()) {
            method_call->SetEvalType(entry->GetType(), false);
          }
          AnalyzeOtherwiseIntrinsic(method_call, static_cast<Expression*>(method_call), depth);
          --nested_call_depth;
          RogueReturn(method_call);
          return;
        }
      }

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
      // check for Try()/Otherwise() intrinsics
      if(method_call->GetCallType() == METHOD_CALL) {
        const std::wstring mthd_name = method_call->GetMethodName();
        const bool has_params = method_call->GetCallingParameters() != nullptr;
        const size_t param_count = has_params ? method_call->GetCallingParameters()->GetExpressions().size() : 0;

        if(mthd_name == L"Try" && has_params && param_count == 0) {
          if(entry && entry->GetType()) {
            method_call->SetEvalType(entry->GetType(), false);
          }
          AnalyzeTryIntrinsic(method_call, static_cast<Expression*>(method_call), depth);
          --nested_call_depth;
          RogueReturn(method_call);
          return;
        }
        if(mthd_name == L"Otherwise" && has_params && param_count == 1) {
          if(entry && entry->GetType()) {
            method_call->SetEvalType(entry->GetType(), false);
          }
          AnalyzeOtherwiseIntrinsic(method_call, static_cast<Expression*>(method_call), depth);
          --nested_call_depth;
          RogueReturn(method_call);
          return;
        }
      }

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
           klass, lib_klass, is_enum_call) && !IsCascade(entry)) {
          ProcessError(static_cast<Expression*>(method_call), L"Invalid class type or assignment");
        }
      }
      else {
        if(!AnalyzeExpressionMethodCall(entry, encoding, klass, lib_klass) && !IsCascade(entry)) {
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
          if(!IsCascade(entry)) {
            ProcessError(static_cast<Expression*>(method_call), L"'" + variable_name + L"' does not resolve to a class; check its declared type");
          }
        }
        else {
          ProcessError(static_cast<Expression*>(method_call), L"Undefined class or method call: '" + method_call->GetMethodName() + L"'");
        }
      }
    }
    else {
      if(!variable_name.empty()) {
        // classes are written with a capital; a lower-case name before '->' is far
        // more likely a variable that was misspelled or never declared
        if(variable_name[0] == L'@' || !iswupper(variable_name[0])) {
          ProcessError(static_cast<Expression*>(method_call), L"Undefined variable: '" + variable_name + L"'"
                       L"\n\t'" + variable_name + L"' comes before '->', so it must be a variable or a class; no variable by that name is declared here and no class is defined with it" +
                       VariableSuggestion(variable_name));
        }
        else {
          ProcessError(static_cast<Expression*>(method_call), L"Undefined class: '" + variable_name + L"'" +
                       (variable_name == L"API.LlmUtil.Prompt" ? std::wstring(L"\n\tA prompt literal is made with the llm_util library: compile with -lib llm_util,regex,json,gen_collect")
                                                               : BundleSuggestion(variable_name)));
        }
        undefined_classes.insert(variable_name);
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

void ContextAnalyzer::ValidateGenericConcreteMapping(const std::vector<Type*> concrete_types, LibraryClass* lib_klass, ParseNode* node, Class* scope)
{
  const std::vector<LibraryClass*> class_generics = lib_klass->GetGenericClasses();
  if(class_generics.size() != concrete_types.size()) {
    ProcessError(node, L"Generic type mismatch for class '" + lib_klass->GetName() +
                 L"': expected " + ToString(class_generics.size()) +
                 L" type parameter(s), got " + ToString(concrete_types.size()));
  }
  // check individual types
  if(class_generics.size() == concrete_types.size()) {
    for(size_t i = 0; i < concrete_types.size(); ++i) {
      Type* concrete_type = concrete_types[i];
      LibraryClass* class_generic = class_generics[i];
      if(class_generic->HasGenericInterface()) {
        const std::wstring concrete_name = concrete_type->GetName();
        Class* inf_klass = nullptr; LibraryClass* inf_lib_klass = nullptr;
        bool resolved = GetProgramOrLibraryClass(concrete_type, inf_klass, inf_lib_klass);
        if(!resolved) {
          inf_klass = (scope ? scope : current_class)->GetGenericClass(concrete_name);
          resolved = (inf_klass != nullptr);
        }
        if(resolved) {
          // the concrete class must satisfy EVERY bound (T : A & B & ...)
          const std::vector<Type*> bounds = class_generic->GetAllGenericInterfaces();
          for(size_t b = 0; b < bounds.size(); ++b) {
            const std::wstring backing_inf_name = bounds[b]->GetName();
            if(!ValidDownCast(backing_inf_name, inf_klass, inf_lib_klass)) {
              ProcessError(node, L"Concrete class: '" + concrete_name +
                           L"' is incompatible with backing class/interface '" + backing_inf_name + L"'");
            }
          }
        }
        else {
          ProcessError(node, L"Undefined class or interface: '" + FormatTypeString(concrete_name) + L"'");
        }
      }
      else {
        const std::wstring concrete_type_name = concrete_type->GetName();
        Class* generic_klass = nullptr; LibraryClass* generic_lib_klass = nullptr;
        if(!GetProgramOrLibraryClass(concrete_type, generic_klass, generic_lib_klass) &&
           !(scope ? scope : current_class)->GetGenericClass(concrete_type_name)) {
          ProcessError(node, L"Undefined class or interface: '" + FormatTypeString(concrete_type_name) + L"'");
        }
      }
    }
  }
}

void ContextAnalyzer::ValidateGenericConcreteMapping(const std::vector<Type*> concrete_types, Class* klass, ParseNode* node, Class* scope)
{
  const std::vector<Class*> class_generics = klass->GetGenericClasses();
  if(class_generics.size() != concrete_types.size()) {
    ProcessError(node, L"Generic type mismatch for class '" + klass->GetName() +
                 L"': expected " + ToString(class_generics.size()) +
                 L" type parameter(s), got " + ToString(concrete_types.size()));
  }
  // check individual types
  if(class_generics.size() == concrete_types.size()) {
    for(size_t i = 0; i < concrete_types.size(); ++i) {
      Type* concrete_type = concrete_types[i];
      ResolveClassEnumType(concrete_type);

      Class* class_generic = class_generics[i];
      if(class_generic->HasGenericInterface()) {
        const std::wstring concrete_name = concrete_type->GetName();
        Class* inf_klass = nullptr; LibraryClass* inf_lib_klass = nullptr;
        bool resolved = GetProgramOrLibraryClass(concrete_type, inf_klass, inf_lib_klass);
        if(!resolved) {
          inf_klass = (scope ? scope : current_class)->GetGenericClass(concrete_name);
          resolved = (inf_klass != nullptr);
        }
        if(resolved) {
          // the concrete class must satisfy EVERY bound (T : A & B & ...)
          const std::vector<Type*> bounds = class_generic->GetAllGenericInterfaces();
          for(size_t b = 0; b < bounds.size(); ++b) {
            const std::wstring backing_inf_name = GetProgramOrLibraryClassName(bounds[b]->GetName());
            if(!ValidDownCast(backing_inf_name, inf_klass, inf_lib_klass)) {
              ProcessError(node, L"Concrete class: '" + concrete_name +
                           L"' is incompatible with backing class/interface '" + backing_inf_name + L"'");
            }
          }
        }
        else {
          ProcessError(node, L"Undefined class or interface: '" + FormatTypeString(concrete_name) + L"'");
        }
      }
      else {
        const std::wstring concrete_type_name = concrete_type->GetName();
        Class* generic_klass = nullptr; LibraryClass* generic_lib_klass = nullptr;
        if(!GetProgramOrLibraryClass(concrete_type, generic_klass, generic_lib_klass) &&
           !(scope ? scope : current_class)->GetGenericClass(concrete_type_name)) {
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
 * True when a call's cast is the cast of its receiver variable, which the parser
 * copies to the call for 'x->As(T)->M()' (the call then dispatches on the cast
 * type). It converts the receiver, not M's return value, so it must not be
 * validated against the return, nor taken as the call's type: 'e->As(Int)->
 * PrintLine()' failed "Cannot cast a Nil return value", 'e->As(Int)->ToString()'
 * an Int/String cast error, and for a class cast 'u->As(IntRef)->Get()->
 * ToString()' an IntRef/Int one.
 ****************************/
static bool IsReceiverCast(Expression* expression)
{
  if(expression->GetExpressionType() != METHOD_CALL_EXPR) {
    return false;
  }

  Type* cast_type = expression->GetCastType();
  Variable* variable = static_cast<MethodCall*>(expression)->GetVariable();
  if(!cast_type || !variable || !variable->GetCastType()) {
    return false;
  }

  Type* var_cast_type = variable->GetCastType();
  if(var_cast_type->GetType() != cast_type->GetType() || var_cast_type->GetDimension() != cast_type->GetDimension()) {
    return false;
  }

  // the parser copies the variable's Type, so a class cast is the same class name
  return cast_type->GetType() != CLASS_TYPE || var_cast_type->GetName() == cast_type->GetName();
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
      if(static_cast<Variable*>(expression)->GetIndices() && !expression->GetCastType()) {
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
    lib_klass = SearchLibraryClasses(BOOL_CLASS_ID, program->GetLibUses(current_class->GetFileName()));
    encoding = L"l";
    break;

  case VAR_TYPE:
  case NIL_TYPE:
    return false;

  case BYTE_TYPE:
    klass = program->GetClass(BYTE_CLASS_ID);
    lib_klass = SearchLibraryClasses(BYTE_CLASS_ID, program->GetLibUses(current_class->GetFileName()));
    encoding = L"b";
    break;

  case CHAR_TYPE:
    klass = program->GetClass(CHAR_CLASS_ID);
    lib_klass = SearchLibraryClasses(CHAR_CLASS_ID, program->GetLibUses(current_class->GetFileName()));
    encoding = L"c";
    break;

  case INT_TYPE:
    klass = program->GetClass(INT_CLASS_ID);
    lib_klass = SearchLibraryClasses(INT_CLASS_ID, program->GetLibUses(current_class->GetFileName()));
    encoding = L"i";
    break;

  case FLOAT_TYPE:
    klass = program->GetClass(FLOAT_CLASS_ID);
    lib_klass = SearchLibraryClasses(FLOAT_CLASS_ID, program->GetLibUses(current_class->GetFileName()));
    encoding = L"f";
    break;

  case CLASS_TYPE: {
    if(dimension > 0 && type->GetDimension() > 0) {
      klass = program->GetClass(BASE_ARRAY_CLASS_ID);
      lib_klass = SearchLibraryClasses(BASE_ARRAY_CLASS_ID, program->GetLibUses(current_class->GetFileName()));
      encoding = L"o.System.Base";
    }
    else {
      const std::wstring &cls_name = type->GetName();
      klass = SearchProgramClasses(cls_name);
      lib_klass = SearchLibraryClasses(cls_name, program->GetLibUses(current_class->GetFileName()));

      if(!klass && !lib_klass) {
        if(HasProgramOrLibraryEnum(cls_name)) {
          klass = program->GetClass(INT_CLASS_ID);
          lib_klass = SearchLibraryClasses(INT_CLASS_ID, program->GetLibUses(current_class->GetFileName()));
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
  else if(expressions.size() > 8) {
    ProcessError(static_cast<Expression*>(method_call), L"Array dimensions cannot exceed 8");
  }
  
  // TODO: check for dimension size of 1, looking at type
  // a copy of an array: an indexed variable is a size, not a source (IsArrayCopySource)
  else if(method_call->GetEvalType() && expressions.size() == 1 && IsArrayCopySource(expressions[0]) && expressions[0]->GetEvalType()->GetDimension() == 1) {
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
    // 'F(a)(b)': call the function value the previous link returned
    else if(method_call->IsCurriedCall()) {
      AnalyzeCurriedCall(expression, method_call, depth);
    }
    else {
      // a hole of a prompt literal is checked before the call that fills it
      if(method_call->GetPromptHole()) {
        AnalyzePromptHole(method_call, depth);
      }

      // track rouge return
      ++nested_call_depth;

      // a subscript on THIS link's result; the first link's is handled by
      // AnalyzeMethodCall, which later links do not pass through
      AnalyzeCallIndices(method_call, depth);

      std::wstring encoding;
      Class* klass = nullptr;
      LibraryClass* lib_klass = nullptr;

      // check expression class
      bool is_enum_call = false;
      if(!AnalyzeExpressionMethodCall(expression, encoding, klass, lib_klass, is_enum_call) && !AnalyzeExpressionMethodCall(method_call, encoding, klass, lib_klass, is_enum_call)) {
          ProcessError(static_cast<Expression*>(method_call), L"Invalid class type or assignment");
      }
      method_call->SetEnumCall(is_enum_call);

      // check for Try()/Otherwise() intrinsics before normal method resolution
      if(method_call->GetCallType() == METHOD_CALL) {
        const std::wstring mthd_name = method_call->GetMethodName();
        const bool has_params = method_call->GetCallingParameters() != nullptr;
        const size_t param_count = has_params ? method_call->GetCallingParameters()->GetExpressions().size() : 0;

        if(mthd_name == L"Try" && has_params && param_count == 0) {
          AnalyzeTryIntrinsic(method_call, expression, depth);
          --nested_call_depth;
          RogueReturn(method_call);
          return;
        }

        if(mthd_name == L"Otherwise" && has_params && param_count == 1) {
          AnalyzeOtherwiseIntrinsic(method_call, expression, depth);
          --nested_call_depth;
          RogueReturn(method_call);
          return;
        }
      }

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

      // `h->Find("a")->TypeOf(IntRef)`: the call's value is the Bool the check
      // pushes, not the call's return type. Left as the return type, `b := ...`
      // declared b a String/IntRef and `b->PrintLine()` crashed on the Bool.
      if(method_call->GetTypeOf()) {
        AnalyzeCast(method_call, depth + 1);
      }

      // check for rouge return
      --nested_call_depth;
      RogueReturn(method_call);
    }
  }
}

/****************************
 * A hole of a prompt literal, '{name}' or '{name : Type}', lowered by the parser
 * to 'Slot("name", name)' (docs/design/AGENT_SYNTAX.md). The variable must exist
 * and hold a String, Int, Float, Bool, Char or Byte, and a declared type must be
 * the variable's type exactly. An error is reported at the hole; the value is
 * then replaced by an empty string, so the call resolves and the holes after it
 * are still checked.
 ****************************/
void ContextAnalyzer::AnalyzePromptHole(MethodCall* method_call, const int depth)
{
  ExpressionList* arguments = method_call->GetCallingParameters();
  if(!arguments || arguments->GetExpressions().size() != 2 || arguments->GetExpressions()[1]->GetExpressionType() != VAR_EXPR) {
    return;
  }
  Variable* variable = static_cast<Variable*>(arguments->GetExpressions()[1]);
  const std::wstring name = variable->GetName();

  AnalyzeExpression(variable, depth + 1);
  Type* type = variable->GetEvalType();

  // a name that is not declared has been reported as that, and has no type to check
  bool valid = false;
  if(type && type->GetType() != VAR_TYPE) {
    // what the hole's value is: 2 String, 3 Int, 4 Float, 5 Bool, 6 Char, 7 Byte, as MethodCall::GetPromptHole
    int found = 0;
    if(type->GetDimension() == 0) {
      switch(type->GetType()) {
      case CLASS_TYPE:
        found = (type->GetName() == L"System.String" || type->GetName() == L"String") ? 2 : 0;
        break;
      case INT_TYPE: found = 3; break;
      case FLOAT_TYPE: found = 4; break;
      case BOOLEAN_TYPE: found = 5; break;
      case CHAR_TYPE: found = 6; break;
      case BYTE_TYPE: found = 7; break;
      default: break;
      }
    }

    static const wchar_t* const names[] = { L"", L"", L"String", L"Int", L"Float", L"Bool", L"Char", L"Byte" };
    const int declared = method_call->GetPromptHole();
    if(!found) {
      ProcessError(static_cast<Expression*>(method_call), L"The prompt hole '{" + name + L"}' cannot take '" + name + L"', which is " + DescribeType(type) +
                   L": a hole takes a String, Int, Float, Bool, Char or Byte\n\tPut the text to send in a String variable first, then name that variable in the hole");
    }
    else if(declared > 1 && declared != found) {
      ProcessError(static_cast<Expression*>(method_call), L"The prompt hole '{" + name + L" : " + names[declared] + L"}' declares " + names[declared] +
                   L", but '" + name + L"' is " + DescribeType(type));
    }
    else {
      valid = true;
    }
  }

  if(!valid) {
    arguments->SetExpression(TreeFactory::Instance()->MakeCharacterString(variable->GetFileName(), variable->GetLineNumber(), variable->GetLinePosition(), L"", true), 1);
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
      else {
        // A discarded constructor call ('Foo->New();' as a statement) leaves
        // the new instance on the operand stack like any other object-returning
        // call and must be popped. Excluding NEW_INST_CALL here fell through to
        // NIL_TYPE, dropping the POP_INT, so the interpreter's operand stack
        // grew one slot per execution (a loop overflowed it and crashed).
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

      klass = SearchLibraryClasses(BASE_ARRAY_CLASS_ID, program->GetLibUses(current_class->GetFileName()));
      encoding = L"o.System.Base";
      for(int i = 0; i < entry->GetType()->GetDimension(); ++i) {
        encoding += L"*";
      }
      encoding += L",";
    }
    // cast type
    else if(method_call->GetVariable() && method_call->GetVariable()->GetCastType() &&
            method_call->GetVariable()->GetCastType()->GetType() == CLASS_TYPE) {
      klass = SearchLibraryClasses(method_call->GetVariable()->GetCastType()->GetName(),
                                           program->GetLibUses(current_class->GetFileName()));
      method_call->SetTypes(entry->GetType());
    }
    else {
      klass = SearchLibraryClasses(entry->GetType()->GetName(), program->GetLibUses(current_class->GetFileName()));
      if(!klass && current_class->HasGenerics()) {
        Class* generic_class = current_class->GetGenericClass(entry->GetType()->GetName());
        if(generic_class) {
          return SearchLibraryClasses(L"System.Base", program->GetLibUses(current_class->GetFileName()));
        }
      }
    }
  }
  // static method call
  if(!klass) {
    klass = SearchLibraryClasses(variable_name, program->GetLibUses(current_class->GetFileName()));
  }

  if(method_call->GetCastType() && method_call->GetCastType()->GetType() == CLASS_TYPE) {
    AnalyzeVariableCast(method_call->GetCastType(), method_call);
  }

  return klass;
}

/*********************************
 * The types analysis has given
 * each argument (at the end of its
 * call chain), and their return
 *********************************/
std::vector<ContextAnalyzer::ArgumentTypes> ContextAnalyzer::SaveArgumentTypes(const std::vector<Expression*> &arguments)
{
  std::vector<ArgumentTypes> saved;
  for(size_t i = 0; i < arguments.size(); ++i) {
    Expression* last = arguments[i];
    while(last && last->GetMethodCall()) {
      last = last->GetMethodCall();
    }
    if(last) {
      ArgumentTypes types;
      types.expression = last;
      last->GetTypeState(types.base_type, types.eval_type, types.cast_type);
      types.to_class = last->GetToClass();
      types.to_lib_class = last->GetToLibraryClass();
      saved.push_back(types);
    }
  }

  return saved;
}

void ContextAnalyzer::RestoreArgumentTypes(const std::vector<ArgumentTypes> &saved)
{
  for(size_t i = 0; i < saved.size(); ++i) {
    saved[i].expression->SetTypeState(saved[i].base_type, saved[i].eval_type, saved[i].cast_type);
    saved[i].expression->SetToClass(saved[i].to_class);
    saved[i].expression->SetToLibraryClass(saved[i].to_lib_class);
  }
}

/*********************************
 * A holder variable passed for a
 * primitive parameter: the argument
 * becomes the call that reads its
 * value. (A call's result is
 * unboxed in place.)
 *********************************/
void ContextAnalyzer::UnboxedArgument(MethodCall* method_call, const size_t index, Expression* argument, Type* parameter, Expression* unboxed)
{
  if(!unboxed || !argument || !parameter || parameter->GetDimension() > 0 || argument->GetExpressionType() != VAR_EXPR ||
     unboxed == argument) {
    return;
  }

  switch(parameter->GetType()) {
  case BOOLEAN_TYPE:
  case BYTE_TYPE:
  case CHAR_TYPE:
  case INT_TYPE:
  case FLOAT_TYPE:
    break;

  default:
    return;
  }

  std::vector<Expression*> arguments = method_call->GetCallingParameters()->GetExpressions();
  if(index < arguments.size() && arguments[index] == argument) {
    arguments[index] = unboxed;
    method_call->GetCallingParameters()->SetExpressions(arguments);
  }
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

      // Nil is a value of every array type, as it is of every class: a relative
      // match, so 'F(Nil)' with F(String[]) and F(Compare) is ambiguous, as it
      // is between two class parameters. Only a typed Nil variable bound before.
      if(calling_type->GetType() == NIL_TYPE && method_type->GetDimension() > 0) {
        return 1;
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
                // here the call argument (calling_type) is the source and the
                // method parameter (method_type) is the target, so left_is_target=false
                if(CheckGenericEqualTypes(calling_type, method_type, calling_param, true, false)) {
                  return 0;
                }
                return -1;
              }
              return 0;
            }
            // calculate relative match
            const std::wstring &from_klass_name = calling_type->GetName();
            Class* from_klass = SearchProgramClasses(from_klass_name);
            LibraryClass* from_lib_klass = SearchLibraryClasses(from_klass_name, program->GetLibUses(current_class->GetFileName()));

            const std::wstring &to_klass_name = method_type->GetName();
            Class* to_klass = SearchProgramClasses(to_klass_name);
            if(to_klass) {
              return ValidDownCast(to_klass->GetName(), from_klass, from_lib_klass) ? 1 : -1;
            }

            LibraryClass* to_lib_klass = SearchLibraryClasses(to_klass_name, program->GetLibUses(current_class->GetFileName()));
            if(to_lib_klass) {
              return ValidDownCast(to_lib_klass->GetName(), from_klass, from_lib_klass) ? 1 : -1;
            }
          }
          else if(method_type->GetType() == INT_TYPE) {
            // program
            if(program->GetEnum(calling_type->GetName()) ||
               SearchLibraryEnums(calling_type->GetName(), program->GetLibUses())) {
              return 1;
            }
          }

          // a holder for a primitive parameter is its value: an IntRef for an
          // Int (2), or for a Float once converted (3). It fits less well than
          // an argument that needs no unboxing.
          EntryType held;
          if(method_type->GetType() != CLASS_TYPE && method_type->GetType() != FUNC_TYPE && method_type->GetDimension() == 0 &&
             HolderPrimitive(calling_type, held) &&
             PrimitiveConverts(method_type->GetType(), held)) {
            return held == method_type->GetType() ? 2 : 3;
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
 * The ToString an object operand
 * of a concatenation calls: its
 * class's own, else the nearest
 * ancestor's, ending at System.Base
 * (which defers to Stringify).
 * False when the class is unknown;
 * true with neither set when the
 * nearest one is private.
 ****************************/
bool ContextAnalyzer::ResolveConcatToString(const std::wstring &cls_name, Method* &method, LibraryMethod* &lib_method)
{
  method = nullptr;
  lib_method = nullptr;

  std::wstring name = cls_name;
  bool found_class = false;
  // a class cannot be its own ancestor, but bound the walk all the same
  for(int depth = 0; depth < 64 && !name.empty(); ++depth) {
    Class* klass = SearchProgramClasses(name);
    if(klass) {
      found_class = true;
      Method* candidate = klass->GetMethod(klass->GetName() + L":ToString:");
      if(candidate) {
        if(candidate->GetMethodType() != PRIVATE_METHOD) {
          method = candidate;
        }
        return true;
      }
      name = klass->GetParentName();
      continue;
    }

    LibraryClass* lib_klass = SearchLibraryClasses(name, program->GetLibUses());
    if(lib_klass) {
      found_class = true;
      LibraryMethod* candidate = lib_klass->GetMethod(lib_klass->GetName() + L":ToString:");
      if(candidate) {
        if(candidate->GetMethodType() != PRIVATE_METHOD) {
          lib_method = candidate;
        }
        return true;
      }
      name = lib_klass->GetParentName();
      continue;
    }

    break;
  }

  if(!found_class) {
    return false;
  }

  // every class ends at System.Base, whatever its declared chain says
  LibraryClass* base_klass = SearchLibraryClasses(L"System.Base", program->GetLibUses());
  if(base_klass) {
    LibraryMethod* candidate = base_klass->GetMethod(L"System.Base:ToString:");
    if(candidate && candidate->GetMethodType() != PRIVATE_METHOD) {
      lib_method = candidate;
    }
  }
  else {
    Class* klass = SearchProgramClasses(L"System.Base");
    if(klass) {
      Method* candidate = klass->GetMethod(L"System.Base:ToString:");
      if(candidate && candidate->GetMethodType() != PRIVATE_METHOD) {
        method = candidate;
      }
    }
  }
  return true;
}

/****************************
 * The receiver of a call that
 * passes it as parameter 0 of a
 * '$' class function -- a
 * primitive ('v->Pow(2)') or a
 * whole array ('a->Fill(0)'):
 * the type its encoding names,
 * or nullptr for any other call
 ****************************/
Type* ContextAnalyzer::ReceiverParameterType(const std::wstring &encoding)
{
  // AnalyzeExpressionMethodCall: "i," for an Int, "b**," for a Byte[,], and
  // "o.System.Base*," for an object array
  if(encoding.size() < 2 || encoding.back() != L',') {
    return nullptr;
  }

  // An object array's functions are on '$BaseArray' and take it as Base[]. Like a
  // primitive, it must match only functions that take it: matched on the written
  // arguments alone, 'names->ToString()' found nothing on $BaseArray, walked up to
  // its implicit parent and bound Base's instance ToString() to a static call.
  const std::wstring object_array = L"o.System.Base";
  if(encoding.compare(0, object_array.size(), object_array) == 0) {
    int dimension = 0;
    for(size_t i = object_array.size(); i + 1 < encoding.size(); ++i) {
      if(encoding[i] != L'*') {
        return nullptr;
      }
      ++dimension;
    }
    if(dimension == 0) {
      return nullptr;
    }

    Type* receiver = TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.Base");
    receiver->SetDimension(dimension);
    return receiver;
  }

  EntryType type;
  switch(encoding[0]) {
  case L'l':
    type = BOOLEAN_TYPE;
    break;

  case L'b':
    type = BYTE_TYPE;
    break;

  case L'c':
    type = CHAR_TYPE;
    break;

  case L'i':
    type = INT_TYPE;
    break;

  case L'f':
    type = FLOAT_TYPE;
    break;

  default:
    return nullptr;
  }

  int dimension = 0;
  for(size_t i = 1; i + 1 < encoding.size(); ++i) {
    if(encoding[i] != L'*') {
      return nullptr;
    }
    ++dimension;
  }

  Type* receiver = TypeFactory::Instance()->MakeType(type);
  receiver->SetDimension(dimension);
  return receiver;
}

/****************************
 * Whether a function's first
 * parameter takes the receiver
 ****************************/
bool ContextAnalyzer::IsReceiverParameter(Type* parameter, Type* receiver)
{
  return parameter && receiver && parameter->GetType() == receiver->GetType() &&
    parameter->GetDimension() == receiver->GetDimension();
}

/****************************
 * Whether a method declares its
 * own type parameters (#1242):
 * a parameter or its return
 * type is marked with one
 ****************************/
bool ContextAnalyzer::IsGenericMethod(Method* method)
{
  if(method->GetReturn() && method->GetReturn()->HasMethodGeneric()) {
    return true;
  }
  std::vector<Declaration*> declarations = method->GetDeclarations()->GetDeclarations();
  for(size_t i = 0; i < declarations.size(); ++i) {
    SymbolEntry* entry = declarations[i]->GetEntry();
    if(entry && entry->GetType() && entry->GetType()->HasMethodGeneric()) {
      return true;
    }
  }
  return false;
}

/****************************
 * Binds a method type parameter
 * by matching a parameter type
 * against an argument's: 'T',
 * 'T[]' and 'C<.., T, ..>'
 ****************************/
bool ContextAnalyzer::UnifyMethodGeneric(Type* param, Type* arg, int arg_dimension, std::map<std::wstring, Type*> &bindings,
                                         std::map<std::wstring, std::wstring> &bounds, std::wstring &error)
{
  const std::wstring &name = param->GetMethodGeneric();
  if(!name.empty()) {
    bounds[name] = param->GetName();
    if(arg->GetType() == NIL_TYPE) {
      return true;
    }
    if(arg->GetType() != CLASS_TYPE) {
      error = L"Type parameter '" + name + L"' takes an object; box a primitive argument (IntRef, FloatRef, ...)";
      return false;
    }
    if(arg_dimension < param->GetDimension()) {
      error = L"Type parameter '" + name + L"': the argument has fewer array dimensions than the parameter";
      return false;
    }

    Type* binding = TypeFactory::Instance()->MakeType(arg);
    binding->SetDimension(arg_dimension - param->GetDimension());
    binding->SetMethodGeneric(L"");
    ResolveClassEnumType(binding);

    std::map<std::wstring, Type*>::iterator found = bindings.find(name);
    if(found == bindings.end()) {
      bindings[name] = binding;
    }
    else if(found->second->GetName() != binding->GetName() || found->second->GetDimension() != binding->GetDimension()) {
      error = L"Type parameter '" + name + L"' is bound to both '" + FormatTypeString(found->second->GetName()) +
        L"' and '" + FormatTypeString(binding->GetName()) + L"'";
      return false;
    }
    return true;
  }

  // (A, ..) ~ R: match a function argument's parameters and return (#1242)
  if(param->GetType() == FUNC_TYPE) {
    if(arg->GetType() != FUNC_TYPE) {
      return true;
    }
    const std::vector<Type*> param_funcs = param->GetFunctionParameters();
    const std::vector<Type*> arg_funcs = arg->GetFunctionParameters();
    if(param_funcs.size() == arg_funcs.size()) {
      for(size_t i = 0; i < param_funcs.size(); ++i) {
        if(param_funcs[i] && arg_funcs[i] && param_funcs[i]->HasMethodGeneric() &&
           !UnifyMethodGeneric(param_funcs[i], arg_funcs[i], arg_funcs[i]->GetDimension(), bindings, bounds, error)) {
          return false;
        }
      }
    }
    Type* param_rtrn = param->GetFunctionReturn();
    Type* arg_rtrn = arg->GetFunctionReturn();
    if(param_rtrn && arg_rtrn && param_rtrn->HasMethodGeneric()) {
      return UnifyMethodGeneric(param_rtrn, arg_rtrn, arg_rtrn->GetDimension(), bindings, bounds, error);
    }
    return true;
  }

  // C<.., T, ..>: match the type arguments pairwise
  const std::vector<Type*> param_generics = param->GetGenerics();
  const std::vector<Type*> arg_generics = arg->GetGenerics();
  if(param_generics.size() == arg_generics.size()) {
    for(size_t i = 0; i < param_generics.size(); ++i) {
      if(param_generics[i]->HasMethodGeneric() &&
         !UnifyMethodGeneric(param_generics[i], arg_generics[i], arg_generics[i]->GetDimension(), bindings, bounds, error)) {
        return false;
      }
    }
  }
  return true;
}

/****************************
 * Infers a generic method's type
 * arguments from a call's
 * arguments, and checks each
 * against its bound (#1242)
 ****************************/
bool ContextAnalyzer::InferMethodGenerics(Method* method, const std::vector<Expression*> &arguments,
                                          std::map<std::wstring, Type*> &bindings, std::wstring &error, const int depth)
{
  std::map<std::wstring, std::wstring> bounds;
  std::vector<Declaration*> declarations = method->GetDeclarations()->GetDeclarations();
  for(size_t i = 0; i < declarations.size() && i < arguments.size(); ++i) {
    Type* param = declarations[i]->GetEntry() ? declarations[i]->GetEntry()->GetType() : nullptr;
    if(!param || !param->HasMethodGeneric()) {
      continue;
    }

    Type* arg = arguments[i]->GetExpressionType() == LAMBDA_EXPR ? static_cast<Lambda*>(arguments[i])->GetLambdaType() :
      GetExpressionType(arguments[i], depth + 1);
    if(!arg) {
      if(arguments[i]->GetExpressionType() == LAMBDA_EXPR) {
        continue;
      }
      return false;
    }
    const int arg_dimension = IsScalar(arguments[i]) ? 0 : arg->GetDimension();
    if(!UnifyMethodGeneric(param, arg, arg_dimension, bindings, bounds, error)) {
      return false;
    }
  }

  // every binding meets its bound
  for(std::map<std::wstring, Type*>::iterator iter = bindings.begin(); iter != bindings.end(); ++iter) {
    const std::wstring bound_name = bounds[iter->first];
    if(bound_name.empty() || bound_name == L"System.Base" || iter->second->GetDimension() > 0) {
      continue;
    }

    Type* bound = TypeFactory::Instance()->MakeType(CLASS_TYPE, bound_name);
    ResolveClassEnumType(bound);
    Class* klass = nullptr;
    LibraryClass* lib_klass = nullptr;
    if(GetProgramOrLibraryClass(iter->second, klass, lib_klass) && !ValidDownCast(bound->GetName(), klass, lib_klass)) {
      error = L"Type parameter '" + iter->first + L"': '" + FormatTypeString(iter->second->GetName()) +
        L"' does not meet its bound '" + FormatTypeString(bound->GetName()) + L"'";
      return false;
    }
  }

  return true;
}

/****************************
 * A type with a call's inferred
 * type arguments in place of
 * the method's type parameters
 ****************************/
Type* ContextAnalyzer::SubstituteMethodGenerics(Type* type, const std::map<std::wstring, Type*> &bindings)
{
  if(!type || bindings.empty() || !type->HasMethodGeneric()) {
    return type;
  }

  const std::wstring &name = type->GetMethodGeneric();
  if(!name.empty()) {
    std::map<std::wstring, Type*>::const_iterator found = bindings.find(name);
    if(found == bindings.end()) {
      return type;
    }
    Type* result = TypeFactory::Instance()->MakeType(found->second);
    result->SetDimension(found->second->GetDimension() + type->GetDimension());
    return result;
  }

  Type* result = TypeFactory::Instance()->MakeType(type);
  std::vector<Type*> generics = type->GetGenerics();
  for(size_t i = 0; i < generics.size(); ++i) {
    generics[i] = SubstituteMethodGenerics(generics[i], bindings);
  }
  result->SetGenerics(generics);

  // a function type's parameters and return (#1242)
  if(type->GetType() == FUNC_TYPE) {
    std::vector<Type*> func_params = type->GetFunctionParameters();
    for(size_t i = 0; i < func_params.size(); ++i) {
      func_params[i] = SubstituteMethodGenerics(func_params[i], bindings);
    }
    result->SetFunctionParameters(func_params);
    result->SetFunctionReturn(SubstituteMethodGenerics(type->GetFunctionReturn(), bindings));
  }
  return result;
}

/****************************
 * Applies a call's type arguments
 * to a parameter type the class's
 * own were already resolved in,
 * walking the original for the
 * marks: resolving rebuilds a
 * function type from its encoded
 * name, which carries the bound and
 * not the mark (#1242)
 ****************************/
Type* ContextAnalyzer::SubstituteAlong(Type* original, Type* resolved, const std::map<std::wstring, Type*> &bindings)
{
  if(!original || !resolved || bindings.empty() || !original->HasMethodGeneric()) {
    return resolved;
  }

  const std::wstring &name = original->GetMethodGeneric();
  if(!name.empty()) {
    std::map<std::wstring, Type*>::const_iterator found = bindings.find(name);
    if(found == bindings.end()) {
      return resolved;
    }
    Type* result = TypeFactory::Instance()->MakeType(found->second);
    result->SetDimension(found->second->GetDimension() + original->GetDimension());
    return result;
  }

  Type* result = TypeFactory::Instance()->MakeType(resolved);
  const std::vector<Type*> original_generics = original->GetGenerics();
  std::vector<Type*> generics = resolved->GetGenerics();
  for(size_t i = 0; i < generics.size() && i < original_generics.size(); ++i) {
    generics[i] = SubstituteAlong(original_generics[i], generics[i], bindings);
  }
  result->SetGenerics(generics);

  if(original->GetType() == FUNC_TYPE && resolved->GetType() == FUNC_TYPE) {
    const std::vector<Type*> original_params = original->GetFunctionParameters();
    std::vector<Type*> params = resolved->GetFunctionParameters();
    for(size_t i = 0; i < params.size() && i < original_params.size(); ++i) {
      params[i] = SubstituteAlong(original_params[i], params[i], bindings);
    }
    result->SetFunctionParameters(params);
    result->SetFunctionReturn(SubstituteAlong(original->GetFunctionReturn(), resolved->GetFunctionReturn(), bindings));
    // matched by its encoded name, which still spells the bound
    result->SetName(L"");
  }
  return result;
}

/****************************
 * Infers a generic library
 * method's type arguments from a
 * call's arguments, and checks each
 * against its bound (#1242, phase 2)
 ****************************/
bool ContextAnalyzer::InferLibraryMethodGenerics(LibraryMethod* method, const std::vector<Expression*> &arguments, const size_t offset,
                                                 std::map<std::wstring, Type*> &bindings, std::wstring &error, const int depth)
{
  std::map<std::wstring, std::wstring> bounds;
  std::vector<Type*> params = method->GetDeclarationTypes();
  for(size_t i = 0; i < arguments.size() && i + offset < params.size(); ++i) {
    Type* param = params[i + offset];
    if(!param || !param->HasMethodGeneric()) {
      continue;
    }

    // a lambda's declared type; an inferred one has none yet, and its result
    // binds after it is built (DerivedLambdaFunction)
    Type* arg = nullptr;
    if(arguments[i]->GetExpressionType() == LAMBDA_EXPR) {
      arg = static_cast<Lambda*>(arguments[i])->GetLambdaType();
    }
    else {
      arg = GetExpressionType(arguments[i], depth + 1);
    }
    if(!arg) {
      continue;
    }
    const int arg_dimension = IsScalar(arguments[i]) ? 0 : arg->GetDimension();
    if(!UnifyMethodGeneric(param, arg, arg_dimension, bindings, bounds, error)) {
      return false;
    }
  }

  for(std::map<std::wstring, Type*>::iterator iter = bindings.begin(); iter != bindings.end(); ++iter) {
    const std::wstring bound_name = bounds[iter->first];
    if(bound_name.empty() || bound_name == L"System.Base" || iter->second->GetDimension() > 0) {
      continue;
    }

    Type* bound = TypeFactory::Instance()->MakeType(CLASS_TYPE, bound_name);
    ResolveClassEnumType(bound);
    Class* klass = nullptr;
    LibraryClass* lib_klass = nullptr;
    if(GetProgramOrLibraryClass(iter->second, klass, lib_klass) && !ValidDownCast(bound->GetName(), klass, lib_klass)) {
      error = L"Type parameter '" + iter->first + L"': '" + FormatTypeString(iter->second->GetName()) +
        L"' does not meet its bound '" + FormatTypeString(bound->GetName()) + L"'";
      return false;
    }
  }

  return true;
}

/****************************
 * The class a value of a type is
 * an object of, for a type
 * parameter's binding: primitives
 * box as they do when passed as a
 * Base (#1242)
 ****************************/
Type* ContextAnalyzer::BoxedBindingType(Type* type)
{
  if(!type || type->GetDimension() > 0) {
    return type;
  }

  switch(type->GetType()) {
  case BOOLEAN_TYPE:
    return TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.BoolRef");
  case BYTE_TYPE:
    return TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.ByteRef");
  case CHAR_TYPE:
    return TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.CharRef");
  case INT_TYPE:
    return TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.IntRef");
  case FLOAT_TYPE:
    return TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.FloatRef");
  default:
    return type;
  }
}

/****************************
 * Notes the declared type a
 * constructor call is about to
 * be stored into or returned as,
 * when the call names a class
 * and leaves out '<..>'
 ****************************/
void ContextAnalyzer::ExpectNewType(Expression* expression, Type* type, bool is_element)
{
  expected_new_call = nullptr;
  expected_new_type = nullptr;

  if(!expression || !type || expression->GetExpressionType() != METHOD_CALL_EXPR) {
    return;
  }

  MethodCall* method_call = static_cast<MethodCall*>(expression);
  // only the constructed object itself: not 'X->New()->Call()' and not a cast of it
  if(method_call->GetCallType() != NEW_INST_CALL || !method_call->GetConcreteTypes().empty() ||
     method_call->GetMethodCall() || method_call->GetCastType()) {
    return;
  }

  if(type->GetType() != CLASS_TYPE || !type->HasGenerics()) {
    return;
  }

  // a plain variable, or one element of an array
  if((type->GetDimension() > 0) != is_element) {
    return;
  }

  expected_new_call = method_call;
  expected_new_type = type;
}

/****************************
 * A type argument as it would
 * have been written: a class's
 * own bounded parameter reads as
 * its bound once it is resolved,
 * and is put back to its name
 ****************************/
Type* ContextAnalyzer::WrittenTypeArgument(Type* type)
{
  const std::wstring name = type->GetClassGeneric().empty() ? type->GetName() : type->GetClassGeneric();
  Type* written = TypeFactory::Instance()->MakeType(CLASS_TYPE, name, type->GetFileName(), type->GetLineNumber(), type->GetLinePosition());
  if(type->GetType() != CLASS_TYPE) {
    return TypeFactory::Instance()->MakeType(type);
  }

  std::vector<Type*> generics = type->GetGenerics();
  for(size_t i = 0; i < generics.size(); ++i) {
    generics[i] = WrittenTypeArgument(generics[i]);
  }
  written->SetGenerics(generics);

  return written;
}

/****************************
 * Binds a class's type parameter
 * by matching a constructor
 * parameter's type against the
 * argument's: 'T' and
 * 'C<.., T, ..>'
 ****************************/
bool ContextAnalyzer::UnifyClassGeneric(Type* param, Type* arg, int arg_dimension, Class* klass, LibraryClass* lib_klass,
                                        std::vector<Type*> &bindings, std::wstring &error)
{
  if(!param || !arg || param->GetType() != CLASS_TYPE) {
    return true;
  }

  const std::wstring param_name = param->GetClassGeneric().empty() ? param->GetName() : param->GetClassGeneric();
  const int index = klass ? klass->GenericIndex(param_name) : lib_klass->GenericIndex(param_name);
  if(index > -1 && index < (int)bindings.size()) {
    // 'Nil' and an array say nothing about 'T'
    if(arg->GetType() == NIL_TYPE || param->GetDimension() != arg_dimension) {
      return true;
    }

    Type* scalar = TypeFactory::Instance()->MakeType(arg);
    scalar->SetDimension(0);
    // a primitive is held by its class, as it is when the type argument is written
    Type* boxed = BoxedBindingType(scalar);
    if(boxed->GetType() != CLASS_TYPE) {
      return true;
    }

    // resolving renames a type in place (a bounded parameter reads as its
    // bound), so the type that is kept is one that was not resolved
    if(!ResolveClassEnumType(WrittenTypeArgument(boxed))) {
      return true;
    }
    Type* binding = WrittenTypeArgument(boxed);

    if(!bindings[index]) {
      bindings[index] = binding;
    }
    else if(FormatGenericType(bindings[index]) != FormatGenericType(binding)) {
      error = L"type parameter '" + param_name + L"' is both '" + FormatGenericType(bindings[index]) +
        L"' and '" + FormatGenericType(binding) + L"'";
      return false;
    }
    return true;
  }

  // C<.., T, ..>: match the type arguments pairwise, when it is the same class
  if(arg->GetType() != CLASS_TYPE || param->GetDimension() != arg_dimension) {
    return true;
  }

  const std::vector<Type*> param_generics = param->GetGenerics();
  const std::vector<Type*> arg_generics = arg->GetGenerics();
  if(param_generics.empty() || param_generics.size() != arg_generics.size()) {
    return true;
  }

  Type* param_class = TypeFactory::Instance()->MakeType(CLASS_TYPE, param->GetName());
  Type* arg_class = TypeFactory::Instance()->MakeType(CLASS_TYPE, arg->GetName());
  const bool param_found = klass ? ResolveClassEnumType(param_class, klass) : ResolveClassEnumType(param_class);
  if(!param_found || !ResolveClassEnumType(arg_class) || param_class->GetName() != arg_class->GetName()) {
    return true;
  }

  for(size_t i = 0; i < param_generics.size(); ++i) {
    if(!UnifyClassGeneric(param_generics[i], arg_generics[i], arg_generics[i]->GetDimension(), klass, lib_klass, bindings, error)) {
      return false;
    }
  }

  return true;
}

/****************************
 * Gives a constructor call of a
 * generic class the type
 * arguments that were not
 * written. In order:
 *
 *  1. the declared type the object
 *     is stored into or returned
 *     as, when it is the same
 *     class: 'v : Vector<Foo> :=
 *     Vector->New();'
 *  2. the arguments, when one
 *     constructor takes that many:
 *     'Pair->New(a, b)'
 *
 * The result is set as if '<..>'
 * had been written, so every check
 * that follows is the one an
 * explicit call gets. Returns false
 * after reporting that the
 * arguments do not settle them.
 ****************************/
bool ContextAnalyzer::InferNewTypeArguments(Class* klass, LibraryClass* lib_klass, MethodCall* method_call, const int depth)
{
  if(method_call->GetCallType() != NEW_INST_CALL || !method_call->GetConcreteTypes().empty()) {
    return true;
  }

  const size_t generic_count = klass ? klass->GetGenericClasses().size() : lib_klass->GetGenericClasses().size();
  if(!generic_count || (klass && klass->GetAnonymousCall()) || !GetConcreteTypes(method_call).empty()) {
    return true;
  }

  const std::wstring class_name = klass ? klass->GetName() : lib_klass->GetName();

  // 1. the declared type
  if(expected_new_call == method_call && expected_new_type) {
    Type* expected = expected_new_type;
    expected_new_call = nullptr;
    expected_new_type = nullptr;

    Class* expected_klass = nullptr; LibraryClass* expected_lib_klass = nullptr;
    if(ResolveClassEnumType(expected) && GetProgramOrLibraryClass(expected, expected_klass, expected_lib_klass) &&
       expected_klass == klass && expected_lib_klass == lib_klass && expected->GetGenerics().size() == generic_count) {
      std::vector<Type*> concrete_types;
      const std::vector<Type*> expected_generics = expected->GetGenerics();
      for(size_t i = 0; i < expected_generics.size(); ++i) {
        concrete_types.push_back(WrittenTypeArgument(expected_generics[i]));
      }
      method_call->SetConcreteTypes(concrete_types);
      return true;
    }
  }

  // 2. the arguments
  ExpressionList* call_params = method_call->GetCallingParameters();
  if(!call_params || call_params->HasNames()) {
    return true;
  }

  const std::vector<Expression*> arguments = call_params->GetExpressions();
  if(arguments.empty()) {
    return true;
  }

  // the parameter types of the constructors that take this many arguments
  std::vector<std::vector<Type*> > candidates;
  if(klass) {
    std::vector<Method*> constructors = klass->GetUnqualifiedMethods(L"New");
    for(size_t i = 0; i < constructors.size(); ++i) {
      std::vector<Declaration*> declarations = constructors[i]->GetDeclarations()->GetDeclarations();
      if(declarations.size() == arguments.size()) {
        std::vector<Type*> params;
        for(size_t j = 0; j < declarations.size(); ++j) {
          params.push_back(declarations[j]->GetEntry() ? declarations[j]->GetEntry()->GetType() : nullptr);
        }
        candidates.push_back(params);
      }
    }
  }
  else {
    std::vector<LibraryMethod*> constructors = lib_klass->GetUnqualifiedMethods(L"New");
    for(size_t i = 0; i < constructors.size(); ++i) {
      std::vector<Type*> params = constructors[i]->GetDeclarationTypes();
      if(params.size() == arguments.size()) {
        candidates.push_back(params);
      }
    }
  }

  // none: the call is reported as it always was
  if(candidates.empty()) {
    return true;
  }

  // Each constructor that takes this many arguments is matched against them.
  // One that leaves a type parameter without a class, or gives one two
  // classes, says nothing. The type arguments are settled when every
  // constructor that says something says the same.
  const std::wstring written = FormatTypeString(class_name) + L"->New(..)<..>";
  std::wstring first_error;
  std::vector<Type*> bindings;
  for(size_t c = 0; c < candidates.size(); ++c) {
    std::wstring error;
    std::vector<Type*> candidate_bindings(generic_count, nullptr);
    const std::vector<Type*> params = candidates[c];
    bool settled = true;
    for(size_t i = 0; settled && i < params.size(); ++i) {
      Type* arg = arguments[i]->GetExpressionType() == LAMBDA_EXPR ? nullptr : GetExpressionType(arguments[i], depth + 1);
      if(!arg) {
        continue;
      }
      const int arg_dimension = IsScalar(arguments[i]) ? 0 : arg->GetDimension();
      settled = UnifyClassGeneric(params[i], arg, arg_dimension, klass, lib_klass, candidate_bindings, error);
    }

    for(size_t i = 0; settled && i < candidate_bindings.size(); ++i) {
      if(!candidate_bindings[i]) {
        const std::wstring generic_name = klass ? klass->GetGenericClasses()[i]->GetName() : lib_klass->GetGenericClasses()[i]->GetName();
        error = L"no argument gives type parameter '" + FormatTypeString(generic_name) + L"' a class";
        settled = false;
      }
    }

    if(!settled) {
      if(first_error.empty()) {
        first_error = error;
      }
      continue;
    }

    if(bindings.empty()) {
      bindings = candidate_bindings;
      continue;
    }

    for(size_t i = 0; i < bindings.size(); ++i) {
      if(FormatGenericType(bindings[i]) != FormatGenericType(candidate_bindings[i])) {
        const std::wstring generic_name = klass ? klass->GetGenericClasses()[i]->GetName() : lib_klass->GetGenericClasses()[i]->GetName();
        ProcessError(static_cast<Expression*>(method_call), L"Cannot infer the type arguments of '" + FormatTypeString(class_name) +
                     L"': its constructors make type parameter '" + FormatTypeString(generic_name) + L"' both '" +
                     FormatGenericType(bindings[i]) + L"' and '" + FormatGenericType(candidate_bindings[i]) +
                     L"'\n\tWrite them: " + written);
        return false;
      }
    }
  }

  if(bindings.empty()) {
    ProcessError(static_cast<Expression*>(method_call), L"Cannot infer the type arguments of '" + FormatTypeString(class_name) +
                 L"': " + first_error + L"\n\tWrite them: " + written);
    return false;
  }

  method_call->SetConcreteTypes(bindings);
  return true;
}

/****************************
 * Resolves method calls
 ****************************/
Method* ContextAnalyzer::ResolveMethodCall(Class* klass, MethodCall* method_call, const int depth, Type* receiver)
{
  // a primitive receiver is parameter 0 of a static function (see AnalyzeMethodCall)
  const size_t offset = receiver ? 1 : 0;
  const std::wstring method_name = method_call->GetMethodName();
  ExpressionList* calling_params = method_call->GetCallingParameters();
  std::vector<Expression*> expr_params = calling_params->GetExpressions();
  std::vector<Method*> candidates = klass->GetAllUnqualifiedMethods(method_name);

  // save all valid candidates; a generic method's type arguments, per candidate (#1242)
  std::vector<MethodCallSelection*> matches;
  // Boxing an argument for one method analyzes 'IntRef->New(arg)', which casts
  // 'arg' for that constructor. Each method is tried against the arguments
  // as they were written, and so is the one that is chosen.
  const std::vector<ArgumentTypes> written_types = SaveArgumentTypes(expr_params);
  bool any_boxed = false;
  std::map<Method*, std::map<std::wstring, Type*> > candidate_bindings;
  std::wstring generic_error;
  for(size_t i = 0; i < candidates.size(); ++i) {
    // match parameter sizes
    Method* candidate = candidates[i];
    std::vector<Declaration*> method_parms = candidate->GetDeclarations()->GetDeclarations();

    if(expr_params.size() + offset == method_parms.size() &&
       (!receiver || (candidate->IsStatic() && IsReceiverParameter(method_parms[0]->GetEntry()->GetType(), receiver)))) {
      // a generic method: infer its type arguments from the arguments (#1242)
      std::map<std::wstring, Type*> bindings;
      if(IsGenericMethod(candidate)) {
        std::wstring error;
        if(!InferMethodGenerics(candidate, expr_params, bindings, error, depth)) {
          if(generic_error.empty()) {
            generic_error = error;
          }
          continue;
        }
        candidate_bindings[candidate] = bindings;
      }

      // box and unbox parameters
      if(any_boxed) {
        RestoreArgumentTypes(written_types);
      }
      std::vector<Expression*> boxed_resolved_params;
      std::vector<int> box_ranks;
      for(size_t j = 0; j < expr_params.size(); ++j) {
        // cannot be set to method, need to preserve test against other selections
        Expression* expr_param = expr_params[j];
        Type* expr_type = expr_param->GetEvalType();
        Type* method_type = SubstituteMethodGenerics(ResolveGenericParam(method_parms[j + offset]->GetEntry()->GetType(), method_call, klass, nullptr, false), bindings);

        // a primitive its holder cannot take (an Int for a BoolRef) is not
        // boxed: this method does not fit, and another may
        const int box_rank = BoxingRank(method_type, expr_param, depth);
        Expression* boxed_param = box_rank < 0 ? nullptr : BoxExpression(method_type, expr_param, depth);
        box_ranks.push_back(boxed_param ? (box_rank > 0 ? box_rank : 2) : 0);
        if(boxed_param) {
          any_boxed = true;
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
        Type* method_type = SubstituteMethodGenerics(ResolveGenericParam(method_parms[j + offset]->GetEntry()->GetType(), method_call, klass, nullptr, false), bindings);
        // add parameter match: a boxed argument fits, but less well than one
        // that is the parameter's type or converts to it
        const int compare = box_ranks[j] ? box_ranks[j] : MatchCallingParameter(boxed_resolved_params[j], method_type, klass, nullptr, depth);
        match->AddParameterMatch(compare);
      }
      matches.push_back(match);
    }
  }

  if(any_boxed) {
    RestoreArgumentTypes(written_types);
  }

  // evaluate matches
  MethodCallSelector selector(method_call, matches);
  Method* method = selector.GetSelection();
  if(selector.IsAmbiguous()) {
    ProcessAmbiguousCall(method_call, selector.GetValidMethodNames());
  }
  if(method) {
    std::map<Method*, std::map<std::wstring, Type*> >::iterator bound = candidate_bindings.find(method);
    if(bound != candidate_bindings.end()) {
      method_call->SetMethodGenericBindings(bound->second);
    }

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
      Type* left = SubstituteMethodGenerics(ResolveGenericParam(method_parms[j + offset]->GetEntry()->GetType(), method_call, klass, nullptr, false),
                                            method_call->GetMethodGenericBindings());
      UnboxedArgument(method_call, j, expr_params[j], left, AnalyzeRightCast(left, expression, IsScalar(expression), depth + 1));
    }
  }
  else if(!generic_error.empty() && matches.empty()) {
    ProcessError(static_cast<Expression*>(method_call), generic_error);
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
  // The class a failed call is reported against is the one the call was made on. The search
  // below walks up to the parents, and reported the last one it reached: 'Other->Nope()' was
  // said to be called on the class the call stands in.
  if(diag_parent_walk && diag_call == method_call) {
    diag_parent_walk = false;
  }
  else {
    diag_parent_walk = false;
    diag_call = method_call;
    diag_class = klass;
    diag_lib_class = nullptr;
  }

#ifdef _DEBUG
  GetLogger() << L"Checking program class call: |" << klass->GetName() << L':' 
    << (method_call->GetMethodName().size() > 0 ? method_call->GetMethodName() : method_call->GetVariableName())
    << L"|" << std::endl;
#endif

  // calling parameters
  ExpressionList* call_params = method_call->GetCallingParameters();

  // lambda inferred type
  CheckLambdaInferredTypes(method_call, depth + 1);

  if(!TakeParamsAnalyzed(method_call)) {
    AnalyzeExpressions(call_params, depth + 1);
  }

  // 'Pair->New(a, b)' and 'v : Vector<Foo> := Vector->New()': the type
  // arguments that were not written
  if(!InferNewTypeArguments(klass, nullptr, method_call, depth)) {
    return;
  }

  // named arguments bind to the forwarder generated for the call's shape (#1244)
  if(call_params->HasNames() && !BindNamedArguments(klass, method_call)) {
    return;
  }

  // note: find system based methods and call with function parameters (i.e. $Int, $Float).
  // A primitive receiver is parameter 0 of the function, so only a function that
  // takes it can match: resolving the written arguments alone bound
  // 255->ToHexString(4) to ToHexString(4), and an instance method of an ancestor
  // (Base) to an Int 'self'.
  Type* receiver = ReceiverParameterType(encoding);
  Method* method = ResolveMethodCall(klass, method_call, depth, receiver);
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

    // A constructor is not inherited (Class::GetAllUnqualifiedMethods leaves
    // 'New' out). Walking on to the parent's made 'Kid->New()' build a Root,
    // typed as one, and 'Bare->New()' on a class with no constructor at all a
    // System.Base, which then failed at run time as an invalid cast.
    if(method_call->GetCallType() == NEW_INST_CALL) {
      if(klass->GetUnqualifiedMethods(L"New").empty()) {
        ProcessError(static_cast<Expression*>(method_call), L"Class '" + klass->GetName() +
                     L"' declares no constructor; constructors are not inherited\n\tDeclare 'New(..)' in the class.");
      }
      else {
        std::wstring message = L"Undefined function/method call: 'New(..)'\n\tEnsure calling parameters are properly casted\n\tConsider first class and static use function calls";
        ProcessErrorAlternativeMethods(message);
        ProcessError(static_cast<Expression*>(method_call), message);
      }
      return;
    }

    // proceed with class resolution
    if(klass->GetParent()) {
      Class* parent = klass->GetParent();
      method_call->SetOriginalClass(klass);
      std::wstring encoding;
      params_analyzed = method_call;
      diag_parent_walk = true;
      AnalyzeMethodCall(parent, method_call, is_expr, encoding, depth + 1);
      return;
    }
    else if(klass->GetLibraryParent()) {
      LibraryClass* lib_parent = klass->GetLibraryParent();
      method_call->SetOriginalClass(klass);
      std::wstring encoding;
      params_analyzed = method_call;
      diag_parent_walk = true;
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
        // check generic parameters for call; a generic method's with its inferred type arguments (#1242)
        Type* left = SubstituteMethodGenerics(ResolveGenericParam(mthd_params[i]->GetEntry()->GetType(), method_call, klass, nullptr, false),
                                              method_call->GetMethodGenericBindings());
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
      ProcessError(static_cast<Expression*>(method_call), L"Cannot reference an instance method from this context" +
                   std::wstring(current_method && current_method->IsLambda() ? L"\n\tIn a lambda, call it through the instance: @self->" + method_call->GetMethodName() + L"(..)" : L""));
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
    // the return type is written in the callee's class, so it is resolved in
    // that class's bundle (see ResolveClassEnumType)
    bool same_cls_return = false;
    {
      OtherClassScope other_scope(skip_use_check, bundle, OtherClassBundle(klass));
      same_cls_return = ClassEquals(method->GetReturn()->GetName(), klass, nullptr);
    }
    if((is_new || same_cls_return) && klass->HasGenerics()) {
      const std::vector<Class*> class_generics = klass->GetGenericClasses();      
      std::vector<Type*> concrete_types = GetConcreteTypes(method_call);
      if(class_generics.size() != concrete_types.size()) {
        ProcessError(static_cast<Expression*>(method_call), L"Generic type mismatch for class '" + klass->GetName() +
                     L"': expected " + ToString(class_generics.size()) +
                     L" type parameter(s), got " + ToString(concrete_types.size()));
      }
      // check individual types
      if(class_generics.size() == concrete_types.size()) {
        for(size_t i = 0; i < concrete_types.size(); ++i) {
          Type* concrete_type = concrete_types[i];
          Class* class_generic = class_generics[i];
          if(class_generic->HasGenericInterface()) {
            // concrete type
            ResolveClassEnumType(concrete_type);
            // validate against EVERY bound (T : A & B & ...)
            const std::vector<Type*> bounds = class_generic->GetAllGenericInterfaces();
            for(size_t b = 0; b < bounds.size(); ++b) {
              Type* backing_type = bounds[b];
              ResolveClassEnumType(backing_type);
              ValidateGenericBacking(concrete_type, backing_type->GetName(), static_cast<Expression*>(method_call));
            }
          }
        }
      }
      method_call->GetEvalType()->SetGenerics(concrete_types);
    }

    // resolve generic to concrete, if needed
    Type* eval_type = method_call->GetEvalType();
    if(klass->HasGenerics()) {
      // see the library-class path: `Wrap(a : T) ~ Cell<T>` (#1182)
      if(eval_type->HasGenerics() && method_call->GetConcreteTypes().empty() && !is_new) {
        eval_type = MapReturnGenerics(eval_type, method_call, klass, nullptr);
        method_call->SetEvalType(eval_type, false);
      }
      eval_type = ResolveGenericType(eval_type, method_call, klass, nullptr, true);
      method_call->SetEvalType(eval_type, false);
    }

    // a generic method's return type, with the call's inferred type arguments (#1242)
    if(!method_call->GetMethodGenericBindings().empty()) {
      eval_type = SubstituteMethodGenerics(method->GetReturn(), method_call->GetMethodGenericBindings());
      method_call->SetEvalType(eval_type, false);
    }

    if(eval_type->GetType() == CLASS_TYPE && !ResolveClassEnumType(eval_type, klass)) {
      ProcessError(static_cast<Expression*>(method_call), L"Undefined class or enum: '" +
                   FormatTypeString(eval_type->GetName()) + L"'");
    }

    // set subsequent call type
    if(method_call->GetMethodCall()) {
      // `c->Wrap(x)->Get()`: Wrap's Cell<T> is a Cell<String> to the next call
      Type* rtrn_type = SubstituteMethodGenerics(method->GetReturn(), method_call->GetMethodGenericBindings());
      if(rtrn_type->HasGenerics() && klass->HasGenerics() && method_call->GetConcreteTypes().empty() && !is_new) {
        rtrn_type = MapReturnGenerics(rtrn_type, method_call, klass, nullptr);
      }
      Type* expr_type = ResolveGenericType(rtrn_type, method_call, klass, nullptr, true);
      method_call->GetMethodCall()->SetEvalType(expr_type, false);
      // kept apart from the eval type, which SetMethod replaces (#1182). A
      // constructor's, or a same-class return's, type arguments are on this
      // call's eval type -- `Ops->New()<String>->Id(..)` -- not on the return.
      if(expr_type->HasGenerics()) {
        method_call->GetMethodCall()->SetReceiverGenerics(expr_type->GetGenerics());
      }
      else if(eval_type->GetName() == expr_type->GetName()) {
        method_call->GetMethodCall()->SetReceiverGenerics(eval_type->GetGenerics());
      }
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

    if(ReportJsonLiteralValue(method_call) || ReportMapLiteralEntry(method_call)) {
      // said in terms of the literal
    }
    else if(mthd_name.size() > 0) {
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
LibraryMethod* ContextAnalyzer::ResolveMethodCall(LibraryClass* klass, MethodCall* method_call, const int depth, Type* receiver)
{
  // a primitive receiver is parameter 0 of a static function (see AnalyzeMethodCall)
  const size_t offset = receiver ? 1 : 0;
  const std::wstring &method_name = method_call->GetMethodName();
  ExpressionList* calling_params = method_call->GetCallingParameters();
  std::vector<Expression*> expr_params = calling_params->GetExpressions();
  std::vector<LibraryMethod*> candidates = klass->GetUnqualifiedMethods(method_name);

  // save all valid candidates; a generic method's type arguments, per candidate (#1242)
  std::vector<LibraryMethodCallSelection*> matches;
  // Boxing an argument for one method analyzes 'IntRef->New(arg)', which casts
  // 'arg' for that constructor. Each method is tried against the arguments
  // as they were written, and so is the one that is chosen.
  const std::vector<ArgumentTypes> written_types = SaveArgumentTypes(expr_params);
  bool any_boxed = false;
  std::map<LibraryMethod*, std::map<std::wstring, Type*> > candidate_bindings;
  std::wstring generic_error;
  for(size_t i = 0; i < candidates.size(); ++i) {
    // match parameter sizes
    LibraryMethod* candidate = candidates[i];
    std::vector<Type*> method_parms = candidate->GetDeclarationTypes();
    if(expr_params.size() + offset == method_parms.size() &&
       (!receiver || (candidate->IsStatic() && IsReceiverParameter(method_parms[0], receiver)))) {
      std::map<std::wstring, Type*> bindings;
      if(candidate->IsGeneric()) {
        std::wstring error;
        if(!InferLibraryMethodGenerics(candidate, expr_params, offset, bindings, error, depth)) {
          if(generic_error.empty()) {
            generic_error = error;
          }
          continue;
        }
        candidate_bindings[candidate] = bindings;
      }

      // box and unbox parameters
      if(any_boxed) {
        RestoreArgumentTypes(written_types);
      }
      std::vector<Expression*> boxed_resolved_params;
      std::vector<int> box_ranks;
      for(size_t j = 0; j < expr_params.size(); ++j) {
        Expression* expr_param = expr_params[j];
        Type* expr_type = expr_param->GetEvalType();
        Type* method_type = SubstituteAlong(method_parms[j + offset], ResolveGenericParam(method_parms[j + offset], method_call, nullptr, klass, false), bindings);

        // see the program-class path
        const int box_rank = BoxingRank(method_type, expr_param, depth);
        Expression* boxed_param = box_rank < 0 ? nullptr : BoxExpression(method_type, expr_param, depth);
        box_ranks.push_back(boxed_param ? (box_rank > 0 ? box_rank : 2) : 0);
        if(boxed_param) {
          any_boxed = true;
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
        Type* method_type = SubstituteAlong(method_parms[j + offset], ResolveGenericParam(method_parms[j + offset], method_call, nullptr, klass, false), bindings);
        const int compare = box_ranks[j] ? box_ranks[j] : MatchCallingParameter(boxed_resolved_params[j], method_type, nullptr, klass, depth);
        match->AddParameterMatch(compare);
      }
      matches.push_back(match);
    }
  }

  if(any_boxed) {
    RestoreArgumentTypes(written_types);
  }

  // evaluate matches
  LibraryMethodCallSelector selector(method_call, matches);
  LibraryMethod* lib_method = selector.GetSelection();
  if(selector.IsAmbiguous()) {
    ProcessAmbiguousCall(method_call, selector.GetValidMethodNames());
  }
  if(lib_method) {
    std::map<LibraryMethod*, std::map<std::wstring, Type*> >::iterator bound = candidate_bindings.find(lib_method);
    if(bound != candidate_bindings.end()) {
      method_call->SetMethodGenericBindings(bound->second);
    }

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
      Type* left = SubstituteAlong(method_parms[j + offset], ResolveGenericParam(method_parms[j + offset], method_call, nullptr, klass, false),
                                   method_call->GetMethodGenericBindings());
      UnboxedArgument(method_call, j, expr_params[j], left, AnalyzeRightCast(left, expression, IsScalar(expression), depth + 1));
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
    else if(!generic_error.empty()) {
      ProcessError(static_cast<Expression*>(method_call), generic_error);
    }
  }

  return lib_method;
}

/****************************
 * A placeholder for a library
 * parameter a named call skips
 * (#1244): the mask forwarder
 * overwrites it with the default
 ****************************/
Expression* ContextAnalyzer::NamedPlaceholder(Type* type, Expression* at)
{
  const std::wstring &file_name = at->GetFileName();
  const int line_num = at->GetLineNumber();
  const int line_pos = at->GetLinePosition();

  if(type->GetDimension() > 0) {
    return TreeFactory::Instance()->MakeNilLiteral(file_name, line_num, line_pos);
  }

  switch(type->GetType()) {
  case BOOLEAN_TYPE:
    return TreeFactory::Instance()->MakeBooleanLiteral(file_name, line_num, line_pos, false);

  case CHAR_TYPE:
    return TreeFactory::Instance()->MakeCharacterLiteral(file_name, line_num, line_pos, L'\0');

  case BYTE_TYPE:
  case INT_TYPE:
    return TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 0);

  case FLOAT_TYPE:
    return TreeFactory::Instance()->MakeFloatLiteral(file_name, line_num, line_pos, 0.0);

  case CLASS_TYPE:
    return TreeFactory::Instance()->MakeNilLiteral(file_name, line_num, line_pos);

  default:
    return nullptr;
  }
}

/****************************
 * Binds a named call to a library
 * method through the parameter
 * names its .obl records (#1244,
 * phase 2). A call that skips
 * nothing becomes a positional
 * call; one that skips a default
 * calls the method's mask
 * forwarder with a placeholder for
 * each skipped parameter and the
 * mask of those given. Either way
 * the arguments are evaluated as
 * written, through hidden locals
 * when the order changes.
 ****************************/
bool ContextAnalyzer::BindNamedLibraryArguments(LibraryClass* klass, MethodCall* method_call, const int depth)
{
  ExpressionList* call_params = method_call->GetCallingParameters();
  const std::vector<std::wstring> shape = call_params->GetNames();
  const std::wstring name = method_call->GetMethodName();
  const std::vector<Expression*> written = call_params->GetExpressions();
  call_params->ClearNames();

  struct Plan {
    LibraryMethod* method;
    std::vector<size_t> order;
    std::vector<size_t> missing;
  };
  std::vector<Plan> plans;
  std::wstring first_error;

  // this class's overloads, then its parents'
  std::vector<LibraryMethod*> candidates;
  for(LibraryClass* search = klass; search; ) {
    const std::vector<LibraryMethod*> methods = search->GetUnqualifiedMethods(name);
    candidates.insert(candidates.end(), methods.begin(), methods.end());
    search = search->GetParentName().empty() ? nullptr :
      SearchLibraryClasses(search->GetParentName(), program->GetLibUses(current_class->GetFileName()));
  }

  for(size_t c = 0; c < candidates.size(); ++c) {
    LibraryMethod* candidate = candidates[c];
    const std::vector<std::wstring> &names = candidate->GetParameterNames();
    const std::vector<bool> &defaults = candidate->GetParameterDefaults();
    if(names.size() != candidate->GetDeclarationTypes().size()) {
      continue;
    }

    Plan plan;
    plan.method = candidate;
    std::vector<bool> given(names.size(), false);
    std::wstring error;
    for(size_t i = 0; error.empty() && i < shape.size(); ++i) {
      size_t index = names.size();
      if(shape[i].empty()) {
        if(i < names.size()) {
          index = i;
        }
        else {
          error = L"Too many arguments for '" + name + L"'";
        }
      }
      else {
        for(size_t j = 0; j < names.size(); ++j) {
          if(names[j] == shape[i]) {
            index = j;
            break;
          }
        }
        if(index == names.size()) {
          std::wstring list;
          for(size_t j = 0; j < names.size(); ++j) {
            list += (j > 0 ? L", " : L"") + names[j];
          }
          error = L"No parameter named '" + shape[i] + L"'; the parameters are: " + (list.empty() ? L"none" : list);
        }
      }
      if(error.empty()) {
        if(given[index]) {
          error = L"Parameter '" + names[index] + L"' is given both by position and by name";
        }
        else {
          given[index] = true;
          plan.order.push_back(index);
        }
      }
    }
    for(size_t j = 0; error.empty() && j < names.size(); ++j) {
      if(!given[j]) {
        if(j < defaults.size() && defaults[j]) {
          plan.missing.push_back(j);
        }
        else {
          error = L"Parameter '" + names[j] + L"' has no default value and is not given";
        }
      }
    }

    if(error.empty()) {
      plans.push_back(plan);
    }
    else if(first_error.empty()) {
      first_error = error;
    }
  }

  if(plans.empty()) {
    ProcessError(static_cast<Expression*>(method_call), first_error.empty() ? L"Undefined function/method call: '" + name + L"(..)'" : first_error);
    return false;
  }

  // a plan that skips nothing calls the method itself (or a default alternate);
  // one that skips a default needs the method's mask forwarder
  const Plan* chosen = nullptr;
  bool ambiguous = false;
  for(size_t p = 0; p < plans.size(); ++p) {
    if(plans[p].missing.empty()) {
      if(chosen && chosen->order != plans[p].order) {
        ambiguous = true;
      }
      chosen = chosen ? chosen : &plans[p];
    }
  }
  // A library records a method with trailing defaults as the method and one
  // alternate per default it can drop: F(a, b, c, d) also as F(a, b, c) and
  // F(a, b). An alternate is a strict prefix of its method, by names and
  // types, whose remaining parameters all have defaults. Skipping by name goes
  // through the METHOD's mask forwarder; planned on an alternate as well, a
  // call that skips a middle default saw two masks and reported
  // "match more than one overload" ('Make(a: 1, c: 5)'). The program path
  // skips alternates through Method::IsAlt; a LibraryMethod has no such flag,
  // and this needs none (no .obl format change).
  auto is_alternate = [&candidates](LibraryMethod* method) {
    const std::vector<std::wstring> &names = method->GetParameterNames();
    const std::vector<frontend::Type*> types = method->GetDeclarationTypes();
    for(size_t c = 0; c < candidates.size(); ++c) {
      LibraryMethod* other = candidates[c];
      const std::vector<std::wstring> &other_names = other->GetParameterNames();
      const std::vector<bool> &other_defaults = other->GetParameterDefaults();
      const std::vector<frontend::Type*> other_types = other->GetDeclarationTypes();
      if(other == method || other->GetLibraryClass() != method->GetLibraryClass() ||
         other_names.size() <= names.size() || other_types.size() != other_names.size() ||
         other_defaults.size() != other_names.size()) {
        continue;
      }

      bool prefix = true;
      for(size_t j = 0; prefix && j < names.size(); ++j) {
        prefix = names[j] == other_names[j] && types[j]->GetType() == other_types[j]->GetType() &&
          types[j]->GetDimension() == other_types[j]->GetDimension() && types[j]->GetName() == other_types[j]->GetName();
      }
      for(size_t j = names.size(); prefix && j < other_names.size(); ++j) {
        prefix = other_defaults[j];
      }
      if(prefix) {
        return true;
      }
    }
    return false;
  };

  bool use_mask = false;
  if(!chosen) {
    for(size_t p = 0; p < plans.size(); ++p) {
      LibraryClass* owner = plans[p].method->GetLibraryClass();
      if(owner->GetUnqualifiedMethods(name + L"$m").empty() || is_alternate(plans[p].method)) {
        continue;
      }
      if(chosen && (chosen->order != plans[p].order || chosen->missing != plans[p].missing)) {
        ambiguous = true;
      }
      chosen = chosen ? chosen : &plans[p];
      use_mask = true;
    }
    if(!chosen) {
      ProcessError(static_cast<Expression*>(method_call), L"'" + name + L"' cannot skip a parameter by name: rebuild its library with this toolchain");
      return false;
    }
  }
  if(ambiguous) {
    ProcessError(static_cast<Expression*>(method_call), L"Named arguments match more than one overload of '" + name +
                 L"'; pass the ambiguous ones by position");
    return false;
  }

  // the arguments in parameter order, a placeholder for each skipped parameter
  const std::vector<frontend::Type*> types = chosen->method->GetDeclarationTypes();
  std::vector<Expression*> arguments(types.size(), nullptr);
  for(size_t k = 0; k < chosen->order.size(); ++k) {
    arguments[chosen->order[k]] = written[k];
  }
  INT64_VALUE mask = 0;
  for(size_t k = 0; k < chosen->order.size(); ++k) {
    mask |= ((INT64_VALUE)1) << chosen->order[k];
  }
  for(size_t m = 0; m < chosen->missing.size(); ++m) {
    const size_t p = chosen->missing[m];
    Expression* placeholder = NamedPlaceholder(types[p], static_cast<Expression*>(method_call));
    if(!placeholder) {
      ProcessError(static_cast<Expression*>(method_call), L"A function-typed parameter of a library method cannot be skipped by name");
      return false;
    }
    arguments[p] = placeholder;
  }
  if(use_mask) {
    arguments.push_back(TreeFactory::Instance()->MakeIntegerLiteral(static_cast<Expression*>(method_call)->GetFileName(),
                                                                    static_cast<Expression*>(method_call)->GetLineNumber(),
                                                                    static_cast<Expression*>(method_call)->GetLinePosition(), mask));
    method_call->SetMethodName(name + L"$m");
  }
  call_params->SetExpressions(arguments);

  // evaluated as written: through hidden locals when the order changes
  bool in_order = true;
  for(size_t k = 1; k < chosen->order.size(); ++k) {
    if(chosen->order[k] < chosen->order[k - 1]) {
      in_order = false;
    }
  }
  if(!in_order && current_method && current_table) {
    static int named_tmp_id = 0;
    std::vector<size_t> eval_order;
    std::vector<SymbolEntry*> temps(arguments.size(), nullptr);
    for(size_t k = 0; k < chosen->order.size(); ++k) {
      const size_t p = chosen->order[k];
      Type* temp_type = TypeFactory::Instance()->MakeType(types[p]);
      if(temp_type->GetType() == FUNC_TYPE) {
        ProcessError(static_cast<Expression*>(method_call), L"A function-typed argument to a library method must be passed in parameter order");
        return false;
      }
      const std::wstring tmp_name = current_method->GetName() + L":#named_tmp#" + std::to_wstring(named_tmp_id++);
      SymbolEntry* entry = TreeFactory::Instance()->MakeSymbolEntry(tmp_name, temp_type, false, true);
      current_table->AddEntry(entry, true);
      entry->WasLoaded();
      temps[p] = entry;
      eval_order.push_back(p);
    }
    method_call->SetNamedEvaluation(eval_order, temps);
  }

  return true;
}

/****************************
 * Analyzes a method call.  This
 * is method call within a linked
 * library
 ****************************/
void ContextAnalyzer::AnalyzeMethodCall(LibraryClass* klass, MethodCall* method_call, bool is_expr, 
                                        std::wstring &encoding, bool is_parent, const int depth)
{
  if(diag_parent_walk && diag_call == method_call) {
    diag_parent_walk = false;
  }
  else {
    diag_parent_walk = false;
    diag_call = method_call;
    diag_class = nullptr;
    diag_lib_class = klass;
  }
#ifdef _DEBUG
  GetLogger() << L"Checking library encoded name: |" << klass->GetName() << L':' << method_call->GetMethodName() << L"|" << std::endl;
#endif

  ExpressionList* call_params = method_call->GetCallingParameters();

  // lambda inferred type
  CheckLambdaInferredTypes(method_call, depth + 1);

  if(!TakeParamsAnalyzed(method_call)) {
    AnalyzeExpressions(call_params, depth + 1);
  }

  // the type arguments that were not written (see the program-class path)
  if(!InferNewTypeArguments(nullptr, klass, method_call, depth)) {
    return;
  }

  // named arguments, through the parameter names the .obl records (#1244). The
  // method may be one this class inherits from a library parent.
  if(call_params->HasNames()) {
    bool found = false;
    for(LibraryClass* search = klass; search && !found; ) {
      found = !search->GetUnqualifiedMethods(method_call->GetMethodName()).empty();
      search = search->GetParentName().empty() ? nullptr :
        SearchLibraryClasses(search->GetParentName(), program->GetLibUses(current_class->GetFileName()));
    }

    // a call to none of them is left to its own path
    if(!found) {
    }
    else if(ReceiverParameterType(encoding)) {
      ProcessError(static_cast<Expression*>(method_call), L"Named arguments are not supported for calls on a primitive value or an array");
      call_params->ClearNames();
    }
    else if(!BindNamedLibraryArguments(klass, method_call, depth)) {
      return;
    }
  }
  // A primitive receiver is parameter 0 of a static function on its $ class, so
  // only a function that takes it can match. Resolving the written arguments
  // alone bound 255->ToHexString(4) to ToHexString(4) (the receiver was pushed
  // as well, and the stack went wrong) and an ancestor's instance method, such
  // as one on Base, to an Int 'self'.
  Type* receiver = ReceiverParameterType(encoding);
  LibraryMethod* lib_method = ResolveMethodCall(klass, method_call, depth, receiver);
  // a constructor is not inherited (see the program-class path)
  if(!lib_method && method_call->GetCallType() == NEW_INST_CALL) {
    if(klass->GetUnqualifiedMethods(L"New").empty()) {
      ProcessError(static_cast<Expression*>(method_call), L"Class '" + klass->GetName() +
                   L"' declares no constructor; constructors are not inherited");
    }
    else if(IsHolderType(klass->GetName()) && method_call->GetCallingParameters() &&
            method_call->GetCallingParameters()->GetExpressions().size() == 1 &&
            method_call->GetCallingParameters()->GetExpressions()[0]->GetExpressionType() == STAT_ARY_EXPR) {
      // A whole array literal where holders are wanted ('v : Vector<IntRef> := [n->Size()];',
      // 'Vector->New([1, 2])<IntRef>'): the holder's constructor was tried on the array,
      // and "IntRef->New(Int[])" says nothing of the elements that were written.
      Expression* literal = method_call->GetCallingParameters()->GetExpressions()[0];
      Type* literal_type = literal->GetEvalType() ? TypeFactory::Instance()->MakeType(literal->GetEvalType()) : nullptr;
      if(literal_type) {
        literal_type->SetDimension(0);
      }
      const std::wstring holder = PlainTypeName(FormatTypeString(klass->GetName()));
      ProcessError(literal, L"The elements of an array literal are not boxed: these are " + DescribeType(literal_type) +
                   L", where " + holder + L" is wanted for each\n\tWrite the holder for each element ('" + holder +
                   L"->New(..)'), or add the values with AddBack(..), which boxes each one");
    }
    else {
      std::wstring message = L"Undefined function/method call: 'New(..)'\n\tEnsure calling parameters are properly casted\n\tConsider first class and static use function calls";
      ProcessErrorAlternativeMethods(message);
      ProcessError(static_cast<Expression*>(method_call), message);
    }
    return;
  }

  if(!lib_method) {
    LibraryClass* parent = SearchLibraryClasses(klass->GetParentName(), program->GetLibUses(current_class->GetFileName()));
    while(!lib_method && parent) {
      lib_method = ResolveMethodCall(parent, method_call, depth, receiver);
      parent = SearchLibraryClasses(parent->GetParentName(), program->GetLibUses(current_class->GetFileName()));
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

  // names no path consumed must not bind to a library method by position; a
  // chained call has no guard. Without a method, the call is some other path's.
  if(lib_method && call_params->HasNames()) {
    call_params->ClearNames();
    ProcessError(static_cast<Expression*>(method_call), L"Named arguments need a method or function of a class; this call has no parameter names");
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
      ProcessError(static_cast<Expression*>(method_call), L"Cannot reference an instance method from this context" +
                   std::wstring(current_method && current_method->IsLambda() ? L"\n\tIn a lambda, call it through the instance: @self->" + method_call->GetMethodName() + L"(..)" : L""));
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

    // a generic method's return type, with the call's inferred type arguments (#1242)
    if(!method_call->GetMethodGenericBindings().empty()) {
      method_call->SetEvalType(SubstituteMethodGenerics(lib_method->GetReturn(), method_call->GetMethodGenericBindings()), false);
    }

    if(method_call->GetMethodCall()) {
      method_call->GetMethodCall()->SetEvalType(lib_method->GetReturn(), false);
    }

    // enum check
    if(method_call->GetMethodCall() && method_call->GetMethodCall()->GetCallType() == ENUM_CALL) {
      ProcessError(static_cast<Expression*>(method_call), L"Invalid enum reference");
    }

    if(lib_method->GetReturn()->GetType() == NIL_TYPE && method_call->GetCastType() && !IsReceiverCast(method_call)) {
      ProcessError(static_cast<Expression*>(method_call), L"Cannot cast a Nil return value");
    }
    
    // map Generic to concrete types
    LibraryClass* lib_klass = lib_method->GetLibraryClass();
    const bool is_new = lib_method->GetMethodType() == NEW_PUBLIC_METHOD || lib_method->GetMethodType() == NEW_PRIVATE_METHOD;
    const bool same_cls_return = ClassEquals(lib_method->GetReturn()->GetName(), nullptr, lib_klass);
    if((is_new || same_cls_return) && lib_klass->HasGenerics()) {
      const std::vector<LibraryClass*> class_generics = lib_klass->GetGenericClasses();
      // A call on the previous call's result -- 'source->AddString(..)->AddInt(..)'
      // -- has no entry or variable of its own. Its eval type is the raw return
      // that SetLibraryMethod put there, 'ObjectSource<T>', so its arguments were
      // the class's own parameter, and 'T' was checked as a concrete class:
      // "Undefined class or interface: 'T'". The previous call handed over the
      // receiver's arguments, as ResolveGenericType already reads them.
      const std::vector<Type*> concrete_types = !is_new && method_call->GetConcreteTypes().empty() && !method_call->GetEntry() &&
        !method_call->GetVariable() && !method_call->GetReceiverGenerics().empty() ?
        method_call->GetReceiverGenerics() : GetConcreteTypes(method_call);
      if(class_generics.size() != concrete_types.size()) {
        ProcessError(static_cast<Expression*>(method_call), L"Generic type mismatch for class '" + lib_klass->GetName() +
                     L"': expected " + ToString(class_generics.size()) +
                     L" type parameter(s), got " + ToString(concrete_types.size()));
      }
      // check individual types
      if(class_generics.size() == concrete_types.size()) {
        for(size_t i = 0; i < concrete_types.size(); ++i) {
          Type* concrete_type = concrete_types[i];
          LibraryClass* class_generic = class_generics[i];
          if(class_generic->HasGenericInterface()) {
            // concrete type
            ResolveClassEnumType(concrete_type);
            // validate against EVERY bound (T : A & B & ...)
            const std::vector<Type*> bounds = class_generic->GetAllGenericInterfaces();
            for(size_t b = 0; b < bounds.size(); ++b) {
              Type* backing_type = bounds[b];
              ResolveClassEnumType(backing_type);
              ValidateGenericBacking(concrete_type, backing_type->GetName(), static_cast<Expression*>(method_call));
            }
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
      // A return built from the class's own parameters -- Map->GetKeys() is
      // Vector<K> -- has its arguments mapped through the receiver's, so
      // `a->GetKeys()` on a Map<String, IntRef> is a Vector<String>. Without
      // this the caller had to write `<K>`, and even a declared
      // `k : Vector<String> := a->GetKeys()` was rejected (#1182).
      if(eval_type->HasGenerics() && method_call->GetConcreteTypes().empty()) {
        eval_type = MapReturnGenerics(eval_type, method_call, nullptr, lib_klass);
        method_call->SetEvalType(eval_type, false);
      }
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

    // The next call in a chain takes its type arguments from its own eval type,
    // which was set above to the RAW return -- 'V' for Map->Find -- so
    // `m->Find(k)->Insert(x)` looked for Insert(H) on a Set with no H and
    // failed, while the same call through a local worked. Hand it the resolved
    // return, type arguments and all, as the program-class path does (#1182).
    if(method_call->GetMethodCall() && method_call->GetEvalType()) {
      method_call->GetMethodCall()->SetEvalType(method_call->GetEvalType(), false);
      method_call->GetMethodCall()->SetReceiverGenerics(method_call->GetEvalType()->GetGenerics());
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
 * Analyzes a call through a function
 * value held by 'entry': a function
 * variable 'f(x)', or the hidden local
 * of a curried call 'F(a)(b)'
 ********************************/
void ContextAnalyzer::AnalyzeFunctionValueCall(MethodCall* method_call, SymbolEntry* entry, const std::wstring& call_name, const int depth)
{
  entry->WasLoaded();

  // a function type has no parameter names (#1244)
  if(method_call->GetCallingParameters()->HasNames()) {
    ProcessError(static_cast<Expression*>(method_call), L"A function value has no parameter names; pass its arguments by position");
    method_call->GetCallingParameters()->ClearNames();
  }

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
    else {
      // a holder for a primitive parameter is its value, as in a call by name
      Expression* last = calling_param;
      while(last->GetMethodCall()) {
        last = last->GetMethodCall();
      }
      bool unboxed = false;
      Expression* replaced = UnboxAs(func_param, last, false, unboxed, depth + 1);
      if(unboxed) {
        boxed_param = replaced ? replaced : calling_param;
        boxed_resolved_params->AddExpression(boxed_param);
      }
    }
    // add default
    if(!boxed_param) {
      boxed_resolved_params->AddExpression(calling_param);
    }

    // encode parameter: the PARAMETER's dimension -- this read the function
    // type's own, which is 0, so an Int[] parameter never matched (#1183)
    dyn_func_params_str += EncodeType(func_param);
    for(int j = 0; j < func_param->GetDimension(); ++j) {
      dyn_func_params_str += L'*';
    }
    dyn_func_params_str += L',';
  }
  
  // method call parameters. This count becomes the DYN_MTHD_CALL operand,
  // which the JIT reads as the number of operand-stack WORDS holding the
  // arguments (it marshals operand + 2 entries with the func-ref's own two
  // words). A function-typed argument is two words, so counting parameters
  // left a call like apply(Test->Inc(Int) ~ Int, 4) one word short under the
  // JIT and crashed it; the interpreter ignores the operand.
  int param_words = 0;
  for(size_t i = 0; i < func_params.size(); ++i) {
    param_words += (func_params[i]->GetType() == FUNC_TYPE && func_params[i]->GetDimension() == 0) ? 2 : 1;
  }
  type->SetFunctionParameterCount(param_words);
  AnalyzeExpressions(boxed_resolved_params, depth + 1);

  // check parameters against the dynamic definition. An exact encoding is a
  // match; otherwise each argument is matched by the rules a named call uses,
  // so with g : (Base) ~ Base, g("hi") and g(Nil) are accepted -- they were
  // rejected for not being exactly Base (#1183)
  const std::wstring call_params_str = EncodeMethodCall(boxed_resolved_params, depth);
  bool params_match = dyn_func_params_str == call_params_str;
  if(!params_match) {
    params_match = true;
    const std::vector<Expression*> resolved_params = boxed_resolved_params->GetExpressions();
    for(size_t i = 0; i < resolved_params.size() && i < func_params.size(); ++i) {
      // 0 or 1: a function value's arguments are not unboxed
      const int compare = MatchCallingParameter(resolved_params[i], func_params[i], nullptr, nullptr, depth);
      if(compare < 0 || compare > 1) {
        params_match = false;
      }
    }
  }
  if(!params_match) {
    ProcessError(static_cast<Expression*>(method_call), L"Undefined function/method call: '" + call_name +
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

/********************************
 * Analyzes a curried call 'F(a)(b)':
 * 'method_call' applies the function
 * value 'expression' returned. The value
 * goes into a hidden local, and the call
 * is then a function-value call (#1184).
 ********************************/
void ContextAnalyzer::AnalyzeCurriedCall(Expression* expression, MethodCall* method_call, const int depth)
{
  // the callee's name, for messages
  std::wstring callee_name;
  if(expression->GetExpressionType() == METHOD_CALL_EXPR) {
    MethodCall* callee = static_cast<MethodCall*>(expression);
    callee_name = callee->IsCurriedCall() ? L"(..)" : callee->GetMethodName();
    if(callee_name.empty()) {
      callee_name = callee->GetVariableName();
    }
  }
  const std::wstring call_name = callee_name + L"(..)";

  // binary-op operands are analyzed more than once: reuse the first pass's local
  SymbolEntry* entry = method_call->GetFunctionalEntry();
  if(!entry) {
    // the previous link's own result (GetExpressionType would walk on into this link)
    Type* func_type = expression->GetEvalType();
    bool is_scalar = true;
    if(expression->GetExpressionType() == METHOD_CALL_EXPR && static_cast<MethodCall*>(expression)->GetCallIndices()) {
      is_scalar = false;
    }

    if(!func_type || func_type->GetType() != FUNC_TYPE || func_type->GetDimension() > 0 || !is_scalar) {
      const std::wstring type_name = func_type ? FormatGenericType(func_type) : L"Nil";
      ProcessError(static_cast<Expression*>(method_call), L"Cannot call the result of '" + call_name + L"': it is '" +
                   type_name + L"', not a function\n\t'F(a)(b)' calls the function value F(a) returns; end the statement with ';' if a new statement was meant");
      return;
    }

    if(!current_method || !current_table) {
      ProcessError(static_cast<Expression*>(method_call), L"A curried call must be inside a method or function");
      return;
    }

    static int curried_tmp_id = 0;
    const std::wstring tmp_name = current_method->GetName() + L":#curried_tmp#" + std::to_wstring(curried_tmp_id++);
    entry = TreeFactory::Instance()->MakeSymbolEntry(tmp_name, TypeFactory::Instance()->MakeType(func_type), false, true);
    current_table->AddEntry(entry, true);
    entry->WasLoaded();
  }

  AnalyzeFunctionValueCall(method_call, entry, call_name, depth);
}

/********************************
 * Analyzes a dynamic function
 * call
 ********************************/
void ContextAnalyzer::AnalyzeVariableFunctionCall(MethodCall* method_call, const int depth)
{
  // dynamic function call that is not bound to a class/function until runtime
  SymbolEntry* entry = GetEntry(method_call->GetMethodName());

  // a function or FuncRef variable captured from the enclosing scope of a
  // lambda: resolve it as a variable so the closure copy is created and the
  // call loads it from closure memory
  if(!entry && capture_lambda && !capture_frames.empty() &&
     ResolveCaptureEntry(method_call->GetMethodName(), capture_frames.size() - 1)) {
    Statement* mc_stmt = static_cast<Statement*>(method_call);
    Variable* variable = TreeFactory::Instance()->MakeVariable(mc_stmt->GetFileName(), mc_stmt->GetLineNumber(),
                                                               mc_stmt->GetLinePosition(), method_call->GetMethodName());
    AnalyzeVariable(variable, depth + 1);
    if(variable->GetEntry() && variable->GetEntry()->IsClosureEntry()) {
      entry = variable->GetEntry();
    }
  }

  // Direct call of a FuncRef<R> instance: `v()` desugars to `(v->Get())()`.
  // FuncRef only wraps a nullary `() ~ R`, so the call is always zero-arg.
  // Synthesize and resolve the `v->Get()` unwrap, materialize its `() ~ R`
  // result into a hidden local temp, then fall through to the ordinary
  // functional-call path on that temp -- reusing the existing
  // STOR/LOAD_FUNC_VAR + DYN_MTHD_CALL lowering (no VM/JIT change).
  if(entry && entry->GetType() && entry->GetType()->GetType() == CLASS_TYPE &&
     entry->GetType()->GetName() == L"System.FuncRef") {
    if(method_call->GetFuncRefUnwrap()) {
      // Already desugared on an earlier pass (binary-op operands are analyzed
      // more than once). Reuse the functional temp established the first time
      // instead of re-synthesizing, otherwise `entry` stays the raw FuncRef and
      // the call is wrongly rejected as undefined.
      entry = method_call->GetFunctionalEntry();
    }
    else {
      Statement* mc_stmt = static_cast<Statement*>(method_call);
      const std::wstring mc_file = mc_stmt->GetFileName();
      const int mc_ln = mc_stmt->GetLineNumber();
      const int mc_lp = mc_stmt->GetLinePosition();
      ExpressionList* no_args = TreeFactory::Instance()->MakeExpressionList();
      MethodCall* unwrap = TreeFactory::Instance()->MakeMethodCall(
        mc_file, mc_ln, mc_lp,
        method_call->GetMidLineNumber(), method_call->GetMidLinePosition(),
        mc_stmt->GetEndLineNumber(), mc_stmt->GetEndLinePosition(),
        method_call->GetMethodName(), L"Get", no_args);
      AnalyzeMethodCall(unwrap, depth + 1);

      Type* unwrap_type = unwrap->GetEvalType();
      if(unwrap_type && unwrap_type->GetType() == FUNC_TYPE) {
        // unique per desugar site -- two `v()` on one line share a line/position,
        // so a counter (not line:pos) is required to avoid temp-name collisions
        static int func_ref_tmp_id = 0;
        const std::wstring tmp_name = current_method->GetName() + L":#funcref_tmp#" +
          std::to_wstring(func_ref_tmp_id++);
        SymbolEntry* tmp = TreeFactory::Instance()->MakeSymbolEntry(tmp_name, unwrap_type, false, true);
        current_table->AddEntry(tmp, true);
        tmp->WasLoaded();
        method_call->SetFuncRefUnwrap(unwrap);
        entry = tmp;  // continue as a functional call on the unwrapped temp
      }
    }
  }

  if(entry && entry->GetType() && entry->GetType()->GetType() == FUNC_TYPE) {
    AnalyzeFunctionValueCall(method_call, entry, method_call->GetMethodName(), depth);
  }
  else if(!use_static_check) {
    const std::wstring &mthd_name = method_call->GetMethodName();
    const std::wstring &var_name = method_call->GetVariableName();

    if(ReportJsonLiteralValue(method_call) || ReportMapLiteralEntry(method_call)) {
      // said in terms of the literal
    }
    else if(mthd_name.size() > 0) {
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
  ResolveSiteScope site_scope(resolve_site, expression);
  // type cast
  if(expression->GetCastType()) {
    // the receiver's cast, already checked on the variable
    if(IsReceiverCast(expression)) {
      return;
    }

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

    // The cast converts THIS expression, so check it as if nothing followed it.
    // With the call chain attached, the class check typed the expression by the
    // chain's end: `u->As(IntRef)->Get()->ToString()` checked IntRef against
    // String. (A rule in GetExpressionType that typed every link by its
    // receiver's cast used to hide this, and crashed `u->As(IntRef)->Get() + 1`.)
    // Against the expression's own class, so a downcast still gets its run-time
    // check; a generic parameter (`@a->As(Stringify)` in a class of <A>) is no
    // class to compare with and takes the general path.
    MethodCall* chain = expression->GetMethodCall();
    if(chain && cast_type->GetType() == CLASS_TYPE) {
      expression->ClearMethodCall();
      if(root_type && root_type->GetType() == CLASS_TYPE && cast_type->GetDimension() == root_type->GetDimension() &&
         (SearchProgramClasses(root_type->GetName()) ||
          SearchLibraryClasses(root_type->GetName(), program->GetLibUses(current_class->GetFileName())))) {
        AnalyzeClassCast(cast_type, root_type, expression, false, depth + 1);
      }
      else {
        AnalyzeRightCast(cast_type, root_type, expression, IsScalar(expression), depth + 1);
      }
      expression->SetMethodCall(chain);
    }
    else {
      AnalyzeRightCast(cast_type, root_type, expression, IsScalar(expression), depth + 1);
    }
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
    else if(SearchLibraryClasses(type_of->GetName(), program->GetLibUses(current_class->GetFileName()))) {
      LibraryClass* lib_klass = SearchLibraryClasses(type_of->GetName(), program->GetLibUses(current_class->GetFileName()));
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

  // AnalyzeExpressions above has analyzed every index. Each was analyzed a second time
  // here, so an index that is itself a subscript was analyzed four times, the one inside
  // it eight: 'a[a[a[...]]]' took twice as long for every level (20 levels 1.7 s, 30
  // about half an hour), and anything analysis does once per visit was done twice.
  for(size_t i = 0; i < expressions.size(); ++i) {
    Expression* expression = expressions[i];    
    // What the index pushes is the value the expression ends in: for a call chain, what
    // its last call returns, not its head. Reading the first call's type refused
    // 'a[order->Line()->Count()]' (an object is not an index), '"ab"->Size()' and
    // '(s + t)->Size()' (Ints though they start with a String), and accepted
    // 'a[refs->Get(i)->Get()]' only by replacing the written 'Get()'.
    Expression* index_value = expression;
    while(index_value->GetMethodCall()) {
      index_value = index_value->GetMethodCall();
    }
    Type* eval_type = expression->GetEvalType() ? EffectiveStackType(expression) : nullptr;
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
          // an IntRef, CharRef or ByteRef index is its value; a FloatRef or a
          // BoolRef is not an index, as a Float or a Bool is not
          bool unboxed = false;
          Expression* unboxed_expresion = UnboxAs(TypeFactory::Instance()->MakeType(INT_TYPE), index_value, true, unboxed, depth);
          if(unboxed) {
            unboxed_expressions.push_back(unboxed_expresion ? unboxed_expresion : expression);
          }
          else {
            // Check if this is a bound variable from each loop
            if(expression->GetExpressionType() == VAR_EXPR) {
              Variable* var = static_cast<Variable*>(expression);
              ProcessError(expression, L"Expected Byte, Char, Int or Enum class type\n\tNote: Variable '" + var->GetName() +
                          L"' is bound to array elements using ':='\n\tUse ':' instead of ':=' to iterate over indices in 'each' loops");
            }
            else {
              ProcessError(expression, L"Expected Byte, Char, Int or Enum class type");
            }
          }
        }
        break;

      default:
        // Check if this is a bound variable from each loop
        if(expression->GetExpressionType() == VAR_EXPR) {
          Variable* var = static_cast<Variable*>(expression);
          ProcessError(expression, L"Expected Byte, Char, Int or Enum class type\n\tNote: Variable '" + var->GetName() +
                      L"' is bound to array elements using ':='\n\tUse ':' instead of ':=' to iterate over indices in 'each' loops");
        }
        else {
          ProcessError(expression, L"Expected Byte, Char, Int or Enum class type");
        }
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
    // `("[" + t + "]")->PrintLine();` -- the chain now hangs off the concatenation
    // node, which AnalyzeExpression already resolved; emit that node instead
    if(expression->GetPreviousExpression() && expression->GetPreviousExpression()->GetExpressionType() == STR_CONCAT_EXPR &&
       expression->GetPreviousExpression()->GetMethodCall()) {
      expression = expression->GetPreviousExpression();
      simple->SetExpression(expression);
    }
    else {
      AnalyzeExpressionMethodCall(expression, depth);
    }
    
    // ensure it's a valid statement
    if(!expression->GetMethodCall()) {
      ProcessError(expression, L"Invalid statement");
    }
    else {
      // The statement's value is discarded, so the last call of its chain must
      // pop what it returns. The call's own analysis marks it only at the top of
      // an expression, and a parenthesised receiver is analysed a level down:
      // `("a" + t)->Size();`, `(F(k))->Abs();` and `(t ?? "x")->Size();` each left
      // their result on the operand stack, one word for every time the statement
      // ran. Found by checking the operand stack's height at each return.
      RogueReturn(expression->GetMethodCall());
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
    expression = UnboxCondition(expression, depth);
    if_stmt->SetExpression(expression);
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
  // What the select is on, for the labels below: a label is a constant of the
  // same kind. Both stay false when the expression is neither (reported here).
  bool is_int_select = false;
  bool is_str_select = false;
  // 'match', or a 'select' used as a value, on an enum: its labels are checked against the enum
  Enum* subject_enum = nullptr;
  LibraryEnum* subject_lib_enum = nullptr;

  // expression
  Expression* expression = select_stmt->GetAssignment()->GetExpression();
  if(expression) {
    AnalyzeExpression(expression, depth + 1);
    // 'select(counts->Get(i))': an IntRef, CharRef or ByteRef is its value
    if(!IsIntegerExpression(expression)) {
      Expression* last = expression;
      while(last->GetMethodCall()) {
        last = last->GetMethodCall();
      }
      bool unboxed = false;
      Expression* replaced = UnboxAs(TypeFactory::Instance()->MakeType(INT_TYPE), last, true, unboxed, depth);
      if(unboxed && replaced) {
        expression = replaced;
        select_stmt->GetAssignment()->SetExpression(expression);
      }
    }
    const bool is_int_expr = IsIntegerExpression(expression);
    is_int_select = is_int_expr;
    if(select_stmt->IsChecked() && is_int_expr) {
      AnalyzeSelectSubjectEnum(expression, subject_enum, subject_lib_enum);
    }

#ifndef _SYSTEM
    const bool is_str_expr = IsStringExpression(expression);
    if(!is_int_expr && !is_str_expr) {
      ProcessError(expression, L"Expected integer or string expression");
    }

    is_str_select = is_str_expr && !is_int_expr;
    if(is_str_expr) {
      if(expression->GetExpressionType() == VAR_EXPR && !expression->GetMethodCall()) {
        Variable* var_expr = static_cast<Variable*>(expression);
        expression = TreeFactory::Instance()->MakeMethodCall(select_stmt->GetFileName(), select_stmt->GetLineNumber(),
                                                             select_stmt->GetLinePosition(), select_stmt->GetEndLineNumber(),
                                                             select_stmt->GetEndLinePosition(), -1, -1, var_expr, L"HashID",
                                                             TreeFactory::Instance()->MakeExpressionList());
        AnalyzeExpression(expression, depth + 1);
        select_stmt->GetAssignment()->SetExpression(expression);
      }
      else if(IsStringConstant(expression)) {
        const CharacterString* char_str_expr = static_cast<CharacterString*>(expression);
        expression = TreeFactory::Instance()->MakeIntegerLiteral(select_stmt->GetFileName(), select_stmt->GetLineNumber(), 
                                                                 select_stmt->GetLinePosition(), HashString(char_str_expr->GetString().c_str()));
        select_stmt->GetAssignment()->SetExpression(expression);
      }
      else {
        // Any other string: a call's result ('select(GetName())'), a variable
        // with calls after it ('select(name->Trim())'), an interpolated literal.
        // Its value exists only when the program runs, so the hash is taken
        // there, by a HashID() at the end of the expression's call chain. This
        // branch used to treat every string that was not a variable as a
        // literal: the node was read as a CharacterString whatever it was, and
        // the text hashed came from memory that was not a string. The select
        // then compared against that number and took 'other', or the compiler
        // stopped with "out of memory" or a crash while copying the "text".
        Expression* tail = expression;
        while(tail->GetMethodCall()) {
          tail = tail->GetMethodCall();
        }
        tail->SetMethodCall(TreeFactory::Instance()->MakeMethodCall(select_stmt->GetFileName(), select_stmt->GetLineNumber(),
                                                                    select_stmt->GetLinePosition(), -1, -1,
                                                                    select_stmt->GetEndLineNumber(), select_stmt->GetEndLinePosition(),
                                                                    L"", L"HashID", TreeFactory::Instance()->MakeExpressionList()));
        // resolved as AnalyzeExpression resolves a link: inside an expression,
        // so the call's value is not taken for one that nothing uses
        ++expression_depth;
        AnalyzeExpressionMethodCall(tail, depth + 1);
        --expression_depth;
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
  
  // duplicate value vector
  INT64_VALUE value = 0;
  std::map<INT64_VALUE, StatementList*> label_statements;
  // Iterate the cases in a deterministic (source-position) order. `statements` is
  // keyed by ExpressionList* (a heap pointer), so iterating it directly visits the
  // cases in pointer-address order, which varies on every compile (ASLR). The
  // case-label strings are interned into the char-string pool right here (via
  // AnalyzeExpressions, before they are hashed for the jump table), so that
  // pointer order leaked into the pool and made the emitted .obl non-reproducible.
  // Sorting by the first case expression's source position restores a stable,
  // parse-like order without changing semantics — label_statements is value-keyed,
  // so its final contents are independent of insertion order.
  std::vector<std::pair<ExpressionList*, StatementList*> > ordered_cases(statements.begin(), statements.end());
  std::sort(ordered_cases.begin(), ordered_cases.end(),
            [](const std::pair<ExpressionList*, StatementList*>& a, const std::pair<ExpressionList*, StatementList*>& b) {
              const std::vector<Expression*> a_exprs = a.first->GetExpressions();
              const std::vector<Expression*> b_exprs = b.first->GetExpressions();
              Expression* a_first = a_exprs.empty() ? nullptr : a_exprs.front();
              Expression* b_first = b_exprs.empty() ? nullptr : b_exprs.front();
              if(!a_first || !b_first) {
                return a_first < b_first;
              }
              if(a_first->GetLineNumber() != b_first->GetLineNumber()) {
                return a_first->GetLineNumber() < b_first->GetLineNumber();
              }
              return a_first->GetLinePosition() < b_first->GetLinePosition();
            });
  for(size_t c = 0; c < ordered_cases.size(); ++c) {
    // expressions
    ExpressionList* expressions = ordered_cases[c].first;
    AnalyzeExpressions(expressions, depth + 1);
    // check expression type
    std::vector<Expression*> expression_list = expressions->GetExpressions();
    for(size_t i = 0; i < expression_list.size(); ++i) {
      Expression* expression = expression_list[i];
      if(expression && (subject_enum || subject_lib_enum)) {
        // an item of another enum, or a number, would be compared by value
        MethodCall* item_call = expression->GetExpressionType() == METHOD_CALL_EXPR ? static_cast<MethodCall*>(expression) : nullptr;
        if(item_call && item_call->GetMethodCall()) {
          item_call = item_call->GetMethodCall();
        }
        const bool is_item = item_call && ((subject_enum && item_call->GetEnumItem() && item_call->GetEnumItem()->GetEnum() == subject_enum) ||
                                           (subject_lib_enum && item_call->GetLibraryEnumItem() && item_call->GetLibraryEnumItem()->GetEnum() == subject_lib_enum));
        if(!is_item) {
          const std::wstring enum_name = FormatTypeString(subject_enum ? subject_enum->GetName() : subject_lib_enum->GetName());
          ProcessError(expression, L"Expected an item of '" + enum_name + L"', the enum this '" + select_stmt->GetKeyword() + L"' is on");
          continue;
        }
      }
      if(expression) {
        switch(expression->GetExpressionType()) {
        case CHAR_STR_EXPR: {
          const CharacterString* char_str_expr = static_cast<CharacterString*>(expression);
          // A label is matched by the hash of its text, taken here. A label with
          // something interpolated ("{$v}") or called on it ("B"->ToLower()) has
          // no text until the program runs: its source text was hashed instead,
          // and the label then matched nothing, or the wrong value, in silence.
          if(!IsStringConstant(expression)) {
            ProcessError(expression, L"A select label must be a constant: a string literal with nothing interpolated and nothing called on it");
          }
          else if(is_int_select) {
            ProcessError(expression, L"String label in a select on an integer");
          }
          value = HashString(char_str_expr->GetString().c_str());
          if(DuplicateCaseItem(label_statements, value)) {
            ProcessError(expression, L"Duplicate select hash value for '" + char_str_expr->GetString() + L'\'');
          }
        }
          break;

        case CHAR_LIT_EXPR:
          CheckSelectIntegerLabel(expression, is_str_select);
          value = static_cast<CharacterLiteral*>(expression)->GetValue();
          if(DuplicateCaseItem(label_statements, value)) {
            ProcessError(expression, L"Duplicate select value '" + ToString(value) + L'\'');
          }
          break;

        case INT_LIT_EXPR:
          CheckSelectIntegerLabel(expression, is_str_select);
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
          if((mthd_call->GetEnumItem() || mthd_call->GetLibraryEnumItem()) && is_str_select) {
            ProcessError(expression, L"Integer label in a select on a string");
          }
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
      label_statements.insert(std::pair<INT64_VALUE, StatementList*>(value, ordered_cases[c].second));
    }
  }
  select_stmt->SetLabelStatements(label_statements);

  if(select_stmt->IsChecked()) {
    AnalyzeSelectCoverage(select_stmt, subject_enum, subject_lib_enum);
  }

  // process statements (in parse order)
  if(select_stmt->IsValue()) {
    AnalyzeSelectValues(select_stmt, depth);
  }
  else {
    std::vector<StatementList*> statement_lists = select_stmt->GetStatementLists();
    for(size_t i = 0; i < statement_lists.size(); ++i) {
      AnalyzeStatements(statement_lists[i], depth + 1);
    }
  }
}

/****************************
 * The enum a 'match' subject is, if it is one. A 'consts' group is a set of
 * named integers, not a closed set of items, so it does not count.
 ****************************/
void ContextAnalyzer::AnalyzeSelectSubjectEnum(Expression* expression, Enum*& eenum, LibraryEnum*& lib_eenum)
{
  while(expression->GetMethodCall()) {
    expression = expression->GetMethodCall();
  }

  Type* type = expression->GetCastType() ? expression->GetCastType() : expression->GetEvalType();
  if(type && type->GetType() == CLASS_TYPE && type->GetDimension() == 0) {
    eenum = SearchProgramEnums(type->GetName());
    if(eenum && eenum->IsConsts()) {
      eenum = nullptr;
    }
    else if(!eenum) {
      lib_eenum = SearchLibraryEnums(type->GetName(), program->GetLibUses());
      if(lib_eenum && lib_eenum->GetOffset() < 0) {
        lib_eenum = nullptr;
      }
    }
  }
}

/****************************
 * A 'match' (or a 'select' used as a value) takes some arm for every value it
 * can meet: with no 'other' arm, the labels of an enum subject name every item
 * of the enum, and the error names the items that are missing. A value needs
 * an arm for every subject, so one on anything but an enum needs 'other'.
 ****************************/
void ContextAnalyzer::AnalyzeSelectCoverage(Select* select_stmt, Enum* eenum, LibraryEnum* lib_eenum)
{
  if(select_stmt->GetOther()) {
    return;
  }

  const std::wstring keyword = select_stmt->GetKeyword();
  if(!eenum && !lib_eenum) {
    if(select_stmt->IsValue()) {
      ProcessError(select_stmt, L"A '" + keyword + L"' used as a value needs an 'other' arm, unless it is on an enum and names every item");
    }
    return;
  }

  // by value, in the order the items are numbered
  std::map<INT64_VALUE, std::wstring> items;
  if(eenum) {
    std::map<const std::wstring, EnumItem*> enum_items = eenum->GetItems();
    for(std::map<const std::wstring, EnumItem*>::iterator iter = enum_items.begin(); iter != enum_items.end(); ++iter) {
      items.insert(std::pair<INT64_VALUE, std::wstring>(iter->second->GetId(), iter->first));
    }
  }
  else {
    std::map<const std::wstring, LibraryEnumItem*> enum_items = lib_eenum->GetItems();
    for(std::map<const std::wstring, LibraryEnumItem*>::iterator iter = enum_items.begin(); iter != enum_items.end(); ++iter) {
      items.insert(std::pair<INT64_VALUE, std::wstring>(iter->second->GetId(), iter->first));
    }
  }

  const std::wstring enum_name = FormatTypeString(eenum ? eenum->GetName() : lib_eenum->GetName());
  const std::map<INT64_VALUE, StatementList*> label_statements = select_stmt->GetLabelStatements();
  std::wstring missing;
  for(std::map<INT64_VALUE, std::wstring>::iterator iter = items.begin(); iter != items.end(); ++iter) {
    if(label_statements.find(iter->first) == label_statements.end()) {
      if(!missing.empty()) {
        missing += L", ";
      }
      missing += iter->second;
    }
  }

  if(!missing.empty()) {
    ProcessError(select_stmt, L"This '" + keyword + L"' does not cover every item of '" + enum_name + L"', missing: " + missing +
                 L"\n\tAdd an arm for each missing item, or an 'other' arm.");
  }
}

/****************************
 * The arms of a 'match' or 'select' used as a value. Each is one assignment to
 * the hidden local the value is read from; the first gives the local its type
 * and the rest must agree with it, as the two branches of '?' must. A Nil arm
 * has no type of its own, so the typed arms go first. The arms hold no block,
 * so none opens a scope.
 ****************************/
void ContextAnalyzer::AnalyzeSelectValues(Select* select_stmt, const int depth)
{
  const std::wstring keyword = select_stmt->GetKeyword();

  std::vector<Assignment*> typed_arms;
  std::vector<Assignment*> nil_arms;
  std::vector<StatementList*> statement_lists = select_stmt->GetStatementLists();
  for(size_t i = 0; i < statement_lists.size(); ++i) {
    std::vector<Statement*> statements = statement_lists[i]->GetStatements();
    if(!statements.empty()) {
      Assignment* arm = static_cast<Assignment*>(statements.front());
      Expression* value = arm->GetExpression();
      if(value->GetExpressionType() == NIL_LIT_EXPR && !value->GetCastType() && !value->GetMethodCall()) {
        nil_arms.push_back(arm);
      }
      else {
        typed_arms.push_back(arm);
      }
    }
  }

  if(typed_arms.empty()) {
    if(!nil_arms.empty()) {
      ProcessError(select_stmt, L"Every arm of this '" + keyword + L"' is Nil, so its value has no type");
      // one error: the value is given a type, so what uses it has nothing to add
      Variable* value = nil_arms.front()->GetVariable();
      AnalyzeVariable(value, depth + 1);
      if(value->GetEntry()) {
        Type* base_type = TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.Base");
        value->GetEntry()->SetType(base_type);
        value->SetTypes(base_type);
      }
    }
    return;
  }

  Type* value_type = nullptr;
  for(size_t i = 0; i < typed_arms.size(); ++i) {
    Assignment* arm = typed_arms[i];
    // the casts the arm was written with: one more after the assignment is a
    // conversion the assignment made to fit the arm to the value's type
    Expression* head = arm->GetExpression();
    Expression* tail = head;
    while(tail->GetMethodCall()) {
      tail = tail->GetMethodCall();
    }
    Type* head_cast = head->GetCastType();
    Type* tail_cast = tail->GetCastType();

    const size_t errors = diagnostics.ErrorCount();
    AnalyzeStatement(arm, depth + 1);
    if(diagnostics.ErrorCount() != errors) {
      // the assignment has said what is wrong with this arm
      continue;
    }

    Type* arm_type = GetExpressionType(arm->GetExpression(), depth + 1);
    if(!arm_type || arm_type->GetType() == NIL_TYPE) {
      continue;
    }

    if(!value_type) {
      if(arm_type->GetType() == VAR_TYPE) {
        ProcessError(arm->GetExpression(), L"An arm of a '" + keyword + L"' used as a value has no value");
        return;
      }
      value_type = arm_type;
      continue;
    }

    // An Int arm after a Float one (or a Char after an Int) would be converted
    // without a word, and the value would depend on the order of the arms. Two
    // classes are checked as a cast by the assignment.
    Type* converted = head->GetCastType() != head_cast ? head->GetCastType() : (tail->GetCastType() != tail_cast ? tail->GetCastType() : nullptr);
    // a literal takes the value's type without a cast ('a' then 66): its own type tells
    Type* written = tail == head && !head_cast ? head->GetBaseType() : nullptr;
    const bool retyped = written && written->GetType() != CLASS_TYPE && written->GetType() != NIL_TYPE &&
      value_type->GetType() != CLASS_TYPE && written->GetType() != value_type->GetType();
    if((converted && converted->GetType() != CLASS_TYPE) || retyped || arm_type->GetType() != value_type->GetType()) {
      ProcessError(arm->GetExpression(), L"The arms of a '" + keyword + L"' used as a value differ in type: this one is not " + DescribeType(value_type) +
                   L", as an earlier one is\n\tConvert it explicitly, as in value->As(" + DescribeType(value_type) + L")");
    }
  }

  for(size_t i = 0; i < nil_arms.size(); ++i) {
    AnalyzeStatement(nil_arms[i], depth + 1);
  }
}

/****************************
 * Analyzes a critical section
 ****************************/
/****************************
 * Analyzes 'try { } on(e : T) { }
 * finally { }' (#1247): each handler's
 * type is System.Error or a subclass,
 * and none is unreachable behind an
 * earlier one
 ****************************/
void ContextAnalyzer::AnalyzeTry(Try* try_stmt, const int depth)
{
#ifdef _DEBUG
  Debug(L"try", try_stmt->GetLineNumber(), depth);
#endif

  if(try_stmt->GetHandlers().empty() && !try_stmt->GetFinally()) {
    ProcessError(try_stmt, L"A 'try' block needs an 'on' clause or a 'finally' block");
  }

  // the hidden locals hold System.Error
  ResolveClassEnumType(try_stmt->GetErrorEntry()->GetType());
  ResolveClassEnumType(try_stmt->GetRaiseEntry()->GetType());

  AnalyzeStatements(try_stmt->GetBody(), depth + 1);

  const std::vector<TryHandler*> &handlers = try_stmt->GetHandlers();
  std::vector<std::wstring> taken;
  for(size_t i = 0; i < handlers.size(); ++i) {
    TryHandler* handler = handlers[i];

    Type* type = handler->type;
    bool valid = type->GetType() == CLASS_TYPE && type->GetDimension() == 0 && ResolveClassEnumType(type);
    Class* klass = valid ? SearchProgramClasses(type->GetName()) : nullptr;
    LibraryClass* lib_klass = valid && !klass ? SearchLibraryClasses(type->GetName(), program->GetLibUses(current_class->GetFileName())) : nullptr;
    if(!valid || (!klass && !lib_klass) || !ValidDownCast(L"System.Error", klass, lib_klass)) {
      ProcessError(try_stmt, L"An 'on' clause takes System.Error or a subclass of it");
    }
    else {
      // an earlier clause for this class or one of its parents takes every error this one would
      for(size_t j = 0; j < taken.size(); ++j) {
        if(ValidDownCast(taken[j], klass, lib_klass)) {
          ProcessError(try_stmt, L"This 'on' clause is unreachable: an earlier one takes '" + taken[j] + L"'");
          break;
        }
      }
      taken.push_back(type->GetName());
    }

    AnalyzeStatements(handler->statements, depth + 1);
  }

  if(try_stmt->GetFinally()) {
    finally_loops.push_back(in_loop);
    AnalyzeStatements(try_stmt->GetFinally(), depth + 1);
    finally_loops.pop_back();
  }
}

/****************************
 * Analyzes 'using(w := E) { ... }': a scope holding 'w', the body (its own
 * scope) and the 'w->Close()' that runs when the body is left. The parser
 * made the same three scopes.
 ****************************/
void ContextAnalyzer::AnalyzeUsing(Using* using_stmt, const int depth)
{
#ifdef _DEBUG
  Debug(L"using", using_stmt->GetLineNumber(), depth);
#endif

  current_table->NewScope();
  ResolveClassEnumType(using_stmt->GetRaiseEntry()->GetType());
  AnalyzeStatement(using_stmt->GetResource(), depth + 1);
  AnalyzeStatements(using_stmt->GetBody(), depth + 1);
  const size_t errors_before_close = diagnostics.ErrorCount();
  AnalyzeStatements(using_stmt->GetClose(), depth + 1);
  if(diagnostics.ErrorCount() > errors_before_close) {
    ProcessError(using_stmt, L"'using' closes its resource with 'Close()': the type of the resource needs a method named 'Close' that takes no arguments");
  }
  current_table->PreviousScope();
}

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

  // A range or 'each' loop always has a '<' condition (the parser builds
  // 'index < range' or 'index < collection->Size()'). Any other condition --
  // a Bool literal, a variable or a call -- is not a CalculatedExpression, and
  // reading its operands read unrelated memory: 'for(...; true; ...)' crashed
  // the compiler.
  CalculatedExpression* cond_expr = nullptr;
  Expression* for_expr = for_stmt->GetExpression();
  if(for_expr && for_expr->GetExpressionType() == LES_EXPR) {
    cond_expr = static_cast<CalculatedExpression*>(for_expr);
  }

  if(cond_expr && cond_expr->GetRight() && cond_expr->GetRight()->GetExpressionType() == METHOD_CALL_EXPR) {
    MethodCall* mthd_call_expr = static_cast<MethodCall*>(cond_expr->GetRight());
    const std::wstring cond_expr_name = mthd_call_expr->GetVariableName();
    if(IsRangeName(cond_expr_name)) {
      AnalyzeMethodCall(mthd_call_expr, depth + 1);
      is_range = true;
    }
    else {
      SymbolEntry* cond_expr_entry = current_table->GetEntry(current_method->GetName() + L':' + cond_expr_name);
      if(!cond_expr_entry) {
        // a range captured by an enclosing lambda (#866): without this the range
        // went undetected and the loop was lowered as a collection, so the user
        // was told their Range has no Size()
        cond_expr_entry = ResolveCapturedVariable(cond_expr_name, for_stmt, depth);
      }
      if(cond_expr_entry && cond_expr_entry->GetType() && cond_expr_entry->GetType()->GetType() == CLASS_TYPE && IsRangeName(cond_expr_entry->GetType()->GetName())) {
        Variable* variable = TreeFactory::Instance()->MakeVariable(for_stmt->GetFileName(), for_stmt->GetLineNumber(), for_stmt->GetLinePosition(), cond_expr_name);
        cond_expr_entry->WasLoaded();
        variable->SetEntry(cond_expr_entry);
        variable->SetEvalType(cond_expr_entry->GetType(), true);
        cond_expr->SetRight(variable);
        is_range = true;

        // A captured range lives in closure memory, and its id indexes that memory,
        // not the frame. The loop re-reads the range from a frame slot on every
        // iteration, so give it one of its own -- the same '#<index>_range' local the
        // parser declares for a range expression -- rather than letting the emitter
        // store the closure id as a local slot and clobber whatever local owns it.
        if(cond_expr_entry->IsClosureEntry()) {
          const std::wstring holder_name = current_method->GetName() + L":#" +
            static_cast<Variable*>(cond_expr->GetLeft())->GetName() + L"_range";
          if(!current_table->GetEntry(holder_name)) {
            SymbolEntry* holder_entry = TreeFactory::Instance()->MakeSymbolEntry(holder_name, cond_expr_entry->GetType(), false, true);
            holder_entry->WasLoaded();
            current_table->AddEntry(holder_entry, true);
          }
        }

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
      expression = UnboxCondition(expression, depth);
      for_stmt->SetExpression(expression);

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
    expression = UnboxCondition(expression, depth);
    do_while_stmt->SetExpression(expression);
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
    expression = UnboxCondition(expression, depth);
    while_stmt->SetExpression(expression);
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
  // the block of a 'spawn' is a thread's body: there is nobody to return a value to
  if(expression && current_method->IsSpawnBody()) {
    AnalyzeExpression(expression, depth + 1);
    ProcessError(rtrn, L"A 'spawn' block does not return a value\n\tUse 'return;' to end the thread, and hand a result back through an object the block shares");
    in_return = false;
    return;
  }
  if(expression) {
    // auto-wrap a bare lambda when the return type is FuncRef<R>
    TypeMapLiteral(expression, mthd_type);
    Expression* wrapped = WrapBareLambdaInFuncRef(expression, mthd_type, depth);
    if(wrapped == expression) {
      wrapped = WrapMethodReferenceInFuncRef(expression, mthd_type, depth);
    }
    if(wrapped == expression) {
      wrapped = WrapCollectionLiteral(expression, mthd_type, depth);
    }
    if(wrapped != expression) {
      rtrn->SetExpression(wrapped);
      expression = wrapped;
    }
    // 'return Vector->New();' takes its type arguments from the method's return type
    ExpectNewType(expression, mthd_type, false);
    AnalyzeExpression(expression, depth + 1);
    expected_new_call = nullptr;
    expected_new_type = nullptr;
    // Handle string concatenation: replace ADD_EXPR with STR_CONCAT_EXPR
    if(expression->GetPreviousExpression() && expression->GetPreviousExpression()->GetExpressionType() == STR_CONCAT_EXPR) {
      expression = expression->GetPreviousExpression();
      rtrn->SetExpression(expression);
    }
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
      cast_kind = 2;
      cast_subject = L"Method '" + current_method->GetUserName() + L"'";
      Expression* box_expression = AnalyzeRightCast(mthd_type, expression, (IsScalar(expression) && mthd_type->GetDimension() == 0), depth + 1);
      cast_kind = 0;
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

void ContextAnalyzer::ValidateConcrete(Type* cls_type, Type* concrete_type, ParseNode* node, const int depth, Class* scope)
{
  if(!cls_type || !concrete_type) {
    return;
  }

  const std::wstring concrete_type_name = concrete_type->GetName();
  Class* concrete_klass = nullptr; LibraryClass* concrete_lib_klass = nullptr;
  if(!GetProgramOrLibraryClass(concrete_type, concrete_klass, concrete_lib_klass)) {
    concrete_klass = (scope ? scope : current_class)->GetGenericClass(concrete_type_name);
  }

  if(concrete_klass || concrete_lib_klass) {
    const bool is_concrete_inf = ((concrete_klass && concrete_klass->IsInterface()) ||
      (concrete_lib_klass && concrete_lib_klass->IsInterface())) ? true : false;

    if(!is_concrete_inf) {
      const std::wstring cls_type_name = cls_type->GetName();
      Class* dclr_klass = nullptr; LibraryClass* dclr_lib_klass = nullptr;
      if(!GetProgramOrLibraryClass(cls_type, dclr_klass, dclr_lib_klass)) {
        dclr_klass = (scope ? scope : current_class)->GetGenericClass(cls_type_name);
      }

      if(dclr_klass && dclr_klass->HasGenerics()) {
        const std::vector<Type*> concrete_types = concrete_type->GetGenerics();
        if(concrete_types.empty()) {
          ProcessError(node, L"Generic to concrete size mismatch");
        }
        else {
          ValidateGenericConcreteMapping(concrete_types, dclr_klass, node, scope);
        }
      }
      else if(dclr_lib_klass && dclr_lib_klass->HasGenerics()) {
        const std::vector<Type*> concrete_types = concrete_type->GetGenerics();
        if(concrete_types.empty()) {
          ProcessError(node, L"Generic to concrete size mismatch");
        }
        else {
          ValidateGenericConcreteMapping(concrete_types, dclr_lib_klass, node, scope);
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
/****************************
 * Only `name := value` declares a variable. A field (`@name`) is declared
 * in its class, and the target of +=, -= and the like must already exist:
 * `@cuont += 1` used to declare a new local and compile (#1184). The entry
 * is still created afterwards so analysis carries on.
 ****************************/
void ContextAnalyzer::CheckImplicitDeclaration(Variable* variable, bool is_target)
{
  const std::wstring &name = variable->GetName();
  if(!name.empty() && name[0] == L'@') {
    ProcessError(variable, L"Undefined variable: '" + name + L"'"
                 L"\n\tFields are declared in the class; check the spelling.");
  }
  else if(in_operation_assignment) {
    ProcessError(variable, L"Undefined variable: '" + name + L"'"
                 L"\n\tA compound assignment needs an existing variable; declare it with ':=' first.");
  }
  // a name that is only read: 'y := zzz' reported "Invalid operation using
  // classes: Var and Var", and 'Foo(zzz)' an undefined Foo (#1184)
  else if(!is_target && !variable->IsInternalVariable() && !variable->IsAlt() && !name.empty() && name[0] != L'#') {
    ProcessError(variable, L"Undefined variable: '" + name + L"'"
                 L"\n\tCheck spelling, ensure the variable is declared in the current scope, or add a 'use' statement for external bundles.");
  }
}

void ContextAnalyzer::AnalyzeAssignment(Assignment* assignment, StatementType type, const int depth)
{
  const size_t errors_before = diagnostics.ErrorCount();
  AnalyzeAssignmentBody(assignment, type, depth);

  // A variable whose assignment failed has no type to speak of, and what the program
  // later does with it is not a new mistake.
  if(diagnostics.ErrorCount() > errors_before) {
    Variable* variable = assignment->GetVariable();
    if(variable && variable->GetEntry()) {
      poisoned_variables.insert(variable->GetEntry()->GetName());
    }
  }
}

void ContextAnalyzer::AnalyzeAssignmentBody(Assignment* assignment, StatementType type, const int depth)
{
#ifdef _DEBUG
  Debug(L"assignment", assignment->GetLineNumber(), depth);
#endif

  in_assignment = true;

  Variable* variable = assignment->GetVariable();
  if(variable) {
    in_operation_assignment = type != ASSIGN_STMT;
    in_assignment_target = type == ASSIGN_STMT;
    AnalyzeVariable(variable, depth + 1);
    in_assignment_target = false;
    in_operation_assignment = false;
    if(current_class && current_class->IsReadonlyRecord() && current_method &&
       current_method->GetMethodType() != NEW_PUBLIC_METHOD && current_method->GetMethodType() != NEW_PRIVATE_METHOD &&
       variable->GetName().size() > 0 && variable->GetName()[0] == L'@') {
      ProcessError(variable, L"Cannot assign to readonly record field outside constructor");
    }
  }

  // get last expression for assignment
  Expression* expression = assignment->GetExpression();
  if(expression) {
    // auto-wrap a bare lambda when the assignment target is FuncRef<R>
    if(variable && variable->GetEvalType()) {
      TypeMapLiteral(expression, variable->GetEvalType());
      Expression* wrapped = WrapBareLambdaInFuncRef(expression, variable->GetEvalType(), depth);
      if(wrapped == expression) {
        wrapped = WrapMethodReferenceInFuncRef(expression, variable->GetEvalType(), depth);
      }
      if(wrapped == expression) {
        wrapped = WrapCollectionLiteral(expression, variable->GetEvalType(), depth);
      }
      if(wrapped != expression) {
        assignment->SetExpression(wrapped);
        expression = wrapped;
      }
    }
    // 'v : Vector<Foo> := Vector->New();' takes its type arguments from 'v'
    if(type == ASSIGN_STMT && variable && variable->GetEntry()) {
      ExpectNewType(expression, variable->GetEntry()->GetType(), variable->GetIndices() != nullptr);
    }
    AnalyzeExpression(expression, depth + 1);
    expected_new_call = nullptr;
    expected_new_type = nullptr;
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
          // a jagged array's row is an array (#1246)
          if(entry->GetType() && expr_variable->GetIndices() && !expr_variable->IsRowValue()) {
            variable->GetBaseType()->SetDimension(0);
            variable->GetEvalType()->SetDimension(0);
            entry->GetType()->SetDimension(0);
          }
        }
        // ... or a call's result: 'x := f->GetCells()[1]' declared x an Int[]
        // holding the element, and the first use of x dereferenced it (#1184)
        else if(expression->GetExpressionType() == METHOD_CALL_EXPR &&
                static_cast<MethodCall*>(expression)->GetCallIndices() && entry->GetType()) {
          Type* scalar_type = TypeFactory::Instance()->MakeType(entry->GetType());
          scalar_type->SetDimension(0);
          variable->SetTypes(scalar_type);
          entry->SetType(scalar_type);
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
          // storing an item into an element ('a[i] := Color->Blue') must not retype
          // the array entry as the scalar enum: the GC declarations would then mark
          // the array slot INT_PARM (never traced) and free the live array
          if(!variable->GetIndices()) {
            variable->SetTypes(from_type);
            to_entry->SetType(from_type);
          }
        }
      }
    }

    // Diagnostic for the rare generic-mismatch miss (intermittent CI failure in
    // bad_generic_arg_mismatch). Enabled only when OBJECK_DIAG_GENERICS is set, so
    // it adds no overhead and changes no behavior by default. When a generic-typed
    // variable is assigned, it records whether the expression's eval type carries
    // generics (-> name-compared, mismatch caught at :5235) or not (-> only the
    // arg COUNT is checked at the else-if below, where a name mismatch slips through).
    {
      static const bool diag_generics = []{
#ifdef _WIN32
        size_t len = 0; char buf[8];
        return getenv_s(&len, buf, sizeof(buf), "OBJECK_DIAG_GENERICS") == 0 && len > 0;
#else
        return std::getenv("OBJECK_DIAG_GENERICS") != nullptr;
#endif
      }();
      if(diag_generics && variable->GetEvalType() && variable->GetEvalType()->GetType() == CLASS_TYPE &&
         variable->GetEntry() && variable->GetEntry()->GetType() && variable->GetEntry()->GetType()->HasGenerics()) {
        Type* et = expression->GetEvalType();
        const bool is_mc = (expression->GetExpressionType() == METHOD_CALL_EXPR);
        std::wcerr << L"[generic-diag] " << variable->GetLineNumber() << L':' << variable->GetLinePosition()
                   << L" var=" << variable->GetEvalType()->GetName()
                   << L" varGenN=" << variable->GetEntry()->GetType()->GetGenerics().size()
                   << L" exprEval=" << (et ? et->GetName() : L"<null>")
                   << L" exprHasGen=" << ((et && et->HasGenerics()) ? L"Y" : L"N")
                   << L" exprGenN=" << (et ? et->GetGenerics().size() : (size_t)0)
                   << L" isMethodCall=" << (is_mc ? L"Y" : L"N")
                   << L" hasConcrete=" << ((is_mc && static_cast<MethodCall*>(expression)->HasConcreteTypes()) ? L"Y" : L"N")
                   << L" -> branch=" << ((et && et->HasGenerics()) ? L"name-check" : L"count-only")
                   << std::endl;
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
      if(variable->GetEntry()->GetType()) {
        const std::vector<Type*> var_types = variable->GetEntry()->GetType()->GetGenerics();
        const std::vector<Type*> concrete_types = mthd_call->GetConcreteTypes();
        if(var_types.size() != concrete_types.size()) {
          ProcessError(variable, L"Generic size mismatch, ensure calling parameters and generic types match");
        }
        else {
          // Same invariant name check as the generics branch above. It is applied
          // here too so a mismatch is caught deterministically even when the
          // expression's eval type did not carry resolved generics: that path took
          // this count-only branch and skipped the comparison, intermittently
          // letting a bad assignment (e.g. Vector<IntRef> -> Vector<String>) slip
          // through. The concrete types come from the explicit '<...>' arguments,
          // so they are already resolved.
          for(size_t i = 0; i < var_types.size(); ++i) {
            Type* var_type = var_types[i];
            ResolveClassEnumType(var_type);

            Type* concrete_type = concrete_types[i];
            ResolveClassEnumType(concrete_type);

            if(var_type->GetName() != concrete_type->GetName()) {
              ProcessError(variable, L"Generic type mismatch for class '" + variable->GetEntry()->GetType()->GetName() +
                           L"' between generic types: '" + FormatTypeString(var_type->GetName()) +
                           L"' and '" + FormatTypeString(concrete_type->GetName()) + L"'");
            }
          }
        }
      }
    }

    Type* left_type = variable->GetEvalType();
    bool check_right_cast = true;
    if(left_type && left_type->GetType() == CLASS_TYPE) {
#ifndef _SYSTEM
      LibraryClass* left_class = SearchLibraryClasses(left_type->GetName(), program->GetLibUses(current_class->GetFileName()));
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
            LibraryClass* right_class = SearchLibraryClasses(right_type->GetName(), program->GetLibUses(current_class->GetFileName()));
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
                  ProcessFatalError(assignment, L"Internal compiler error.");
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
              ProcessFatalError(assignment, L"Internal compiler error.");
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
            ProcessFatalError(assignment, L"Internal compiler error.");
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
      cast_kind = 1;
      cast_subject = variable->GetName();
      Expression* box_expression = AnalyzeRightCast(variable, expression, (IsScalar(variable) && IsScalar(expression)), depth + 1);
      cast_kind = 0;
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
    // Each operand is analyzed once, here: AnalyzeExpression dispatches a
    // calculated operand back into AnalyzeCalculation. This used to recurse
    // into a calculated operand directly first and then analyze it again
    // through AnalyzeExpression, so every level of a nested expression was
    // analyzed twice and a left-nested chain took time exponential in its
    // length: twenty terms compiled in three seconds, twenty-four in over a
    // minute, and a thirty-term sum never finished (nine gigabytes and
    // counting).
    AnalyzeExpression(left, depth + 1);
    AnalyzeExpression(right, depth + 1);

    // An operand that is a concatenation with a call chain, `("a" + t)->Size() * 2`:
    // the chain was resolved on the concatenation node, so that node is the operand
    // (as for a return value, an assignment and a call argument).
    if(left->GetPreviousExpression() && left->GetPreviousExpression()->GetExpressionType() == STR_CONCAT_EXPR &&
       left->GetPreviousExpression()->GetMethodCall()) {
      left = left->GetPreviousExpression();
      expression->SetLeft(left);
    }
    if(right->GetPreviousExpression() && right->GetPreviousExpression()->GetExpressionType() == STR_CONCAT_EXPR &&
       right->GetPreviousExpression()->GetMethodCall()) {
      right = right->GetPreviousExpression();
      expression->SetRight(right);
    }

    // check operations
    AnalyzeCalculationCast(expression, depth);

    // a holder variable that was unboxed is now the call that reads its value
    left = expression->GetLeft();
    right = expression->GetRight();

    // check for valid operation cast; an operand whose cast is followed by a call
    // chain, `x->As(Kid)->Only() <> "only"`, casts the receiver, which AnalyzeCast
    // already checked: its eval type is the chain's end, not the cast's source
    if(left->GetCastType() && left->GetEvalType() && !left->GetMethodCall()) {
      AnalyzeRightCast(left->GetCastType(), left->GetEvalType(), left, IsScalar(left), depth);
    }

    // check for valid operation cast
    if(right->GetCastType() && right->GetEvalType() && !right->GetMethodCall()) {
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

  // A conversion (an Int made a Float for 'n * 2.5') belongs to the value, and
  // the value of an operand that is a call chain is what its LAST call returns.
  // The emitter converts after the call that carries the cast, by that call's
  // return type; on the first call of 'a->B()->Count() * 2.5' it was set on a
  // call that returns an object, nothing was converted and the Int's bits were
  // multiplied as a Float.
  Expression* left_value = left_expr;
  while(left_value && left_value->GetMethodCall()) {
    left_value = left_value->GetMethodCall();
  }
  Expression* right_value = right_expr;
  while(right_value && right_value->GetMethodCall()) {
    right_value = right_value->GetMethodCall();
  }

  Type* left = GetExpressionType(left_expr, depth + 1);
  Type* right = GetExpressionType(right_expr, depth + 1);

  if(!left || !right) {
    return;
  }

  const std::wstring op = ExpressionTypeToOperator(expression->GetExpressionType());

  if(!IsScalar(left_expr) || !IsScalar(right_expr)) {
    // check for valid equal and not-equal checks, consider Nil and array based equal and not-equal checks
    if(!(right->GetType() == NIL_TYPE && (expression->GetExpressionType() == EQL_EXPR || expression->GetExpressionType() == NEQL_EXPR)) && left->GetDimension() != right->GetDimension()) {
      ProcessError(left_expr, L"Invalid array operation '" + op + L"'");
    }
  }
  else {
    const std::wstring left_name = ShortTypeName(left);
    const std::wstring right_name = ShortTypeName(right);

    switch(left->GetType()) {
    case VAR_TYPE: {
      // An operand still typed Var here never got a concrete type. Inside a
      // method an unknown identifier is silently turned into a new
      // type-inferred entry -- that is how `x := 1` declares one -- so using a
      // name that was never declared arrives here rather than being reported
      // where it was read. Name it, with the guidance the class-level check
      // already gives, instead of printing the internal placeholder type.
      const std::wstring var_name = NamedVariable(left_expr);
      if(!var_name.empty()) {
        ProcessError(left_expr, L"Undefined variable: '" + var_name + L"'"
                     L"\n\tCheck spelling, ensure the variable is declared in the current scope, or add a 'use' statement for external bundles.");
        break;
      }

      // VAR
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Var and Function");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Var and Var");
        break;

      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Var and Nil");
        break;

      case BYTE_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Var and Byte");
        break;

      case CHAR_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Var and Char");
        break;

      case INT_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Var and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Var and Float");
        break;

      case CLASS_TYPE:
        if(HasProgramOrLibraryEnum(right->GetName())) {
          ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Var and Enum");
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Var and Bool");
        break;
      }
    }
      break;

    case ALIAS_TYPE:
      break;

    case NIL_TYPE:
      // NIL
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Nil and Function");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Nil and Var");
        break;

      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Nil and Nil");
        break;

      case BYTE_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Nil and Byte");
        break;

      case CHAR_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Nil and Char");
        break;

      case INT_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Nil and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Nil and Float");
        break;

      case CLASS_TYPE:
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Nil and Bool");
        break;
      }
      break;

    case BYTE_TYPE:
      // BYTE
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Byte and Function");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Byte and Var");
        break;

      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Byte and Nil");
        break;

      case CHAR_TYPE:
      case INT_TYPE:
      case BYTE_TYPE:
        expression->SetEvalType(left, true);
        break;

      case FLOAT_TYPE:
        left_value->SetCastType(right, true);
        expression->SetEvalType(right, true);
        break;

      case CLASS_TYPE:
        if(HasProgramOrLibraryEnum(right->GetName())) {
          right_value->SetCastType(left, true);
          expression->SetEvalType(left, true);
        }
        else if(!UnboxingCalculation(right, right_expr, expression, false, depth)) {
          ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Byte and " +
                       FormatTypeString(right->GetName()));
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Byte and Bool");
        break;
      }
      break;

    case CHAR_TYPE:
      // CHAR
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Char and Function");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Char and Var");
        break;

      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Char and Nil");
        break;

      case INT_TYPE:
      case CHAR_TYPE:
      case BYTE_TYPE:
        expression->SetEvalType(left, true);
        break;

      case FLOAT_TYPE:
        left_value->SetCastType(right, true);
        expression->SetEvalType(right, true);
        break;

      case CLASS_TYPE:
        if(HasProgramOrLibraryEnum(right->GetName())) {
          right_value->SetCastType(left, true);
          expression->SetEvalType(left, true);
        }
        else if(!UnboxingCalculation(right, right_expr, expression, false, depth)) {
          ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Char and " +
                       FormatTypeString(right->GetName()));
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Char and Bool");
        break;
      }
      break;

    case INT_TYPE:
      // INT
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Int and Function");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Int and Var");
        break;

      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Int and Nil");
        break;

      case BYTE_TYPE:
      case CHAR_TYPE:
      case INT_TYPE:
        expression->SetEvalType(left, true);
        break;

      case FLOAT_TYPE:
        left_value->SetCastType(right, true);
        expression->SetEvalType(right, true);
        break;

      case CLASS_TYPE:
        if(HasProgramOrLibraryEnum(right->GetName())) {
          right_value->SetCastType(left, true);
          expression->SetEvalType(left, true);
        }
        else if(!UnboxingCalculation(right, right_expr, expression, false, depth)) {
          ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Int and " +
                       FormatTypeString(right->GetName()));
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Int and Bool");
        break;
      }
      break;

    case FLOAT_TYPE:
      // FLOAT
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Float and Function");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Float and Var");
        break;

      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Float and Nil");
        break;

      case FLOAT_TYPE:
        expression->SetEvalType(left, true);
        break;

      case BYTE_TYPE:
      case CHAR_TYPE:
      case INT_TYPE:
        right_value->SetCastType(left, true);
        expression->SetEvalType(left, true);
        break;

      case CLASS_TYPE:
        if(HasProgramOrLibraryEnum(right->GetName())) {
          right_value->SetCastType(left, true);
          expression->SetEvalType(left, true);
        }
        else if(UnboxingCalculation(right, right_expr, expression, false, depth)) {
          expression->SetEvalType(left, true);
        }
        else {
          ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Float and " +
                       FormatTypeString(right->GetName()));
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Float and Bool");
        break;
      }
      break;

    case CLASS_TYPE:
      // CLASS
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: " +
                     FormatTypeString(left->GetName()) +
                     L" and Function");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: " +
                     FormatTypeString(left->GetName()) +
                     L" and Var");
        break;

      case ALIAS_TYPE:
      case NIL_TYPE:
        break;

      case BYTE_TYPE:
        if(HasProgramOrLibraryEnum(left->GetName())) {
          left_value->SetCastType(right, true);
          expression->SetEvalType(right, true);
        }
        else if(!UnboxingCalculation(left, left_expr, expression, true, depth)) {
          ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: " +
                       FormatTypeString(left->GetName()) + L" and Byte");
        }
        break;

      case CHAR_TYPE:
        if(HasProgramOrLibraryEnum(left->GetName())) {
          left_value->SetCastType(right, true);
          expression->SetEvalType(right, true);
        }
        else if(!UnboxingCalculation(left, left_expr, expression, true, depth)) {
          ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: " +
                       FormatTypeString(left->GetName()) + L" and Char");
        }
        break;

      case INT_TYPE:
        if(HasProgramOrLibraryEnum(left->GetName())) {
          left_value->SetCastType(right, true);
          expression->SetEvalType(right, true);
        }
        else if(!UnboxingCalculation(left, left_expr, expression, true, depth)) {
          ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: " + FormatTypeString(left->GetName()) + L" and Int");
        }
        break;

      case FLOAT_TYPE:
        if(HasProgramOrLibraryEnum(left->GetName())) {
          left_value->SetCastType(right, true);
          expression->SetEvalType(right, true);
        }
        else if(!UnboxingCalculation(left, left_expr, expression, true, depth)) {
          ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: " + FormatTypeString(left->GetName()) + L" and Float");
        }
        break;

      case CLASS_TYPE: {
        ResolveClassEnumType(left);
        const bool can_unbox_left = UnboxingCalculation(left, left_expr, expression, true, depth);
        const bool is_left_enum = HasProgramOrLibraryEnum(left->GetName());

        ResolveClassEnumType(right);
        const bool can_unbox_right = UnboxingCalculation(right, right_expr, expression, false, depth);
        const bool is_right_enum = HasProgramOrLibraryEnum(right->GetName());

        // Two holders: both are now their values, and the calculation was worked
        // out for those (an Int and a Float, two Bools, ...). What follows reads
        // the holders' types, which made every result an Int or a Float -- a
        // 'BoolRef & BoolRef' an Int -- and cast one holder class to the other.
        if(can_unbox_left && can_unbox_right) {
          break;
        }

        if(!can_unbox_left && !is_left_enum && !can_unbox_right && !is_right_enum &&
           expression->GetExpressionType() != EQL_EXPR && expression->GetExpressionType() != NEQL_EXPR) {
          ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: " + FormatTypeString(left->GetName()) + L" and " + FormatTypeString(right->GetName()));
        }

        if((is_left_enum && !is_right_enum) || (!is_left_enum && is_right_enum)) {
          ProcessError(left_expr, L"Invalid operation '" + op + L"' between class and enum: '" + FormatTypeString(left->GetName()) +
                       L"' and '" + FormatTypeString(right->GetName()) + L"'");
        }
        else if((is_left_enum && is_right_enum) || (can_unbox_left && can_unbox_right)) {
          AnalyzeClassCast(left, right, left_expr, false, depth + 1);
        }
        else if(can_unbox_left && !is_right_enum) {
          ProcessError(left_expr, L"Invalid operation '" + op + L"' between class and enum: '" + left->GetName() + L"' and '" + right->GetName() + L"'");
        }
        else if(can_unbox_right && !is_left_enum) {
          ProcessError(left_expr, L"Invalid operation '" + op + L"' between class and enum: '" + left->GetName() + L"' and '" + right->GetName() + L"'");
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
        // 'flags->Get(i) & done' and '<>flags->Get(i)': a BoolRef is its value
        if(!UnboxingCalculation(left, left_expr, expression, true, depth)) {
          ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: " + FormatTypeString(left->GetName()) + L" and Bool");
        }
        break;
      }
      break;

    case BOOLEAN_TYPE:
      // BOOLEAN
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Bool and Function");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Bool and Var");
        break;

      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Bool and Nil");
        break;

      case BYTE_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Bool and Byte");
        break;

      case CHAR_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Bool and Char");
        break;

      case INT_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Bool and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Bool and Float");
        break;

      case CLASS_TYPE:
        if(HasProgramOrLibraryEnum(right->GetName())) {
          right_value->SetCastType(left, true);
          expression->SetEvalType(left, true);
        }
        else if(!UnboxingCalculation(right, right_expr, expression, false, depth)) {
          ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Bool and " +
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
          ProcessError(expression, L"Invalid operation '" + op + L"' between functions: " +
                       FormatTypeString(left->GetName()) + L" and " +
                       FormatTypeString(right->GetName()));
        }
      }
                      break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Function and Var");
        break;

      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Function and Nil");
        break;

      case BYTE_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Function and Byte");
        break;

      case CHAR_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Function and Char");
        break;

      case INT_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Function and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Function and Float");
        break;

      case CLASS_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Function and " +
                     FormatTypeString(right->GetName()));
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation '" + op + L"' between types: Function and Bool");
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
    // the holder is what the LAST call of the chain returned: 'a->B()->C() + 1'
    // reads C's. Adding 'Get()' after the first call dropped the rest of the
    // chain and called Get() on what B returned.
    Expression* last = expression;
    while(last->GetMethodCall()) {
      last = last->GetMethodCall();
    }
    // Unboxing one operand works out the calculation again, which unboxes the
    // other; the pass that started it then reaches that operand with the type
    // it read before. The chain already ends in the value.
    EntryType held;
    if(!last->GetEvalType() || !HolderPrimitive(last->GetEvalType(), held)) {
      return true;
    }
    ExpressionList* box_expressions = TreeFactory::Instance()->MakeExpressionList();
    MethodCall* box_method_call = TreeFactory::Instance()->MakeMethodCall(expression->GetFileName(), expression->GetLineNumber(), expression->GetLinePosition(),
                                                                          -1, -1, expression->GetLineNumber(), expression->GetLinePosition(),
                                                                          type->GetName(), L"Get", box_expressions);
    last->SetMethodCall(box_method_call);
    // Only the added call is analyzed, and the calculation's types are worked
    // out from its operands as they now are. Analyzing the whole calculation
    // again analyzed both operands again, which unboxed again: two holders of
    // different kinds ('ints->Get(0) + floats->Get(0)') never ended, and the
    // compiler ran out of stack.
    AnalyzeExpressionMethodCall(last, depth + 1);
    AnalyzeCalculationCast(calc_expression, depth + 1);
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
    case FLOAT_TYPE:
      // a holder returned as a primitive is unboxed by the rule an assignment
      // follows (AnalyzeRightCast, which the caller runs next). Unboxing it
      // here returned the LAST call of a chain as the whole expression, so
      // 'return a->B()->Get(0);' lost 'a->B()', and held a Float as an Int
      // without converting it.
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
  // a jagged array's row takes an array (#1246)
  if(variable->GetIndices() && !is_scalar && !variable->IsRowValue()) {
    ProcessError(expression, L"Dimension size mismatch");
  }

  return box_expression;
}

Expression* ContextAnalyzer::AnalyzeRightCast(Type* left, Expression* expression, bool is_scalar, const int depth)
{
  return AnalyzeRightCast(left, GetExpressionType(expression, depth + 1), expression, is_scalar, depth);
}

// True when 'expression' carries an explicit scalar Byte, Char or Int cast
// ('f->As(Byte)'), which a Float value converts through on its own
bool ContextAnalyzer::HasIntegralCast(Expression* expression)
{
  Type* cast_type = expression->GetCastType();
  if(!cast_type || cast_type->GetDimension() > 0) {
    return false;
  }

  switch(cast_type->GetType()) {
  case BYTE_TYPE:
  case CHAR_TYPE:
  case INT_TYPE:
    return true;

  default:
    return false;
  }
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
        // keep an explicit integral cast: the parameter type replaced
        // 'f->As(Byte)' and the Byte wrap was lost (#1176)
        if(!HasIntegralCast(expression)) {
          expression->SetCastType(left, false);
        }
        expression->SetEvalType(right, false);
        break;

      case CLASS_TYPE:
        if(!HasProgramOrLibraryEnum(right->GetName())) {
          bool unboxed = false;
          Expression* unboxed_expresion = UnboxAs(left, expression, false, unboxed, depth);
          if(unboxed) {
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
        // keep an explicit integral cast: the parameter type replaced
        // 'f->As(Byte)' and the Byte wrap was lost (#1176)
        if(!HasIntegralCast(expression)) {
          expression->SetCastType(left, false);
        }
        expression->SetEvalType(right, false);
        break;

      case CLASS_TYPE:
        if(!HasProgramOrLibraryEnum(right->GetName())) {
          bool unboxed = false;
          Expression* unboxed_expresion = UnboxAs(left, expression, false, unboxed, depth);
          if(unboxed) {
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
        // keep an explicit integral cast: the parameter type replaced
        // 'f->As(Byte)' and the Byte wrap was lost (#1176)
        if(!HasIntegralCast(expression)) {
          expression->SetCastType(left, false);
        }
        expression->SetEvalType(right, false);
        break;

      case CLASS_TYPE:
        if(!HasProgramOrLibraryEnum(right->GetName())) {
          bool unboxed = false;
          Expression* unboxed_expresion = UnboxAs(left, expression, false, unboxed, depth);
          if(unboxed) {
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
          // 'f : Float := floats->Get(0);'
          bool unboxed = false;
          Expression* unboxed_expresion = UnboxAs(left, expression, false, unboxed, depth);
          if(unboxed) {
            return unboxed_expresion;
          }
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
          bool unboxed = false;
          Expression* unboxed_expresion = UnboxAs(left, expression, false, unboxed, depth);
          if(unboxed) {
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
      AnalyzeArrayClassCast(left, right, expression, depth + 1);
    }

    expression->SetEvalType(left, false);
  }

  return nullptr;
}

/****************************
 * The primitive a holder class
 * holds: IntRef an Int, FloatRef
 * a Float, BoolRef a Bool, CharRef
 * a Char and ByteRef a Byte
 ****************************/
bool ContextAnalyzer::HolderPrimitive(Type* type, EntryType &primitive)
{
  // an element of an array of holders is a holder, and its type still carries
  // the array's dimension: whether the value is a scalar is for the caller
  if(!type || type->GetType() != CLASS_TYPE) {
    return false;
  }

  ResolveClassEnumType(type);
  const std::wstring &name = type->GetName();
  if(name == L"System.IntRef") {
    primitive = INT_TYPE;
  }
  else if(name == L"System.FloatRef") {
    primitive = FLOAT_TYPE;
  }
  else if(name == L"System.BoolRef") {
    primitive = BOOLEAN_TYPE;
  }
  else if(name == L"System.CharRef") {
    primitive = CHAR_TYPE;
  }
  else if(name == L"System.ByteRef") {
    primitive = BYTE_TYPE;
  }
  else {
    return false;
  }

  return true;
}

/****************************
 * Whether a primitive is stored
 * as another, by the rule an
 * assignment between two
 * primitives follows: Byte, Char,
 * Int and Float convert among
 * themselves, a Bool is only a
 * Bool
 ****************************/
bool ContextAnalyzer::PrimitiveConverts(EntryType to, EntryType from)
{
  const bool to_number = to == BYTE_TYPE || to == CHAR_TYPE || to == INT_TYPE || to == FLOAT_TYPE;
  const bool from_number = from == BYTE_TYPE || from == CHAR_TYPE || from == INT_TYPE || from == FLOAT_TYPE;
  if(to == BOOLEAN_TYPE || from == BOOLEAN_TYPE) {
    return to == from;
  }

  return to_number && from_number;
}

/****************************
 * How a primitive argument fits a
 * holder parameter: 2 when the
 * holder is the argument's own
 * (an Int for an IntRef), 3 when
 * the argument is converted first
 * (an Int for a FloatRef), -1 when
 * it cannot be (an Int for a
 * BoolRef) and 0 when this is not
 * a primitive for a holder
 ****************************/
int ContextAnalyzer::BoxingRank(Type* method_type, Expression* expr_param, const int depth)
{
  EntryType held;
  if(!method_type || !expr_param || method_type->GetDimension() > 0 || !HolderPrimitive(method_type, held)) {
    return 0;
  }

  // an enum item is boxed as it always was
  if(expr_param->GetExpressionType() == METHOD_CALL_EXPR && static_cast<MethodCall*>(expr_param)->GetEnumItem()) {
    return 2;
  }

  Type* from_type = GetExpressionType(expr_param, depth);
  if(!from_type || !IsScalar(expr_param)) {
    return 0;
  }

  switch(from_type->GetType()) {
  case BOOLEAN_TYPE:
  case BYTE_TYPE:
  case CHAR_TYPE:
  case INT_TYPE:
  case FLOAT_TYPE:
    if(!PrimitiveConverts(held, from_type->GetType())) {
      return -1;
    }
    return held == from_type->GetType() ? 2 : 3;

  default:
    return 0;
  }
}

/****************************
 * A holder where a primitive is
 * wanted: reads its value with
 * 'Get()' and converts it as the
 * primitive it holds, so an
 * IntRef for a Float is the Int
 * made a Float, and a BoolRef for
 * an Int is refused as a Bool is.
 *
 * 'last' is the end of the
 * expression's call chain. A
 * variable is replaced by the
 * call that is returned; a call
 * has the 'Get()' added after it
 * and nothing is returned.
 * 'unboxed' tells the two apart
 * from "not a holder".
 ****************************/
Expression* ContextAnalyzer::UnboxAs(Type* to_type, Expression* last, bool integral_only, bool &unboxed, const int depth)
{
  // 'to_type' may carry an array's dimension: an element's type does
  // ('a[i] := refs->Get(i)'), and the caller has found the value a scalar
  unboxed = false;
  if(!to_type || !last || last->GetMethodCall()) {
    return nullptr;
  }

  switch(to_type->GetType()) {
  case BOOLEAN_TYPE:
  case BYTE_TYPE:
  case CHAR_TYPE:
  case INT_TYPE:
  case FLOAT_TYPE:
    break;

  default:
    return nullptr;
  }

  Type* from_type = GetExpressionType(last, depth + 1);
  EntryType held;
  if(!from_type || !IsScalar(last) || !HolderPrimitive(from_type, held) || !PrimitiveConverts(to_type->GetType(), held)) {
    return nullptr;
  }

  // the conversion is of one value: the cast it leaves on the call must not
  // carry an array's dimension
  Type* value_type = TypeFactory::Instance()->MakeType(to_type);
  value_type->SetDimension(0);
  to_type = value_type;

  if(integral_only && held != BYTE_TYPE && held != CHAR_TYPE && held != INT_TYPE) {
    return nullptr;
  }

  if(last->GetExpressionType() == VAR_EXPR) {
    MethodCall* get_call = TreeFactory::Instance()->MakeMethodCall(last->GetFileName(), last->GetLineNumber(), last->GetLinePosition(),
                                                                   -1, -1, -1, -1, static_cast<Variable*>(last), L"Get",
                                                                   TreeFactory::Instance()->MakeExpressionList());
    AnalyzeMethodCall(get_call, depth);
    if(!get_call->GetEvalType()) {
      return nullptr;
    }
    AnalyzeRightCast(to_type, get_call->GetEvalType(), get_call, true, depth);
    unboxed = true;
    return get_call;
  }

  if(last->GetExpressionType() == METHOD_CALL_EXPR) {
    MethodCall* get_call = TreeFactory::Instance()->MakeMethodCall(last->GetFileName(), last->GetLineNumber(), last->GetLinePosition(),
                                                                   -1, -1, -1, -1, from_type->GetName(), L"Get",
                                                                   TreeFactory::Instance()->MakeExpressionList());
    last->SetMethodCall(get_call);
    AnalyzeExpressionMethodCall(last, depth + 1);
    if(!get_call->GetEvalType()) {
      return nullptr;
    }
    AnalyzeRightCast(to_type, get_call->GetEvalType(), get_call, true, depth);
    unboxed = true;
    return nullptr;
  }

  return nullptr;
}

/****************************
 * A BoolRef where a condition is
 * wanted is its value. Returns
 * the expression to test.
 ****************************/
Expression* ContextAnalyzer::UnboxCondition(Expression* expression, const int depth)
{
  if(!expression || IsBooleanExpression(expression)) {
    return expression;
  }

  Expression* last = expression;
  while(last->GetMethodCall()) {
    last = last->GetMethodCall();
  }

  bool unboxed = false;
  Expression* replaced = UnboxAs(TypeFactory::Instance()->MakeType(BOOLEAN_TYPE), last, false, unboxed, depth);
  return unboxed && replaced ? replaced : expression;
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
      // only into a holder the value converts to: an Int is not boxed as a
      // BoolRef. The caller reports the two types; building the call reported
      // a constructor that does not exist, 'BoolRef->New(Int)'.
      EntryType held;
      if(!is_enum && HolderPrimitive(to_type, held) && !PrimitiveConverts(held, from_type->GetType())) {
        return nullptr;
      }
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
/****************************
 * An array of one class where an
 * array of another is wanted.
 * An object array holds bare
 * references, and a read dispatches
 * on each element's own class, so
 * an array of a class serves as an
 * array of a parent or of an
 * interface the class implements
 * ('String[]' for 'Compare[]').
 * There is no element type to check
 * at run time, so the elements cannot
 * be checked for a narrowing ('Base[]'
 * for 'Kid[]'): that takes an
 * explicit As. The element cast
 * AnalyzeClassCast sets is never
 * emitted for an array: on the array
 * itself it failed, "Invalid object
 * cast: '?' to 'System.Compare'",
 * for an array literal argument,
 * while a variable's was skipped.
 ****************************/
void ContextAnalyzer::AnalyzeArrayClassCast(Type* left, Type* right, Expression* expression, const int depth)
{
  Class* to_class = expression->GetToClass();
  LibraryClass* to_lib_class = expression->GetToLibraryClass();
  AnalyzeClassCast(left, expression, depth);
  const bool needs_check = expression->GetToClass() != to_class || expression->GetToLibraryClass() != to_lib_class;
  expression->SetToClass(to_class);
  expression->SetToLibraryClass(to_lib_class);
  if(!needs_check) {
    return;
  }

  // explicit: 'a->As(Kid[])'
  Type* cast_type = expression->GetCastType();
  if(cast_type && cast_type->GetDimension() > 0) {
    return;
  }

  // a widening to an interface sets a check too, which an element never needs
  Type* from_type = GetExpressionType(expression, depth + 1);
  if(!from_type) {
    from_type = right;
  }
  ResolveClassEnumType(left);
  ResolveClassEnumType(from_type);
  Class* from_class = SearchProgramClasses(from_type->GetName());
  LibraryClass* from_lib_class = from_class ? nullptr :
    SearchLibraryClasses(from_type->GetName(), program->GetLibUses(current_class->GetFileName()));
  if(!from_class && !from_lib_class) {
    return;
  }

  Class* to_klass = SearchProgramClasses(left->GetName());
  LibraryClass* to_lib_klass = to_klass ? nullptr : SearchLibraryClasses(left->GetName(), program->GetLibUses(current_class->GetFileName()));
  const std::wstring to_name = to_klass ? to_klass->GetName() : (to_lib_klass ? to_lib_klass->GetName() : left->GetName());
  if(!ValidDownCast(to_name, from_class, from_lib_class)) {
    ProcessError(expression, L"Cannot use an array of '" + FormatTypeString(from_type->GetName()) + L"' as an array of '" +
                 FormatTypeString(left->GetName()) + L"': array elements are not checked at run time; use As(" +
                 FormatTypeString(left->GetName()) + L"[])");
  }
}

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

  // a jagged array's row type (#1246) is no class: two row types are the same
  // type exactly when they name the same rows
  if((left && IsRowTypeName(left->GetName())) || (right && IsRowTypeName(right->GetName()))) {
    if(left && right) {
      ResolveClassEnumType(left);
      ResolveClassEnumType(right);
      if(left->GetName() != right->GetName()) {
        ProcessError(expression, L"Invalid cast between jagged array types: '" + FormatTypeString(left->GetName().substr(1)) +
                     L"[]' and '" + FormatTypeString(right->GetName().substr(1)) + L"[]'");
      }
    }
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
    else if(right && SearchLibraryEnums(right->GetName(), program->GetLibUses(current_class->GetFileName()))) {
      LibraryEnum* right_lib_enum = SearchLibraryEnums(right->GetName(), program->GetLibUses(current_class->GetFileName()));
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
    else if(SearchLibraryClasses(right->GetName(), program->GetLibUses(current_class->GetFileName()))) {
      LibraryClass* right_lib_class = SearchLibraryClasses(right->GetName(), program->GetLibUses(current_class->GetFileName()));
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
  else if(left && right && (left_lib_enum = SearchLibraryEnums(left->GetName(), program->GetLibUses(current_class->GetFileName())))) {
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
    else if(SearchLibraryEnums(right->GetName(), program->GetLibUses(current_class->GetFileName()))) {
      LibraryEnum* right_lib_enum = SearchLibraryEnums(right->GetName(), program->GetLibUses(current_class->GetFileName()));
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
  else if(left && right && (left_lib_class = SearchLibraryClasses(left->GetName(), program->GetLibUses(current_class->GetFileName())))) {
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
    else if(SearchLibraryClasses(right->GetName(), program->GetLibUses(current_class->GetFileName()))) {
      LibraryClass* right_lib_class = SearchLibraryClasses(right->GetName(), program->GetLibUses(current_class->GetFileName()));
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
      // A name already reported as an undefined class does not need a second,
      // differently worded complaint about the same name (#cascade): the reader
      // has one mistake to fix, not two.
      if(undefined_classes.find(left->GetName()) == undefined_classes.end()) {
        ProcessError(expression, L"Unknown class cast type: '" + left->GetName() + L"'");
      }
    }
    else {
      ProcessError(expression, L"Invalid class, enum or method call context\n\tEnsure all required libraries have been included");
    }
  }
}

bool ContextAnalyzer::CheckGenericEqualTypes(Type* left, Type* right, Expression* expression, bool check_only, bool left_is_target)
{
  // is `sub` a subtype of (assignable to) `super`? equal types qualify.
  auto IsArgSubtype = [&](Type* sub, Type* super) -> bool {
    if(StructuralGenericEquals(sub, super)) {
      return true;
    }
    Class* sub_klass = nullptr; LibraryClass* sub_lib_klass = nullptr;
    if(GetProgramOrLibraryClass(sub, sub_klass, sub_lib_klass)) {
      return ValidDownCast(super->GetName(), sub_klass, sub_lib_klass);
    }
    return false;
  };

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
      ProcessError(expression, L"Generic type argument count mismatch: '" + FormatGenericType(left) + L"' and '" + FormatGenericType(right) + L"'");
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

        // Declaration-site variance: if the i-th type parameter of the (shared)
        // generic class is covariant ('out') or contravariant ('in'), accept a
        // subtype/supertype argument instead of requiring exact equality. The
        // target/source roles depend on which side is the assignment target.
        GenericVariance variance = GENERIC_INVARIANT;
        if(left_klass && i < left_klass->GetGenericClasses().size()) {
          variance = left_klass->GetGenericClasses()[i]->GetVariance();
        }
        else if(lib_left_klass && i < lib_left_klass->GetGenericClasses().size()) {
          variance = lib_left_klass->GetGenericClasses()[i]->GetVariance();
        }
        if(variance != GENERIC_INVARIANT && left_generic_type->IsResolved()) {
          Type* target_arg = left_is_target ? left_generic_type : right_generic_type;
          Type* source_arg = left_is_target ? right_generic_type : left_generic_type;
          const bool variance_ok = (variance == GENERIC_COVARIANT)
              ? IsArgSubtype(source_arg, target_arg)   // out: source <: target
              : IsArgSubtype(target_arg, source_arg);  // in:  target <: source
          if(variance_ok) {
            continue;
          }
        }

        // Compare the resolved argument types by their (recursively-built) names.
        // This is the original, behavior-preserving comparison — the structural
        // rewrite regressed a real stdlib overload (openai), so the proven logic
        // is kept; only the user-facing diagnostic is modernized.
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
            ProcessError(expression, L"Incompatible generic type arguments: '" + FormatGenericType(left) + L"' and '" + FormatGenericType(right) + L"'");
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
  ResolveSiteScope site_scope(resolve_site, declaration);
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

      ValidateConcrete(type, type, declaration, depth, klass);
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

    // A value given at class scope has been refused above. It is not analyzed: there is no
    // method to analyze it in, and the analysis of an assignment reads the current method
    // (obc ended with a segmentation fault on 'keys : Vector<String> := Vector->New();'
    // written between two methods).
    if(statement && (current_method || entry->IsLocal())) {
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
    if(expression->GetExpressionType() == METHOD_CALL_EXPR) {
      Expression* wrapped = WrapMethodReferenceInFuncRef(expression, nullptr, depth);
      if(wrapped != expression) {
        parameters->SetExpression(wrapped, i);
        expression = wrapped;
      }
    }
    AnalyzeExpression(expression, depth);    
    if(expression->GetPreviousExpression() && expression->GetPreviousExpression()->GetExpressionType() == STR_CONCAT_EXPR) {
      parameters->SetExpression(expression->GetPreviousExpression(), i);
    }
  }
}

/********************************
 * Encodes a function definition
 ********************************/
std::wstring ContextAnalyzer::EncodeFunctionReference(ExpressionList* calling_params, [[maybe_unused]] const int depth)
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
      // a function type written in the reference itself, `Ap((Int) ~ Int)`:
      // the parser left the type on a marker variable (#1183)
      else if(variable->GetName() == FUNC_TYPE_MARKER && variable->GetEvalType() &&
              variable->GetEvalType()->GetType() == FUNC_TYPE) {
        Type* func_type = variable->GetEvalType();
        encoded_name += L"m." + EncodeFunctionType(func_type->GetFunctionParameters(), func_type->GetFunctionReturn());
      }
      // function type encoded by MapLambdaDeclarations ("m.(i,)~i"); an
      // identifier can never start with "m." so this cannot shadow a class
      else if(variable->GetName().compare(0, 2, L"m.") == 0) {
        encoded_name += variable->GetName();
        Type* func_type = TypeParser::ParseType(variable->GetName());
        if(func_type) {
          variable->SetEvalType(func_type, true);
        }
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
          LibraryClass* lib_klass = SearchLibraryClasses(klass_name, program->GetLibUses(current_class->GetFileName()));
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
      // a jagged array's row is an array value (#1246)
      if(static_cast<Variable*>(expression)->IsRowValue()) {
        return false;
      }
      indices = static_cast<Variable*>(expression)->GetIndices();
    }
    else if(expression->GetExpressionType() == METHOD_CALL_EXPR) {
      // 'GetItems()[0]' subscripts the call's result, so the expression is
      // scalar exactly when the call carries indices.
      indices = static_cast<MethodCall*>(expression)->GetCallIndices();
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
    // An array of Bool is not a boolean -- unless it has been subscripted, in
    // which case the result is a scalar even though the eval type still carries
    // the array's dimension. The dimension test used to be applied to variables
    // only, so a method call returning Bool[] counted as a boolean and
    // `if(obj->Predict(x) = Nil)` was rejected as a boolean operation with a
    // non-boolean operand, while the same comparison on Float[], or on a Bool[]
    // held in a local, was accepted. A call carries its subscript separately
    // from a variable's, so both are gathered here.
    ExpressionList* indices = nullptr;
    if(expression->GetExpressionType() == VAR_EXPR) {
      indices = static_cast<Variable*>(expression)->GetIndices();
    }
    else if(expression->GetExpressionType() == METHOD_CALL_EXPR) {
      indices = static_cast<MethodCall*>(expression)->GetCallIndices();
    }

    return !eval_type->GetDimension() || indices;
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
      if(SearchLibraryEnums(eval_type->GetName(), program->GetLibUses())) {
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

// An integer or character literal used as a select label: nothing may be called
// on it (the call was ignored and the literal's own value used), and it cannot
// label a select on a string, whose labels are compared by hash.
void ContextAnalyzer::CheckSelectIntegerLabel(Expression* expression, bool is_str_select)
{
  if(expression->GetMethodCall()) {
    ProcessError(expression, L"A select label must be a constant: a literal with nothing called on it");
  }
  else if(is_str_select) {
    ProcessError(expression, L"Integer label in a select on a string");
  }
}

// A string literal whose text is all there is to it: nothing interpolated and
// nothing called on it, so its hash is known while compiling.
bool ContextAnalyzer::IsStringConstant(Expression* expression)
{
  if(expression->GetExpressionType() != CHAR_STR_EXPR || expression->GetMethodCall()) {
    return false;
  }

  const std::vector<CharacterStringSegment*> segments = static_cast<CharacterString*>(expression)->GetSegments();
  for(size_t i = 0; i < segments.size(); ++i) {
    if(segments[i]->GetType() != STRING) {
      return false;
    }
  }

  return true;
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
      if(SearchLibraryEnums(eval_type->GetName(), program->GetLibUses())) {
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

bool ContextAnalyzer::DuplicateCaseItem(const std::map<INT64_VALUE, StatementList*>& label_statements, INT64_VALUE value)
{
  std::map<INT64_VALUE, StatementList*>::const_iterator result = label_statements.find(value);
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
    // a receiver captured by a lambda is loaded from closure memory, so an
    // instance call on it is valid even though the lambda itself is static
    SymbolEntry* entry = GetEntry(method_call->GetVariableName());
    if(entry && (entry->IsLocal() || entry->IsStatic() || entry->IsClosureEntry())) {
      return false;
    }

    Variable* variable = method_call->GetVariable();
    if(variable) {
      entry = variable->GetEntry();
      if(entry && (entry->IsLocal() || entry->IsStatic() || entry->IsClosureEntry())) {
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
  if(!not_variable || !not_static_prev || !not_new) {
    return false;
  }

  // An unqualified call of an instance method is a call on self. From an
  // instance method of a class that inherits the method from a library class
  // -- 'GetName()' in a Thread's Run() -- that is valid, as the program-class
  // overload above has it; this overload never looked at the calling method,
  // so it rejected every such call. From a function, or a lambda (which does
  // not capture self), there is no instance to call it on.
  if(!current_method || current_method->IsStatic() || !current_class) {
    return true;
  }

  const std::wstring& lib_class_name = lib_method->GetLibraryClass()->GetName();
  LibraryClass* lib_parent = nullptr;
  // the program classes' chain ends at the first library parent
  for(Class* parent = current_class; parent && !lib_parent; parent = parent->GetParent()) {
    lib_parent = parent->GetLibraryParent();
  }

  const std::vector<std::wstring>& uses = program->GetLibUses(current_class->GetFileName());
  for(int guard = 0; lib_parent && guard < 256; ++guard) {
    if(lib_parent->GetName() == lib_class_name) {
      return false;
    }

    const std::wstring& parent_name = lib_parent->GetParentName();
    LibraryClass* next = parent_name.empty() ? nullptr : linker->SearchClassLibraries(parent_name, uses);
    lib_parent = next != lib_parent ? next : nullptr;
  }

  return true;
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

/****************************
 * Resolves a name inside a lambda body to its closure copy, creating the copy if
 * this is the first reference. Copies are made lazily by AnalyzeVariable and are
 * registered in the class table under <lambdaMethod>:<var>, where GetEntry never
 * looks, so a site that resolves a name directly must fall back to this before
 * reporting it undefined: lookup alone misses a copy that exists, and misses
 * outright when this reference is the one that should create it (#865, #866).
 * Returns nullptr when the name is not captured by the enclosing lambdas.
 ****************************/
SymbolEntry* ContextAnalyzer::ResolveCapturedVariable(const std::wstring& name, ParseNode* node, const int depth)
{
  if(!capture_lambda || capture_frames.empty() || !ResolveCaptureEntry(name, capture_frames.size() - 1)) {
    return nullptr;
  }

  Variable* variable = TreeFactory::Instance()->MakeVariable(node->GetFileName(), node->GetLineNumber(),
                                                             node->GetLinePosition(), name);
  AnalyzeVariable(variable, depth + 1);
  SymbolEntry* entry = variable->GetEntry();
  return entry && entry->IsClosureEntry() ? entry : nullptr;
}

/****************************
 * Resolves a variable captured by the lambda in capture_frames[frame_index]:
 * a local or parameter of the method the lambda is written in, or -- when that
 * method is itself a lambda body -- a variable of an enclosing scope, captured
 * into each enclosing lambda's closure on the way in. Closure copies live in the
 * class table, not the lambda method's own table, so looking only in that table
 * missed them and a nested lambda re-declared the name as an untyped Var local
 * ("Invalid operation using classes: Var and Int").
 ****************************/
SymbolEntry* ContextAnalyzer::ResolveCaptureEntry(const std::wstring& name, size_t frame_index)
{
  const CaptureFrame& frame = capture_frames[frame_index];
  if(!frame.method || !frame.table) {
    return nullptr;
  }

  SymbolEntry* entry = frame.table->GetEntry(frame.method->GetName() + L':' + name);
  if(entry || !frame.method->IsLambda() || frame_index == 0) {
    return entry;
  }

  // frame.method is the body of the enclosing lambda
  const CaptureFrame& outer = capture_frames[frame_index - 1];
  if(!outer.lambda || outer.lambda->GetMethod() != frame.method) {
    return nullptr;
  }

  SymbolEntry* outer_capture = ResolveCaptureEntry(name, frame_index - 1);
  if(!outer_capture) {
    return nullptr;
  }
  outer_capture->WasLoaded();

  SymbolEntry* copy_entry = outer.lambda->GetClosure(outer_capture);
  if(!copy_entry) {
    const std::wstring var_scope_name = frame.method->GetName() + L':' + name;
    copy_entry = TreeFactory::Instance()->MakeSymbolEntry(var_scope_name, outer_capture->GetType(), false, false);
    copy_entry->SetClosureEntry();
    symbol_table->GetSymbolTable(current_class->GetName())->AddEntry(copy_entry, true);
    outer.lambda->AddClosure(copy_entry, outer_capture);
  }

  return copy_entry;
}

/****************************
 * A lambda written in an instance method may read and write the instance's fields
 * (@x) and name the instance (@self). It captures the instance it was created in
 * as one more closure value, the way it captures a local; a field is then loaded
 * through that copy, so a write is a write to the field, not to a copy of it, and
 * a later read sees it. Returns false (the caller reports the usual error) when
 * the use is not inside a lambda directly in an instance method; a lambda inside
 * a lambda does not capture the instance.
 ****************************/
bool ContextAnalyzer::CaptureLambdaSelf(Variable* variable, SymbolEntry* entry)
{
  if(!current_method || !current_method->IsLambda() || !capture_lambda || capture_frames.size() != 1) {
    return false;
  }

  Method* enclosing = capture_frames[0].method;
  if(!enclosing || enclosing->IsStatic() || enclosing->IsLambda() ||
     entry->IsLocal() || entry->IsStatic() || entry->IsClosureEntry()) {
    return false;
  }

  SymbolEntry* self_entry = entry->IsSelf() ? entry : GetEntry(SELF_ID);
  if(!self_entry || !self_entry->IsSelf()) {
    return false;
  }
  self_entry->WasLoaded();

  SymbolEntry* copy_entry = capture_lambda->GetClosure(self_entry);
  if(!copy_entry) {
    copy_entry = TreeFactory::Instance()->MakeSymbolEntry(current_method->GetName() + L':' + SELF_ID, self_entry->GetType(), false, false);
    copy_entry->SetClosureEntry();
    symbol_table->GetSymbolTable(current_class->GetName())->AddEntry(copy_entry, true);
    capture_lambda->AddClosure(copy_entry, self_entry);
  }

  if(entry->IsSelf()) {
    variable->SetTypes(copy_entry->GetType());
    variable->SetEntry(copy_entry);
    copy_entry->AddVariable(variable);
  }
  else {
    variable->SetLambdaSelf(copy_entry);
  }

  return true;
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

      // favor a cast on the call itself. A cast on the RECEIVER (`u->As(IntRef)`,
      // parsed as a cast of 'u') says what the call is made on, not what it
      // returns: typing `u->As(IntRef)->Get()` as IntRef made `+ 1` unbox the Int
      // result a second time -- a field load through an integer, a crash in
      // every mode -- and rejected `u->As(IntRef)->Get()->ToString()` as an
      // invalid cast.
      // `s->ToUpper()->TypeOf(String)` is a Bool, whatever ToUpper returns: typed
      // as a String, `b->PrintLine()` called String:PrintLine on the Bool (a crash)
      if(mthd_call->GetTypeOf()) {
        type = TypeFactory::Instance()->MakeType(BOOLEAN_TYPE);
      }
      else if(mthd_call->GetCastType() && !IsReceiverCast(mthd_call)) {
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
        LibraryClass* lib_klass = SearchLibraryClasses(interface_names[i], program->GetLibUses());
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
      lib_class_tmp = SearchLibraryClasses(lib_class_tmp->GetParentName(), program->GetLibUses());
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
      LibraryClass* lib_klass = SearchLibraryClasses(interface_names[i], program->GetLibUses());
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
      LibraryClass* lib_klass = SearchLibraryClasses(interface_names[i], program->GetLibUses());
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

  lib_klass = SearchLibraryClasses(cls_name, program->GetLibUses(current_class->GetFileName()));
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
    case BOOLEAN_TYPE:
      type_name = L"System.$Bool";
      break;

    case BYTE_TYPE:
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
      // Function types are encoded "m.(...)~R" everywhere else (method
      // signatures, library signatures, declaration names), but a nameless
      // function type -- a function-typed parameter of a function type -- was
      // named here without the prefix. A call through a variable of type
      // ((Int) ~ Int, Int) ~ Int then compared "(i,)~i" against the argument's
      // "m.(i,)~i" and was reported undefined.
      if(type->GetName().compare(0, 2, L"m.") != 0) {
        encoded_name += L"m.";
      }
      encoded_name += type->GetName();
      break;
    }
  }

  return encoded_name;
}

bool ContextAnalyzer::ResolveClassEnumType(Type* type, Class* context_klass)
{
   // a callee's declarations, checked from its caller, resolve in the callee's
   // bundle. In the caller's, a callee's 'Take(s : Shape)' in bundle B became
   // the 'Shape' of whichever bundle the program's uses listed first.
   OtherClassScope other_scope(skip_use_check, bundle, OtherClassBundle(context_klass));
   // a jagged array's row type, '$String[]': resolve the class it is built on,
   // so '$String[]' and '$System.String[]' are one type (#1246)
   if(IsRowTypeName(type->GetName())) {
     int levels = 0;
     const std::wstring base = RowTypeBase(type->GetName(), levels);
     if(base == L"Bool" || base == L"Byte" || base == L"Char" || base == L"Int" || base == L"Float") {
       type->SetResolved(true);
       return true;
     }
     Type* base_type = TypeFactory::Instance()->MakeType(CLASS_TYPE, base);
     if(!ResolveClassEnumType(base_type, context_klass)) {
       return false;
     }
     type->SetName(RowTypeName(base_type->GetName(), levels));
     type->SetResolved(true);
     return true;
   }

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
      // concreate generic types
      auto generic_types = type->GetGenerics();
      for(auto& generic_type : generic_types) {
         Class* check_klass = nullptr; LibraryClass* check_lib_klass = nullptr;
         if(!GetProgramOrLibraryClass(generic_type, check_klass, check_lib_klass) && !context_klass->GetGenericClass(generic_type->GetName()) && !type->HasGeneric(generic_type)) {
            return false;
         }

         if(!ResolveClassEnumType(generic_type, context_klass)) {
            return false;
         }
      }

      klass->SetCalled(true);
      type->SetName(klass->GetName());
      type->SetResolved(true);
      return true;
   }

   LibraryClass* lib_klass = SearchLibraryClasses(type->GetName(), program->GetLibUses());
   if(lib_klass) {
      // concreate generic types
      auto generic_types = type->GetGenerics();
      for(auto& generic_type : generic_types) {
         Class* check_klass = nullptr; LibraryClass* check_lib_klass = nullptr;
         if(!GetProgramOrLibraryClass(generic_type, check_klass, check_lib_klass) && !context_klass->GetGenericClass(generic_type->GetName()) && !type->HasGeneric(generic_type)) {
            return false;
         }

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
               // keep the parameter's name: the rename is in place, on the
               // declaration a caller later maps to its type argument
               if(type->GetClassGeneric().empty()) {
                  type->SetClassGeneric(type->GetName());
               }
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

   // the class's own enum, or its nearest parent's, before a used one
   const std::wstring scoped_name = SearchScopedEnumName(type->GetName(), context_klass);
   if(!scoped_name.empty()) {
      type->SetName(scoped_name);
      type->SetResolved(true);
      return true;
   }

   Enum* eenum = SearchProgramEnums(type->GetName());
   if(eenum) {
      // the enum's own name, as the class and library branches do: 'Defs.ColumnType'
      // is 'ColumnType', and a class enum found through its class's use,
      // 'Type', is 'System.Time.DatePart#Type'
      type->SetName(eenum->GetName());
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

   LibraryEnum* lib_eenum = SearchLibraryEnums(type->GetName(), program->GetLibUses());
   if(lib_eenum) {
      type->SetName(lib_eenum->GetName());
      type->SetResolved(true);
      return true;
   }
   else {
      lib_eenum = SearchLibraryEnums(type->GetName(), program->GetLibUses());
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
    from_lib_klass = SearchLibraryClasses(from_klass_name, program->GetLibUses());
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
    LibraryClass* to_lib_klass = SearchLibraryClasses(method_type->GetName(), program->GetLibUses());
    if(to_lib_klass) {
      to_klass_name = to_lib_klass->GetName();
    }
  }

  // check enum types
  if(!from_klass && !from_lib_klass) {
    Enum* from_enum = SearchProgramEnums(from_klass_name);
    LibraryEnum* from_lib_enum = SearchLibraryEnums(from_klass_name, program->GetLibUses());

    std::wstring to_enum_name;
    Enum* to_enum = SearchProgramEnums(method_type->GetName());
    if(to_enum) {
      to_enum_name = to_enum->GetName();
    }
    else {
      LibraryEnum* to_lib_enum = SearchLibraryEnums(method_type->GetName(), program->GetLibUses());
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

    // a parenthesised '+' with its own call chain or cast, `("<" + t)->Size() + 1`,
    // is ONE operand; flattening through it dropped the chain
    while(calc_left_expr && calc_left_expr->GetExpressionType() == ADD_EXPR &&
          !calc_left_expr->GetMethodCall() && !calc_left_expr->GetCastType()) {
      concat_exprs.push_front(static_cast<CalculatedExpression*>(calc_left_expr)->GetRight());
      calc_left_expr = static_cast<CalculatedExpression*>(calc_left_expr)->GetLeft();
    }

    if(calc_left_expr) {
      AnalyzeExpression(calc_left_expr, depth + 1);
      Type* calc_left_type = nullptr;
      // `("<" + t)->ToUpper()->Size() + 1`: the left operand is a concatenation
      // whose chain ends in an Int, so this '+' is arithmetic, not a concatenation
      Expression* left_concat = calc_left_expr->GetPreviousExpression();
      if(left_concat && left_concat->GetExpressionType() == STR_CONCAT_EXPR && left_concat->GetMethodCall()) {
        calc_left_type = EffectiveStackType(left_concat);
        calc_left_expr = left_concat;
      }
      else {
        calc_left_type = GetExpressionType(calc_left_expr, depth + 1);
      }
      if(calc_left_type && calc_left_type->GetName() == L"System.String") {
        concat_exprs.push_front(calc_left_expr);

        std::unordered_map<Expression*, Method*> methods_to_string;
        std::unordered_map<Expression*, LibraryMethod*> lib_methods_to_string;

        for(std::list<Expression*>::iterator iter = concat_exprs.begin(); iter != concat_exprs.end(); ++iter) {
          Expression* concat_expr = *iter;
          // The leftmost operand was analyzed above, to learn whether this '+'
          // is a concatenation at all. Analyzing it again is not free: analysis
          // has side effects, and AnalyzeCharacterString appends interpolated
          // segments on every visit, so "{$a}" + b emitted the {$a} append twice
          // and printed AAB (#752). The right operands are visited once, here.
          if(concat_expr != calc_left_expr) {
            AnalyzeExpression(concat_expr, depth + 1);
            // a parenthesised concatenation with its own chain is one operand
            Expression* inner_concat = concat_expr->GetPreviousExpression();
            if(inner_concat && inner_concat->GetExpressionType() == STR_CONCAT_EXPR && inner_concat->GetMethodCall()) {
              concat_expr = inner_concat;
              *iter = concat_expr;
            }
          }

          // what the operand pushes, i.e. the END of a call chain: `x->Abs()->ToString()`
          // is a String though its head is an Int (see EffectiveStackType)
          Type* concat_type = EffectiveStackType(concat_expr);
          if(concat_type) {
            if(concat_type->GetType() == CLASS_TYPE && concat_type->GetName() != L"System.String" && concat_type->GetName() != L"String") {
              const std::wstring cls_name = concat_type->GetName();
              // enum and consts values are Int: no ToString, appended as the integer
              if(concat_type->GetDimension() < 1 && HasProgramOrLibraryEnum(cls_name)) {
                continue;
              }
              Method* method = nullptr;
              LibraryMethod* lib_method = nullptr;
              if(!ResolveConcatToString(cls_name, method, lib_method)) {
                ProcessError(concat_expr, L"Class/enum variable does not have a 'ToString' method");
              }
              else if(method) {
                methods_to_string[concat_expr] = method;
              }
              else if(lib_method) {
                lib_methods_to_string[concat_expr] = lib_method;
              }
              else {
                ProcessError(concat_expr, L"Class/enum variable does not have a public 'ToString' method");
              }
            }
            else if(concat_type->GetType() == FUNC_TYPE) {
              ProcessError(concat_expr, L"Invalid function variable type");
            }
          }
        }

        // create temporary variable for concat of strings and variables
        StringConcat* str_concat = TreeFactory::Instance()->MakeStringConcat(concat_exprs, methods_to_string, lib_methods_to_string);
        Type * type = TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.String");
        // Each concatenation needs its OWN accumulator. EmitStringConcat lowers a
        // concatenation to "allocate a System.String, store it in this slot, then
        // for each part evaluate the part, load the slot and Append". A single slot
        // per method was therefore wrong the moment concatenations could nest: an
        // inner one (a call argument that is itself a concatenation, say) allocates
        // its own String, overwrites the slot the outer one is still accumulating
        // into, and the outer then appends into the inner's object. That silently
        // duplicated values and could drop an operand outright -- issue #746's
        // sibling, #750. Probe for an unused name so every node gets a fresh slot.
        const std::wstring scope_prefix = current_method->GetName() + L":#_add_concat_#";
        const std::wstring scope_name = scope_prefix + std::to_wstring(FirstFreeHiddenIndex(current_table, scope_prefix));

        SymbolEntry* entry = TreeFactory::Instance()->MakeSymbolEntry(scope_name, type, false, true);
        current_table->AddEntry(entry, true);
        str_concat->SetConcat(entry);
        
        return str_concat;
      }
    }
  }

  return nullptr;
}

void ContextAnalyzer::AnalyzeCharacterStringVariable(SymbolEntry* entry, CharacterString* char_str, [[maybe_unused]] int depth)
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
      // enum and consts values are Int: no ToString, appended as the integer
      if(HasProgramOrLibraryEnum(cls_name)) {
        char_str->AddSegment(entry, static_cast<Method*>(nullptr));
        entry->WasLoaded();
        return;
      }
      Method* method = nullptr;
      LibraryMethod* lib_method = nullptr;
      if(!ResolveConcatToString(cls_name, method, lib_method)) {
        ProcessError(char_str, L"Class/enum variable does not have a 'ToString' method");
      }
      else if(method) {
        char_str->AddSegment(entry, method);
        entry->WasLoaded();
      }
      else if(lib_method) {
        char_str->AddSegment(entry, lib_method);
        entry->WasLoaded();
      }
      else {
        ProcessError(char_str, L"Class/enum variable does not have a public 'ToString' method");
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

/****************************
 * Check if token contains expression operators
 * (method call, array access, arithmetic/comparison/logical operators, etc.)
 ****************************/
bool ContextAnalyzer::IsComplexExpression(const std::wstring& token)
{
  // A method call / array access, or any operator expression (arithmetic,
  // comparison, logical). The operator set includes '-' and '>', so a method
  // accessor "->" is covered here too. Identifiers and '@fields' contain none
  // of these characters, so a bare variable like "i" or "@x" stays on the
  // simple-variable path.
  return token.find(L'(') != std::wstring::npos ||
         token.find(L'[') != std::wstring::npos ||
         token.find_first_of(L"+-*/%<>=&|^") != std::wstring::npos;
}

/****************************
 * Whether an interpolation token is a literal: a number (decimal, hex,
 * octal, binary or float), a character or string literal, or true/false
 ****************************/
bool ContextAnalyzer::IsInterpolatedLiteral(const std::wstring& token)
{
  if(token.empty()) {
    return false;
  }

  const wchar_t first = token[0];
  return (first >= L'0' && first <= L'9') || first == L'\'' || first == L'"' ||
    (first == L'\\' && token.size() > 1 && token[1] == L'"') ||
    token == L"true" || token == L"false";
}

/****************************
 * Analyzes a character string expression
 * (for interpolated method calls, array access, etc.)
 ****************************/
void ContextAnalyzer::AnalyzeCharacterStringExpression(const std::wstring& expr_text, CharacterString* char_str, int depth)
{
#ifdef _DEBUG
  Debug(L"expression=|" + expr_text + L"|", char_str->GetLineNumber(), depth + 1);
#endif

  // Unescape quotes in the expression text (\" -> ")
  std::wstring unescaped_text;
  for(size_t i = 0; i < expr_text.size(); ++i) {
    if(expr_text[i] == L'\\' && i + 1 < expr_text.size() && expr_text[i + 1] == L'"') {
      unescaped_text += L'"';
      ++i;  // Skip the backslash, the " will be added
    }
    else {
      unescaped_text += expr_text[i];
    }
  }

  // Parse expression with current class context for function resolution
  std::wstring reason;
  Expression* expr = Parser::ParseExpressionText(unescaped_text, char_str->GetFileName(),
                                                  char_str->GetLineNumber(), char_str->GetLinePosition(), current_class, &reason);
  if(!expr) {
    ProcessError(char_str, L"Invalid expression in string interpolation: '" + expr_text + L"'" +
                 (reason.empty() ? L"" : L"\n\t" + reason));
    return;
  }

  // Analyze the expression
  AnalyzeExpression(expr, depth + 1);

  // Check for errors during analysis. The type is what the expression PUSHES --
  // the end of a call chain: `{$o->Me()->Name()}` is a String though Me()
  // returns an object with no ToString, and was rejected for it.
  Type* eval_type = expr->GetEvalType() ? EffectiveStackType(expr) : nullptr;
  if(!eval_type) {
    ProcessError(char_str, L"Cannot determine type of expression: '" + expr_text + L"'");
    return;
  }

  // Check for Nil return type (compiler error)
  if(eval_type->GetType() == NIL_TYPE) {
    ProcessError(char_str, L"Expression cannot return Nil: '" + expr_text + L"'");
    return;
  }

  // A fully indexed array variable is its element: `{$s[1]}` and `{$m[i,j]}`
  // push a scalar, though a variable's eval type keeps the array's dimension
  // (the rest of the analyzer reads the indices, as AnalyzeRightCast does).
  // FEATURES.md documents array access here; it was rejected outright.
  if(expr->GetExpressionType() == VAR_EXPR && eval_type->GetDimension() > 0) {
    ExpressionList* indices = static_cast<Variable*>(expr)->GetIndices();
    if(indices && (int)indices->GetExpressions().size() == eval_type->GetDimension()) {
      eval_type = TypeFactory::Instance()->MakeType(eval_type);
      eval_type->SetDimension(0);
    }
  }

  // Check for array type (not allowed)
  if(eval_type->GetDimension() > 0) {
    ProcessError(char_str, L"Invalid expression type or dimension size: '" + expr_text + L"'");
    return;
  }

  // Find ToString if not String type
  Method* to_string_method = nullptr;
  LibraryMethod* to_string_lib_method = nullptr;

  // enum and consts values are Int: no ToString, appended as the integer
  if(eval_type->GetType() == CLASS_TYPE &&
     eval_type->GetName() != L"System.String" &&
     eval_type->GetName() != L"String" &&
     !HasProgramOrLibraryEnum(eval_type->GetName())) {
    const std::wstring cls_name = eval_type->GetName();
    Class* klass = SearchProgramClasses(cls_name);
    if(klass) {
      to_string_method = klass->GetMethod(cls_name + L":ToString:");
      if(!to_string_method || to_string_method->GetMethodType() == PRIVATE_METHOD) {
        ProcessError(char_str, L"Expression type does not have public 'ToString' method: '" + expr_text + L"'");
        return;
      }
    }
    else {
      LibraryClass* lib_klass = SearchLibraryClasses(cls_name, program->GetLibUses());
      if(lib_klass) {
        to_string_lib_method = lib_klass->GetMethod(cls_name + L":ToString:");
        if(!to_string_lib_method || to_string_lib_method->GetMethodType() == PRIVATE_METHOD) {
          ProcessError(char_str, L"Expression type does not have public 'ToString' method: '" + expr_text + L"'");
          return;
        }
      }
      else {
        ProcessError(char_str, L"Expression type does not have 'ToString' method: '" + expr_text + L"'");
        return;
      }
    }
  }

  // Add as EXPRESSION segment
  char_str->AddSegment(expr, to_string_method, to_string_lib_method);
}

namespace {
  // Parsed Python/.NET-style interpolation format spec, e.g. ".2", "05", "<10", "x".
  struct FormatSpec {
    bool valid = false;
    bool left_justify = false;   // '<' present ('>' or default is right-justify)
    bool zero_pad = false;       // leading '0' before the width
    bool has_width = false;
    int  width = 0;
    bool has_precision = false;
    int  precision = 0;
    wchar_t radix = 0;           // 'x' hex, 'b' binary, 0 = none
  };

  // Grammar: [ '<' | '>' ] [ '0' ] [ width ] [ '.' precision ]  |  'x'  |  'b'
  FormatSpec ParseFormatSpec(const std::wstring& spec_in) {
    FormatSpec fs;

    const size_t a = spec_in.find_first_not_of(L" \t");
    if(a == std::wstring::npos) {
      return fs;  // empty -> invalid
    }
    const size_t b = spec_in.find_last_not_of(L" \t");
    const std::wstring s = spec_in.substr(a, b - a + 1);

    // pure radix
    if(s == L"x" || s == L"X") { fs.radix = L'x'; fs.valid = true; return fs; }
    if(s == L"b" || s == L"B") { fs.radix = L'b'; fs.valid = true; return fs; }

    size_t i = 0;
    // alignment
    if(s[i] == L'<') { fs.left_justify = true; ++i; }
    else if(s[i] == L'>') { fs.left_justify = false; ++i; }
    // zero-pad flag (only meaningful with a width)
    if(i < s.size() && s[i] == L'0') { fs.zero_pad = true; ++i; }
    // width
    const size_t wstart = i;
    while(i < s.size() && s[i] >= L'0' && s[i] <= L'9') { ++i; }
    // A width or precision past an int is an invalid specifier, reported like
    // any other; std::stoi threw on one and ended obc.
    if(i > wstart) {
      fs.has_width = true;
      if(!objeck_parse::ToInt(s.substr(wstart, i - wstart), fs.width)) {
        return fs;
      }
    }
    // precision
    if(i < s.size() && s[i] == L'.') {
      ++i;
      const size_t pstart = i;
      while(i < s.size() && s[i] >= L'0' && s[i] <= L'9') { ++i; }
      if(i == pstart) { return fs; }   // '.' with no digits -> invalid
      fs.has_precision = true;
      if(!objeck_parse::ToInt(s.substr(pstart, i - pstart), fs.precision)) {
        return fs;
      }
    }

    // the whole spec must be consumed and carry at least a width or precision
    if(i != s.size() || (!fs.has_width && !fs.has_precision)) {
      return fs;
    }

    fs.valid = true;
    return fs;
  }
}

void ContextAnalyzer::AnalyzeInterpolatedToken(const std::wstring& token, CharacterString* char_str, int depth)
{
  std::wstring expr_part, spec_part;
  if(SplitFormatSpec(token, expr_part, spec_part)) {
    AnalyzeCharacterStringFormat(expr_part, spec_part, char_str, depth);
  }
  // A literal -- `{$0x1F}`, `{$42}`, `{$1.5}`, `{$'a'}`, `{$true}` -- is an
  // expression. No name starts with a digit or a quote, and true/false are
  // reserved, so this never takes a variable; it was looked up as one and
  // reported undefined, though `{$255:x}` and `{$-1}` already compiled.
  else if(IsComplexExpression(token) || IsInterpolatedLiteral(token)) {
    AnalyzeCharacterStringExpression(token, char_str, depth);
  }
  else {
    SymbolEntry* entry = GetEntry(token);
    if(!entry) {
      // a variable captured by an enclosing lambda (#865)
      entry = ResolveCapturedVariable(token, char_str, depth);
    }
    if(entry) {
      AnalyzeCharacterStringVariable(entry, char_str, depth);
    }
    else {
      ProcessError(char_str, L"Undefined variable: '" + token + L"'");
    }
  }
}

/****************************
 * Splits an interpolation token into an expression and an optional format
 * specifier at a top-level ':'. A ':' inside parens/brackets/quotes, or one that
 * pairs with a ternary '?', is not a separator. Returns true if a spec was found.
 ****************************/
bool ContextAnalyzer::SplitFormatSpec(const std::wstring& token, std::wstring& expr_out, std::wstring& spec_out)
{
  int paren = 0, bracket = 0, ternary = 0;
  bool in_str = false, in_chr = false;

  for(size_t i = 0; i < token.size(); ++i) {
    const wchar_t c = token[i];

    if(in_str) {
      if(c == L'\\') { ++i; }
      else if(c == L'"') { in_str = false; }
      continue;
    }
    if(in_chr) {
      if(c == L'\\') { ++i; }
      else if(c == L'\'') { in_chr = false; }
      continue;
    }

    switch(c) {
    case L'"':  in_str = true;  break;
    case L'\'': in_chr = true;  break;
    case L'(':  ++paren;        break;
    case L')':  if(paren > 0)   { --paren; }   break;
    case L'[':  ++bracket;      break;
    case L']':  if(bracket > 0) { --bracket; } break;
    case L'?':
      if(paren == 0 && bracket == 0) { ++ternary; }
      break;
    case L':':
      if(paren == 0 && bracket == 0) {
        if(ternary > 0) {
          --ternary;   // ternary colon (a ? b : c), not a format separator
        }
        else {
          expr_out = token.substr(0, i);
          spec_out = token.substr(i + 1);
          return true;
        }
      }
      break;
    default:
      break;
    }
  }

  return false;
}

/****************************
 * Analyzes an interpolation token that carries a format specifier, e.g. "pi:.2".
 * The spec is desugared into a chain of existing library helpers (Float->ToString
 * with precision, Int->ToHexString/ToBinaryString, String->PadTo) producing a
 * System.String, which is emitted as an ordinary EXPRESSION segment.
 ****************************/
void ContextAnalyzer::AnalyzeCharacterStringFormat(const std::wstring& expr_text, const std::wstring& spec_text, CharacterString* char_str, int depth)
{
  const FormatSpec fs = ParseFormatSpec(spec_text);
  if(!fs.valid) {
    ProcessError(char_str, L"Invalid format specifier in string interpolation: '" + spec_text + L"'");
    return;
  }

  // Unescape quotes in the expression text (\" -> ")
  std::wstring expr;
  for(size_t i = 0; i < expr_text.size(); ++i) {
    if(expr_text[i] == L'\\' && i + 1 < expr_text.size() && expr_text[i + 1] == L'"') {
      expr += L'"';
      ++i;
    }
    else {
      expr += expr_text[i];
    }
  }

  // Probe the expression's type to pick the natural stringifier (only needed when
  // no radix/precision is given). The probe AST is discarded.
  Type* probe_type = nullptr;
  {
    std::wstring reason;
    Expression* probe = Parser::ParseExpressionText(expr, char_str->GetFileName(),
                                                    char_str->GetLineNumber(), char_str->GetLinePosition(), current_class, &reason);
    if(!probe) {
      ProcessError(char_str, L"Invalid expression in string interpolation: '" + expr_text + L"'" +
                   (reason.empty() ? L"" : L"\n\t" + reason));
      return;
    }
    AnalyzeExpression(probe, depth + 1);
    probe_type = probe->GetEvalType();
  }
  if(!probe_type) {
    ProcessError(char_str, L"Cannot determine type of expression: '" + expr_text + L"'");
    return;
  }

  // Compose a wrapper expression (as text) over existing library helpers.
  const std::wstring inner = L"(" + expr + L")";
  std::wstring wrapped;

  if(fs.radix == L'x') {
    wrapped = L"Int->ToHexString(" + inner + L"->As(Int))";
  }
  else if(fs.radix == L'b') {
    wrapped = L"Int->ToBinaryString(" + inner + L"->As(Int))";
  }
  else if(fs.has_precision) {
    wrapped = L"Float->ToString(" + inner + L"->As(Float), " + std::to_wstring(fs.precision) + L")";
  }
  else {
    switch(probe_type->GetType()) {
    case FLOAT_TYPE:   wrapped = L"Float->ToString(" + inner + L")"; break;
    case INT_TYPE:     wrapped = L"Int->ToString(" + inner + L")";   break;
    case CHAR_TYPE:    wrapped = L"Char->ToString(" + inner + L")";  break;
    case BYTE_TYPE:    wrapped = L"Byte->ToString(" + inner + L")";  break;
    case BOOLEAN_TYPE: wrapped = L"Bool->ToString(" + inner + L")";  break;
    case CLASS_TYPE:
      if(probe_type->GetName() == L"System.String" || probe_type->GetName() == L"String") {
        wrapped = inner;
      }
      // enum and consts values are Int
      else if(probe_type->GetDimension() < 1 && HasProgramOrLibraryEnum(probe_type->GetName())) {
        wrapped = L"Int->ToString(" + inner + L"->As(Int))";
      }
      else {
        wrapped = inner + L"->ToString()";
      }
      break;
    default:
      wrapped = inner + L"->ToString()";
      break;
    }
  }

  // Apply width / justification / zero-pad to the resulting string.
  if(fs.has_width) {
    const wchar_t pad = fs.zero_pad ? L'0' : L' ';
    // right-justify (default / '>') pads on the left; '<' pads on the right
    const std::wstring is_left = fs.left_justify ? L"false" : L"true";
    wrapped = L"(" + wrapped + L")->PadTo(" + std::to_wstring(fs.width) + L", '" + std::wstring(1, pad) + L"', " + is_left + L")";
  }

  Expression* expr_node = Parser::ParseExpressionText(wrapped, char_str->GetFileName(),
                                                      char_str->GetLineNumber(), char_str->GetLinePosition(), current_class);
  if(!expr_node) {
    ProcessError(char_str, L"Invalid format expression in string interpolation: '" + expr_text + L":" + spec_text + L"'");
    return;
  }
  AnalyzeExpression(expr_node, depth + 1);

  if(!expr_node->GetEvalType()) {
    ProcessError(char_str, L"Cannot apply format specifier '" + spec_text + L"' to expression: '" + expr_text + L"'");
    return;
  }

  // The composed expression yields System.String; no extra ToString needed.
  char_str->AddSegment(expr_node, nullptr, nullptr);
}

void ContextAnalyzer::AnalyzeVariableCast(Type* to_type, Expression* expression)
{
  if(to_type && to_type->GetType() == CLASS_TYPE && expression->GetCastType() && to_type->GetDimension() < 1 &&
     to_type->GetName() != L"System.Base" && to_type->GetName() != L"Base") {
    const std::wstring to_class_name = to_type->GetName();
    if(SearchProgramEnums(to_class_name) ||
       SearchLibraryEnums(to_class_name, program->GetLibUses(current_class->GetFileName()))) {
      return;
    }

    Class* to_class = SearchProgramClasses(to_class_name);
    if(to_class) {
      expression->SetToClass(to_class);
    }
    else {
      LibraryClass* to_lib_class = SearchLibraryClasses(to_class_name, program->GetLibUses());
      if(to_lib_class) {
        expression->SetToLibraryClass(to_lib_class);
      }
      else {
        ProcessError(expression, L"Undefined class: '" + to_class_name + L"'" + BundleSuggestion(to_class_name));
        undefined_classes.insert(to_class_name);
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
      // a callee's declared type, in the callee's scope (see below)
      ResolveClassEnumType(candidate_type, DeclaredTypeContext(candidate_type, klass));
      const std::wstring generic_name = candidate_type->GetName();
      if(klass) {
        concrete_index = klass->GenericIndex(generic_name);
        // a bounded parameter's declaration may already read as its bound, if
        // the callee's class was analyzed first; map it by the parameter's name
        if(concrete_index < 0 && !candidate_type->GetClassGeneric().empty()) {
          concrete_index = klass->GenericIndex(candidate_type->GetClassGeneric());
        }
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
        // a call on the previous call's result: that result's type arguments
        else if(!method_call->GetReceiverGenerics().empty()) {
          concrete_types = method_call->GetReceiverGenerics();
        }
        else if(method_call->GetEvalType()) {
          concrete_types = method_call->GetEvalType()->GetGenerics();
          method_call->SetConcreteTypes(concrete_types);
        }

        // get concrete type
        if(concrete_index < (int)concrete_types.size()) {
          Type* concrete_type = TypeFactory::Instance()->MakeType(concrete_types[concrete_index]);
          concrete_type->SetDimension(candidate_type->GetDimension());
          // the type argument, not a parameter of the callee's class
          concrete_type->SetClassGeneric(L"");
          ResolveClassEnumType(concrete_type);
          return concrete_type;
        }
      }
    }
  }

  // A callee's declared type resolves in the callee's scope. In the caller's,
  // a callee's 'Take(s : Shape)' in bundle B became the 'Shape' of whichever
  // bundle the program's uses listed first, and its 'Level(t : Tint)' missed
  // the callee's own enum.
  if(!candidate_type->IsResolved()) {
    ResolveClassEnumType(candidate_type, DeclaredTypeContext(candidate_type, klass));
  }

  return candidate_type;
}

/****************************
 * A return type's own type arguments mapped through the receiver's: for a
 * Map<String, IntRef>, GetKeys()'s Vector<K> is a Vector<String> (#1182).
 * Arguments that are not the class's parameters are kept, and nested ones
 * (Vector<Pair<K, V>>) are mapped too. Returns 'rtrn' itself if nothing
 * changes.
 ****************************/
Type* ContextAnalyzer::MapReturnGenerics(Type* rtrn, MethodCall* method_call, Class* klass, LibraryClass* lib_klass)
{
  const std::vector<Type*> args = rtrn->GetGenerics();
  std::vector<Type*> mapped;
  bool changed = false;
  for(Type* arg : args) {
    Type* to = arg->HasGenerics() ? MapReturnGenerics(arg, method_call, klass, lib_klass) :
      ResolveGenericType(arg, method_call, klass, lib_klass, false);
    if(to != arg) {
      changed = true;
    }
    mapped.push_back(to);
  }

  if(!changed) {
    return rtrn;
  }

  Type* out = TypeFactory::Instance()->MakeType(rtrn);
  out->SetGenerics(mapped);
  return out;
}

/****************************
 * A parameter's type for matching a call: the class parameter it names
 * resolved, and, for a parameter like `a : Cell<T>` or `v : Vector<T>`, its
 * own type arguments mapped through the receiver's (#1182)
 ****************************/
Type* ContextAnalyzer::ResolveGenericParam(Type* param_type, MethodCall* method_call, Class* klass, LibraryClass* lib_klass, bool is_rtrn)
{
  // map first: resolving Cell<T> as a class reports T as an undefined class
  Type* mapped = param_type;
  if(param_type->HasGenerics() && ((klass && klass->HasGenerics()) || (lib_klass && lib_klass->HasGenerics()))) {
    mapped = MapReturnGenerics(param_type, method_call, klass, lib_klass);
  }

  return ResolveGenericType(mapped, method_call, klass, lib_klass, is_rtrn);
}

Type* ContextAnalyzer::ResolveGenericType(Type* type, Expression* expression, Class* left_klass, LibraryClass* lib_left_klass)
{
  int concrete_index = -1;
  const std::wstring left_type_name = type->GetName();

  if(program->GetEnum(left_type_name) || SearchLibraryEnums(left_type_name, program->GetLibUses())) {
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
  CheckAmbiguousUse(klass_name);
  Class* klass = program->GetClass(klass_name);
  if(!klass) {
    klass = program->GetClass(bundle->GetName() + L"." + klass_name);
    if(!klass) {
      const std::vector<std::wstring>& uses = program->GetLibUses();
      std::wstring qualified; // one buffer for every probe (a miss allocated a string per use)
      for(size_t i = 0; !klass && i < uses.size(); ++i) {
        qualified.assign(uses[i]);
        qualified += L'.';
        qualified += klass_name;
        klass = program->GetClass(qualified);
      }
    }
  }

  return klass;
}

Enum* ContextAnalyzer::SearchProgramEnums(const std::wstring& eenum_name)
{
  CheckAmbiguousUse(eenum_name);
  Enum* eenum = program->GetEnum(eenum_name);
  if(!eenum) {
    eenum = program->GetEnum(bundle->GetName() + L"." + eenum_name);
    if(!eenum) {
      const std::vector<std::wstring>& uses = program->GetLibUses();
      std::wstring qualified; // one buffer for every probe (a miss allocated a string per use)
      for(size_t i = 0; !eenum && i < uses.size(); ++i) {
        qualified.assign(uses[i]);
        qualified += L'.';
        qualified += eenum_name;
        eenum = program->GetEnum(qualified);
      }

      if(!eenum) {
        eenum = SearchBundleClassEnums(eenum_name);
      }
    }
  }

  return eenum;
}

/****************************
 * An enum another class of the current bundle declares, 'Klass#Name', by its
 * short name: Data.JSON's JsonBuilder writes JsonElement's 'JsonType'. The
 * parser made every such class a program-wide use ('Klass#'), so the short
 * name reached the first such enum of ANY class in the program; it is now in
 * scope in its own bundle only, and two classes' enums of the name are
 * ambiguous there.
 ****************************/
Enum* ContextAnalyzer::SearchBundleClassEnums(const std::wstring& eenum_name)
{
  if(!bundle || eenum_name.empty() || eenum_name.find_first_of(L".#") != std::wstring::npos) {
    return nullptr;
  }

  // the class's own, or a parent's, wins
  if(current_class) {
    const std::wstring scoped_name = SearchScopedEnumName(eenum_name, current_class);
    if(!scoped_name.empty() && program->GetEnum(scoped_name)) {
      return program->GetEnum(scoped_name);
    }
  }

  // the bundle's classes and enums are all parsed before analysis, so the matches for a name never change
  std::unordered_map<std::wstring, std::vector<Enum*> >& bundle_cache = bundle_enum_cache[bundle];
  std::unordered_map<std::wstring, std::vector<Enum*> >::iterator cached = bundle_cache.find(eenum_name);
  if(cached == bundle_cache.end()) {
    std::vector<Enum*> bundle_matches;
    const std::vector<Class*> classes = bundle->GetClasses();
    for(size_t i = 0; i < classes.size(); ++i) {
      Enum* eenum = program->GetEnum(classes[i]->GetName() + L'#' + eenum_name);
      if(eenum) {
        bundle_matches.push_back(eenum);
      }
    }
    cached = bundle_cache.insert(std::make_pair(eenum_name, bundle_matches)).first;
  }

  const std::vector<Enum*>& matches = cached->second;
  Enum* found = matches.empty() ? nullptr : matches[0];
  std::vector<std::wstring> candidates;
  if(matches.size() > 1) {
    for(size_t i = 0; i < matches.size(); ++i) {
      Enum* eenum = matches[i];
      candidates.push_back(L"'" + FormatTypeString(eenum->GetName()) + L"' (" + eenum->GetFileName() + L':' + ToString(eenum->GetLineNumber()) + L')');
    }
  }

  ParseNode* site = resolve_site ? resolve_site : (current_method ? static_cast<ParseNode*>(current_method) : static_cast<ParseNode*>(current_class));
  if(candidates.size() > 1 && site && !in_use_check && !skip_use_check) {
    std::wstring listed;
    for(size_t i = 0; i < candidates.size(); ++i) {
      if(i > 0) {
        listed += i + 1 == candidates.size() ? L" and " : L", ";
      }
      listed += candidates[i];
    }
    ProcessError(site, L"Ambiguous name '" + eenum_name + L"': " + listed +
                 L" are each declared by a class of this bundle\n\tQualify the name with its class, 'Klass->" + eenum_name + L"'.");
  }

  return found;
}

/****************************
 * A class's own enum, then its parents', in front of the use-prefix search:
 * the parser adds 'Aa#' as a program-wide use for every class that declares an
 * enum, so SearchProgramEnums("Type") inside Bb found the first such class's
 * enum, Aa#Type, before Bb#Type
 ****************************/
std::wstring ContextAnalyzer::SearchScopedEnumName(const std::wstring& eenum_name, Class* klass)
{
  if(eenum_name.empty() || eenum_name.find(L'#') != std::wstring::npos) {
    return L"";
  }

  // program classes
  LibraryClass* lib_klass = nullptr;
  std::wstring scoped_name; // reused for every probe: this runs for each type name resolved
  for(int guard = 0; klass && guard < 256; ++guard) {
    scoped_name.assign(klass->GetName());
    scoped_name += L'#';
    scoped_name += eenum_name;
    if(program->GetEnum(scoped_name)) {
      return scoped_name;
    }
    lib_klass = klass->GetLibraryParent();
    klass = klass->GetParent();
  }

  // library parents
  const std::vector<std::wstring>& uses = program->GetLibUses();
  for(int guard = 0; lib_klass && guard < 256; ++guard) {
    scoped_name.assign(lib_klass->GetName());
    scoped_name += L'#';
    scoped_name += eenum_name;
    // only an enum of exactly this name counts, so look it up directly: SearchEnumLibraries
    // would also probe "use.Name" for every use of the program, which can never match
    const std::unordered_map<std::wstring, LibraryEnum*>& enum_map = linker->GetAllEnumsMap();
    std::unordered_map<std::wstring, LibraryEnum*>::const_iterator lib_hit = enum_map.find(scoped_name);
    if(lib_hit != enum_map.end() && lib_hit->second && lib_hit->second->GetName() == scoped_name) {
      return scoped_name;
    }

    const std::wstring parent_name = lib_klass->GetParentName();
    LibraryClass* lib_parent = parent_name.empty() ? nullptr : linker->SearchClassLibraries(parent_name, uses);
    lib_klass = lib_parent != lib_klass ? lib_parent : nullptr;
  }

  return L"";
}

/****************************
 * The bundle a program class is declared in
 ****************************/
ParsedBundle* ContextAnalyzer::GetClassBundle(Class* klass)
{
  const std::wstring name = klass->GetName();
  const size_t dot = name.find_last_of(L'.');
  return program->GetBundle(dot == std::wstring::npos ? L"" : name.substr(0, dot));
}

/****************************
 * The distinct classes and enums an unqualified name reaches through the
 * 'use's of a file, each as "'Bundle.Name' (where)", in 'use' order. A bundle
 * listed twice is one candidate. A class's 'Klass#' use (its enums, added for
 * every class that declares one) is not a bundle and is not searched here.
 ****************************/
const std::vector<std::wstring>& ContextAnalyzer::UseCandidates(const std::wstring& name, const std::wstring& file_name)
{
  const std::wstring key = file_name + L'\n' + name;
  std::unordered_map<std::wstring, std::vector<std::wstring>>::iterator cached = use_candidates.find(key);
  if(cached != use_candidates.end()) {
    return cached->second;
  }

  std::vector<std::wstring> candidates;
  std::set<std::wstring> seen;
  const std::unordered_map<std::wstring, LibraryClass*>& lib_classes = linker->GetAllClassesMap();
  const std::unordered_map<std::wstring, LibraryEnum*>& lib_enums = linker->GetAllEnumsMap();
  const std::vector<std::wstring>& uses = program->GetLibUses(file_name);
  for(size_t i = 0; i < uses.size(); ++i) {
    const std::wstring& use = uses[i];
    if(use.empty() || use.back() == L'#') {
      continue;
    }

    const std::wstring full_name = use + L'.' + name;
    if(!seen.insert(full_name).second) {
      continue;
    }

    ParseNode* node = program->GetClass(full_name);
    if(!node) {
      node = program->GetEnum(full_name);
    }

    if(node) {
      candidates.push_back(L"'" + full_name + L"' (" + node->GetFileName() + L':' + ToString(node->GetLineNumber()) + L')');
    }
    else if(lib_classes.find(full_name) != lib_classes.end() || lib_enums.find(full_name) != lib_enums.end()) {
      candidates.push_back(L"'" + full_name + L"' (library)");
    }
  }

  return use_candidates[key] = candidates;
}

/****************************
 * An unqualified class or enum name that two 'use'd bundles both declare is
 * ambiguous. Every search took the first 'use' that declared it, so 'Foo' with
 * 'use A, B' quietly meant A.Foo. A name the current bundle or class declares
 * wins without a check; so does an exact match (a qualified name, or a class
 * in the unnamed bundle).
 ****************************/
void ContextAnalyzer::CheckAmbiguousUse(const std::wstring& name)
{
  if(in_use_check || skip_use_check > 0 || !linker || !program) {
    return;
  }

  // qualified, class-scoped, a row type or a primitive holder: not a short name
  if(name.empty() || name.find_first_of(L".#$|<[ ") != std::wstring::npos) {
    return;
  }

  ParseNode* site = resolve_site;
  if(!site) {
    site = current_method ? static_cast<ParseNode*>(current_method) : static_cast<ParseNode*>(current_class);
  }

  if(!site || site->GetFileName().empty()) {
    return;
  }

  in_use_check = true;
  bool exempt = program->GetClass(name) || program->GetEnum(name) ||
    linker->GetAllClassesMap().count(name) || linker->GetAllEnumsMap().count(name);

  // the current bundle's own
  if(!exempt && bundle && !bundle->GetName().empty()) {
    const std::wstring own_name = bundle->GetName() + L'.' + name;
    exempt = program->GetClass(own_name) || program->GetEnum(own_name);
  }

  // the current method's own type parameter (#1242), which the parser erases
  // where it reads a type; a name of one is never a used bundle's class
  if(!exempt && current_method && current_method->HasGenericName(name)) {
    exempt = true;
  }

  // the current class's own type parameter or enum, or a parent's enum
  if(!exempt && current_class) {
    exempt = (current_class->HasGenerics() && current_class->GetGenericClass(name)) ||
      !SearchScopedEnumName(name, current_class).empty();
  }

  if(!exempt) {
    const std::vector<std::wstring>& candidates = UseCandidates(name, site->GetFileName());
    if(candidates.size() > 1) {
      ProcessError(site, AmbiguousUseMessage(name, candidates));
    }
  }
  in_use_check = false;
}

std::wstring ContextAnalyzer::AmbiguousUseMessage(const std::wstring& name, const std::vector<std::wstring>& candidates)
{
  std::wstring listed;
  for(size_t i = 0; i < candidates.size(); ++i) {
    if(i > 0) {
      listed += i + 1 == candidates.size() ? L" and " : L", ";
    }
    listed += candidates[i];
  }

  return L"Ambiguous name '" + name + L"': " + listed + L" are each in scope through 'use'\n\tQualify the name with its bundle.";
}

/****************************
 * The named bundle whose declaration in 'file_name' spans 'line_num', or null
 ****************************/
ParsedBundle* ContextAnalyzer::GetBundleAt(const std::wstring& file_name, int line_num)
{
  std::vector<ParsedBundle*> bundles = program->GetBundles();
  for(size_t i = 0; i < bundles.size(); ++i) {
    ParsedBundle* candidate = bundles[i];
    if(!candidate->GetName().empty() && candidate->GetFileName() == file_name &&
       candidate->GetLineNumber() <= line_num && line_num <= candidate->GetEndLineNumber()) {
      return candidate;
    }
  }

  return nullptr;
}

/****************************
 * An alias's member types are written in the alias's bundle, so they are
 * resolved there, before the alias is copied to where it is used: in the user's
 * bundle, a 'Shape' the alias's own bundle declares became the user's uses'
 * first 'Shape'. An ambiguous name is reported at the alias.
 ****************************/
void ContextAnalyzer::ResolveAliasTypes(Alias* alias)
{
  const std::wstring name = alias->GetName();
  const size_t dot = name.find_last_of(L'.');
  ParsedBundle* alias_bundle = program->GetBundle(dot == std::wstring::npos ? L"" : name.substr(0, dot));

  PointerScope<ParsedBundle> bundle_scope(bundle, alias_bundle);
  ResolveSiteScope site_scope(resolve_site, alias);
  Class* saved_class = current_class;
  Method* saved_method = current_method;
  current_class = nullptr;
  current_method = nullptr;

  const std::map<const std::wstring, Type*>& types = alias->GetTypes();
  for(std::map<const std::wstring, Type*>::const_iterator iter = types.begin(); iter != types.end(); ++iter) {
    ResolveAliasType(iter->second, alias->GetFileName());
  }

  current_class = saved_class;
  current_method = saved_method;
}

void ContextAnalyzer::ResolveAliasType(Type* type, const std::wstring& file_name)
{
  if(!type) {
    return;
  }

  if(type->GetType() == FUNC_TYPE) {
    const std::vector<Type*> params = type->GetFunctionParameters();
    for(size_t i = 0; i < params.size(); ++i) {
      ResolveAliasType(params[i], file_name);
    }
    ResolveAliasType(type->GetFunctionReturn(), file_name);
  }
  else if(type->GetType() == CLASS_TYPE && !IsRowTypeName(type->GetName())) {
    const std::wstring type_name = type->GetName();
    const std::vector<std::wstring>& uses = program->GetLibUses(file_name);
    if(Class* klass = SearchProgramClasses(type_name)) {
      type->SetName(klass->GetName());
    }
    else if(LibraryClass* lib_klass = SearchLibraryClasses(type_name, uses)) {
      type->SetName(lib_klass->GetName());
    }
    else if(Enum* eenum = SearchProgramEnums(type_name)) {
      type->SetName(eenum->GetName());
    }
    else if(LibraryEnum* lib_eenum = SearchLibraryEnums(type_name, uses)) {
      type->SetName(lib_eenum->GetName());
    }
  }

  const std::vector<Type*> generics = type->GetGenerics();
  for(size_t i = 0; i < generics.size(); ++i) {
    ResolveAliasType(generics[i], file_name);
  }
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

  // an argument is boxed or unboxed to fit: the method every argument fits
  // at least as well as it fits any other, or none
  const int boxed_index = SelectBoxedMatch(valid_matches);
  if(boxed_index == -2) {
    is_ambiguous = true;
    return nullptr;
  }
  else if(boxed_index > -1) {
    method_call->GetCallingParameters()->SetExpressions(valid_matches[boxed_index]->GetCallingParameters());
    return valid_matches[boxed_index]->GetLibraryMethod();
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

  // an argument is boxed or unboxed to fit: the method every argument fits
  // at least as well as it fits any other, or none
  const int boxed_index = SelectBoxedMatch(valid_matches);
  if(boxed_index == -2) {
    is_ambiguous = true;
    return nullptr;
  }
  else if(boxed_index > -1) {
    method_call->GetCallingParameters()->SetExpressions(valid_matches[boxed_index]->GetCallingParameters());
    return valid_matches[boxed_index]->GetMethod();
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
  found_completion.push_back(std::pair<int, std::wstring>(14, L"spawn"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"use"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"leaving"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"assert"));
  found_completion.push_back(std::pair<int, std::wstring>(14, L"using"));
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
  SymbolTable* sym_tbl = method->GetSymbolTable();
  if(!sym_tbl) {
    sym_tbl = symbol_table ? symbol_table->GetSymbolTable(method->GetParsedName()) : nullptr;
  }
  if(!sym_tbl) {
    return false;
  }
  std::vector<SymbolEntry*> entries = sym_tbl->GetEntries();

  std::set<std::wstring> unique_names;

  // an enum's items after its '->', typed prefix or not. This lived inside the
  // instance-variable loop, so it ran once per field (never in a class without
  // one), only with a prefix, and only for a program enum declared in a class.
  if(!var_str.empty()) {
    GetCompletionEnumItems(program, context_klass, var_str, mthd_str, unique_names, found_completion);
  }

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
    SymbolTable* cls_sym_table = context_klass ? context_klass->GetSymbolTable() : nullptr;
    entries = cls_sym_table ? cls_sym_table->GetEntries() : std::vector<SymbolEntry*>();
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
    SymbolTable* sym_tbl_local = method->GetSymbolTable();
    if(!sym_tbl_local && symbol_table) {
      sym_tbl_local = symbol_table->GetSymbolTable(method->GetParsedName());
    }
    std::vector<SymbolEntry*> entries = sym_tbl_local ? sym_tbl_local->GetEntries() : std::vector<SymbolEntry*>();
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
    SymbolTable* cls_sym_table = context_klass ? context_klass->GetSymbolTable() : nullptr;
    entries = cls_sym_table ? cls_sym_table->GetEntries() : std::vector<SymbolEntry*>();
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

/****************************
 * The items of the enum 'var_str' names, for completion after its '->'. The
 * receiver is resolved as AnalyzeEnumCall resolves it: the class's own enum or
 * an ancestor's, then a program enum, then a library one. An enum declared in
 * some other class ('Number->Format->', where only 'Format' reaches here) is
 * taken when exactly one candidate fits, rather than mixing several enums'
 * items. Items are CompletionItemKind.EnumMember (20).
 ****************************/
void ContextAnalyzer::GetCompletionEnumItems(ParsedProgram* program, Class* context_klass, const std::wstring& var_str, const std::wstring& mthd_str,
                                             std::set<std::wstring>& unique_names, std::vector<std::pair<int, std::wstring> >& found_completion)
{
  // a literal receiver, or a qualified one the caller already cut down
  if(var_str.empty() || !(iswalpha(var_str[0]) || var_str[0] == L'_')) {
    return;
  }

  const std::wstring file_name = context_klass ? context_klass->GetFileName() : L"";
  const std::vector<std::wstring>& uses = program->GetLibUses(file_name);
  const std::wstring nested_suffix = L'#' + var_str;

  Enum* eenum = nullptr;
  LibraryEnum* lib_eenum = nullptr;

  // the class's own enum, or an ancestor's (program or library)
  const std::wstring scoped_name = SearchScopedEnumName(var_str, context_klass);
  if(!scoped_name.empty()) {
    eenum = program->GetEnum(scoped_name);
    if(!eenum && linker) {
      lib_eenum = linker->SearchEnumLibraries(scoped_name, uses);
    }
  }

  // a program enum at bundle level: unqualified, in this class's bundle, or a used one
  if(!eenum && !lib_eenum) {
    eenum = program->GetEnum(var_str);
    if(!eenum && context_klass) {
      eenum = program->GetEnum(context_klass->GetBundleName() + L'.' + var_str);
    }
    for(size_t i = 0; !eenum && i < uses.size(); ++i) {
      eenum = program->GetEnum(uses[i] + L'.' + var_str);
    }
  }

  // a program enum declared in another class
  if(!eenum && !lib_eenum) {
    Enum* candidate = nullptr; bool ambiguous = false;
    const std::vector<ParsedBundle*> bundles = program->GetBundles();
    for(size_t i = 0; i < bundles.size(); ++i) {
      const std::vector<Enum*> eenums = bundles[i]->GetEnums();
      for(size_t j = 0; j < eenums.size(); ++j) {
        if(EndsWith(eenums[j]->GetName(), nested_suffix)) {
          ambiguous = ambiguous || (candidate && candidate != eenums[j]);
          candidate = eenums[j];
        }
      }
    }
    if(!ambiguous) {
      eenum = candidate;
    }
  }

  // a library enum at bundle level, by its full name or through a 'use'
  if(!eenum && !lib_eenum && linker) {
    lib_eenum = linker->SearchEnumLibraries(var_str, uses);
  }

  // a library enum declared in a class whose bundle is used
  if(!eenum && !lib_eenum && linker) {
    LibraryEnum* candidate = nullptr; bool ambiguous = false;
    const std::unordered_map<std::wstring, LibraryEnum*>& enum_map = linker->GetAllEnumsMap();
    for(std::unordered_map<std::wstring, LibraryEnum*>::const_iterator iter = enum_map.begin(); iter != enum_map.end(); ++iter) {
      const std::wstring& name = iter->first;
      if(!EndsWith(name, nested_suffix)) {
        continue;
      }

      // 'Bundle.Klass#Name': the bundle is what precedes the class's last '.'
      const std::wstring klass_name = name.substr(0, name.size() - nested_suffix.size());
      const size_t dot = klass_name.find_last_of(L'.');
      const std::wstring bundle_name = dot == std::wstring::npos ? L"" : klass_name.substr(0, dot);
      if(std::find(uses.begin(), uses.end(), bundle_name) != uses.end()) {
        ambiguous = ambiguous || (candidate && candidate != iter->second);
        candidate = iter->second;
      }
    }
    if(!ambiguous) {
      lib_eenum = candidate;
    }
  }

  std::vector<std::wstring> item_names;
  if(eenum) {
    std::map<const std::wstring, EnumItem*> items = eenum->GetItems();
    for(std::map<const std::wstring, EnumItem*>::iterator iter = items.begin(); iter != items.end(); ++iter) {
      item_names.push_back(iter->first);
    }
  }
  else if(lib_eenum) {
    std::map<const std::wstring, LibraryEnumItem*> items = lib_eenum->GetItems();
    for(std::map<const std::wstring, LibraryEnumItem*>::iterator iter = items.begin(); iter != items.end(); ++iter) {
      item_names.push_back(iter->first);
    }
  }

  for(size_t i = 0; i < item_names.size(); ++i) {
    const std::wstring& item_name = item_names[i];
    if(item_name.rfind(mthd_str, 0) == 0 && unique_names.find(item_name) == unique_names.end()) {
      unique_names.insert(item_name);
      found_completion.push_back(std::pair<int, std::wstring>(20, item_name));
    }
  }
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
    SymbolTable* sym_tbl_local = method->GetSymbolTable();
    if(!sym_tbl_local && symbol_table) {
      sym_tbl_local = symbol_table->GetSymbolTable(method->GetParsedName());
    }
    std::vector<SymbolEntry*> entries = sym_tbl_local ? sym_tbl_local->GetEntries() : std::vector<SymbolEntry*>();
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
    SymbolTable* cls_sym_table = context_klass ? context_klass->GetSymbolTable() : nullptr;
    entries = cls_sym_table ? cls_sym_table->GetEntries() : std::vector<SymbolEntry*>();
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
    lib_klass = SearchLibraryClasses(BOOL_CLASS_ID, program->GetLibUses(context_klass->GetFileName()));
    break;

  case BYTE_TYPE:
    lib_klass = SearchLibraryClasses(BYTE_CLASS_ID, program->GetLibUses(context_klass->GetFileName()));

    break;

  case CHAR_TYPE:
    lib_klass = SearchLibraryClasses(CHAR_CLASS_ID, program->GetLibUses(context_klass->GetFileName()));
    break;

  case INT_TYPE:
    lib_klass = SearchLibraryClasses(INT_CLASS_ID, program->GetLibUses(context_klass->GetFileName()));
    break;

  case FLOAT_TYPE:
    lib_klass = SearchLibraryClasses(FLOAT_CLASS_ID, program->GetLibUses(context_klass->GetFileName()));
    break;

  case CLASS_TYPE:
    klass = SearchProgramClasses(type->GetName());
    if(!klass) {
      lib_klass = SearchLibraryClasses(type->GetName(), program->GetLibUses(context_klass->GetFileName()));
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
    // Look up the symbol entry — try method's own table, then the
    // analyzer's SymbolTableManager, then fall back to the expression's entry
    SymbolEntry* found_entry = nullptr;
    SymbolTable* sym_table = method->GetSymbolTable();
    if(!sym_table && symbol_table) {
      sym_table = symbol_table->GetSymbolTable(method->GetParsedName());
    }
    if(sym_table) {
      const std::wstring entry_name = method->GetName() + L':' + found_name;
      found_entry = sym_table->GetEntry(entry_name);
    }
    // Fall back to the expression's own entry
    if(!found_entry && found_expression) {
      if(found_expression->GetExpressionType() == METHOD_CALL_EXPR) {
        found_entry = static_cast<MethodCall*>(found_expression)->GetEntry();
      }
      else if(found_expression->GetExpressionType() == VAR_EXPR) {
        found_entry = static_cast<Variable*>(found_expression)->GetEntry();
      }
    }

    // found variable — look up its type's class/enum definition.
    if(found_entry && found_entry->GetType()) {
      const std::wstring search_name = found_entry->GetType()->GetName();
      // found class
      klass = SearchProgramClasses(search_name);
      if(klass && klass->GetLineNumber() > 0) {
        return true;
      }
      else {
        // found enum
        eenum = SearchProgramEnums(search_name);
        if(eenum && eenum->GetLineNumber() > 0) {
          return true;
        }
      }
    }
    // found enum
    else if(found_expression && found_expression->GetExpressionType() == METHOD_CALL_EXPR && static_cast<MethodCall*>(found_expression)->GetEnumItem()) {
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
          // Constructor / static call with cursor on the class token (e.g. `Counter`
          // in `Counter->New()`): match the variable token to the class via the
          // resolved Method, so go-to-definition jumps to the class declaration.
          if(mthd_call->GetVariableName() == found_name && mthd_call->GetMethod() &&
             mthd_call->GetMethod()->GetClass()) {
            klass = mthd_call->GetMethod()->GetClass();
            if(klass->GetLineNumber() > 0) {
              return true;
            }
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
      SymbolTable* cls_table = method->GetClass() ? method->GetClass()->GetSymbolTable() : nullptr;
      if(cls_table) {
        const std::wstring class_entry_name = method->GetClass()->GetName() + L':' + found_name;
        found_entry = cls_table->GetEntry(class_entry_name);
        if(found_entry) {
          return true;
        }
      }

      SymbolTable* mthd_table = method->GetSymbolTable();
      if(!mthd_table && symbol_table) {
        mthd_table = symbol_table->GetSymbolTable(method->GetParsedName());
      }
      if(mthd_table) {
        const std::wstring method_entry_name = method->GetName() + L':' + found_name;
        found_entry = mthd_table->GetEntry(method_entry_name);
        if(found_entry) {
          return true;
        }
      }
      // Fall back to expression's entry
      if(!found_entry && found_expression) {
        if(found_expression->GetExpressionType() == METHOD_CALL_EXPR) {
          found_entry = static_cast<MethodCall*>(found_expression)->GetEntry();
        }
        else if(found_expression->GetExpressionType() == VAR_EXPR) {
          found_entry = static_cast<Variable*>(found_expression)->GetEntry();
        }
        if(found_entry) {
          return true;
        }
      }
      // Constructor / static-style call (e.g. `Counter->New()`): no entry/variable,
      // but the resolved Method points back to the class. Caller renders via the class.
      if(!found_entry && found_expression && found_expression->GetExpressionType() == METHOD_CALL_EXPR) {
        MethodCall* mc = static_cast<MethodCall*>(found_expression);
        if(!mc->GetEntry() && !mc->GetVariable() && mc->GetMethod() && mc->GetMethod()->GetClass()) {
          return true;
        }
      }
    }
  }

  // Declaration-position fallback: LocateExpression omits declaration LHS
  // (e.g. `counter` in `counter := ...`) to avoid synthetic Variable nodes.
  // Match directly against the SymbolEntry's own declaration position.
  auto match_entry_decl = [&](SymbolEntry* entry) -> bool {
    if(!entry || entry->GetLineNumber() != line_num + 1) {
      return false;
    }
    const std::wstring entry_full_name = entry->GetName();
    const size_t sep = entry_full_name.find_last_of(L':');
    const std::wstring bare_name = (sep != std::wstring::npos) ? entry_full_name.substr(sep + 1) : entry_full_name;
    const int start_pos = entry->GetLinePosition() - 1;
    const int end_pos = start_pos + (int)bare_name.size();
    if(start_pos <= line_pos && line_pos <= end_pos) {
      found_entry = entry;
      found_name = bare_name;
      found_line = entry->GetLineNumber();
      found_start_pos = start_pos;
      found_end_pos = end_pos;
      return true;
    }
    return false;
  };

  std::vector<SymbolEntry*> local_entries = symbol_table->GetEntries(method->GetParsedName());
  for(size_t i = 0; i < local_entries.size(); ++i) {
    if(match_entry_decl(local_entries[i])) {
      return true;
    }
  }
  if(method->GetClass()) {
    std::vector<SymbolEntry*> class_entries = symbol_table->GetEntries(method->GetClass()->GetName());
    for(size_t i = 0; i < class_entries.size(); ++i) {
      if(match_entry_decl(class_entries[i])) {
        return true;
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
      SymbolTable* cls_table = method->GetClass() ? method->GetClass()->GetSymbolTable() : nullptr;
      SymbolEntry* found_entry = nullptr;
      if(cls_table) {
        const std::wstring class_entry_name = method->GetClass()->GetName() + L':' + found_name;
        found_entry = cls_table->GetEntry(class_entry_name);
      }
      if(found_entry && found_entry->GetLineNumber() > 0 && found_entry->GetLinePosition() > 0) {
        found_name = found_entry->GetName();
        size_t var_name_pos = found_name.find_last_of(L':');
        if(var_name_pos != std::wstring::npos) {
          found_name = found_name.substr(var_name_pos + 1, found_name.size() - var_name_pos - 1);
        }
        // Return 1-based positions; the caller (diag_find_declaration)
        // converts to 0-based with `- 1`.
        found_line = found_entry->GetLineNumber();
        found_start_pos = found_end_pos = found_entry->GetLinePosition();
        found_end_pos += (int)found_name.size();

        return true;
      }
      else {
        SymbolTable* mthd_table = method->GetSymbolTable();
        if(!mthd_table && symbol_table) {
          mthd_table = symbol_table->GetSymbolTable(method->GetParsedName());
        }
        const std::wstring method_entry_name = method->GetName() + L':' + found_name;
        found_entry = mthd_table ? mthd_table->GetEntry(method_entry_name) : nullptr;
        if(found_entry && found_entry->GetLineNumber() > 0 && found_entry->GetLinePosition() > 0) {
          found_name = found_entry->GetName();
          size_t var_name_pos = found_name.find_last_of(L':');
          if(var_name_pos != std::wstring::npos) {
            found_name = found_name.substr(var_name_pos + 1, found_name.size() - var_name_pos - 1);
          }
          found_line = found_entry->GetLineNumber();
          found_start_pos = found_end_pos = found_entry->GetLinePosition();
          found_end_pos += (int)found_name.size();

          return true;
        }
      }

      // Fallback for inferred-type locals: find the earliest matching
      // expression as the declaration site
      {
        int best_line = INT_MAX;
        int best_start = 0, best_end = 0;
        bool found = false;

        for(size_t i = 0; i < all_expressions.size(); ++i) {
          Expression* expr = all_expressions[i];
          if(!expr || expr->GetLineNumber() <= 0 || expr->GetLinePosition() <= 0) {
            continue;
          }

          bool matches = false;
          if(expr->GetExpressionType() == METHOD_CALL_EXPR) {
            MethodCall* mc = static_cast<MethodCall*>(expr);
            matches = (mc->GetVariableName() == found_name);
          }
          else if(expr->GetExpressionType() == VAR_EXPR) {
            Variable* var = static_cast<Variable*>(expr);
            matches = (var->GetName() == found_name);
          }

          if(matches && expr->GetLineNumber() < best_line) {
            best_line = expr->GetLineNumber();
            best_start = expr->GetLinePosition();
            best_end = best_start + (int)found_name.size();
            found = true;
          }
        }

        if(found) {
          found_line = best_line;
          found_start_pos = best_start;
          found_end_pos = best_end;
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

  // local entries — add variable references (not declarations, to avoid
  // temporary Variable allocations that accumulate across requests)
  std::vector<SymbolEntry*> local_entries = symbol_table->GetEntries(method->GetParsedName());
  for(size_t i = 0; i < local_entries.size(); ++i) {
    SymbolEntry* local_entry = local_entries[i];

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
          alt_start_pos = alt_end_pos = method_call->GetMidLinePosition() - 1;
          alt_end_pos += (int)alt_found_name.size();
        }
        else if(method_call->GetMethod()) {
          // Variable/class token is primary, method name is alt — matches branch 1.
          // For constructor / static-style calls (e.g. `Counter->New()`) the variable
          // token holds the class name; the method name lives at mid_line_pos.
          found_name = method_call->GetVariableName();
          end_pos += (int)found_name.size();

          alt_found_name = method_call->GetMethodName();
          alt_start_pos = alt_end_pos = method_call->GetMidLinePosition() - 1;
          alt_end_pos += (int)alt_found_name.size();
        }
        else if(method_call->GetCallType() == ENUM_CALL) {
          found_name = method_call->GetVariableName();
          end_pos += (int)found_name.size();

          alt_found_name = method_call->GetMethodName();
          end_pos = method_call->GetEndLinePosition() - 1;
        }
        else {
          found_name = L"@self";
          end_pos += (int)found_name.size();

          alt_found_name = method_call->GetMethodName();
          alt_start_pos = alt_end_pos = method_call->GetMidLinePosition() - 1;
          alt_end_pos += (int)alt_found_name.size();
        }
      }
        break;

      default:
        break;
      }

      // '?->' and '??' desugar to synthesized Try()/Otherwise() calls that have
      // no method-name token in the source: their mid position aliases the
      // receiver variable (or is unset). Suppress the alt range so the receiver
      // token resolves to the variable instead of a phantom method; when the
      // synthesized call has no receiver token of its own either (empty
      // variable slot / '@self'), suppress its primary range too so the real
      // receiver Variable node and chained calls get to match instead.
      if(expression->GetExpressionType() == METHOD_CALL_EXPR) {
        MethodCall* head_call = static_cast<MethodCall*>(expression);
        const int mid_pos = head_call->GetMidLinePosition();
        if(mid_pos <= 0 || mid_pos == expression->GetLinePosition()) {
          alt_start_pos = alt_end_pos = -1;
          if(head_call->GetVariableName().empty() || found_name == L"@self") {
            start_pos = end_pos = -1;
          }
        }
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

      // not on the head token: check chained calls (a->b()->c(), and the real
      // call behind a synthesized Try()/Otherwise() from '?->'/'??'). The
      // method-name position is the mid when recorded; chains built by the
      // identifier-context parser path leave mid at -1 and carry the method
      // token's position as their own line position instead.
      if(expression->GetExpressionType() == METHOD_CALL_EXPR) {
        for(MethodCall* chained = expression->GetMethodCall(); chained; chained = chained->GetMethodCall()) {
          const int chained_mid = chained->GetMidLinePosition() > 0
            ? chained->GetMidLinePosition()
            : static_cast<Expression*>(chained)->GetLinePosition();
          if(chained_mid > 0 && !chained->GetMethodName().empty() &&
             static_cast<Expression*>(chained)->GetLineNumber() == line_num + 1) {
            const int chained_start = chained_mid - 1;
            const int chained_end = chained_start + (int)chained->GetMethodName().size();
            if(chained_start <= line_pos && chained_end >= line_pos) {
              found_name = chained->GetMethodName();
              is_alt = true;
              found_expression = chained;
              return true;
            }
          }
        }
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
          alt_start_pos = alt_end_pos = method_call->GetMidLinePosition() - 1;
          alt_end_pos += (int)alt_found_name.size();
        }
        else {
          found_name = L"@self";
          end_pos += (int)found_name.size();

          alt_found_name = method_call->GetMethodName();
          alt_start_pos = alt_end_pos = method_call->GetMidLinePosition() - 1;
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
