/***************************************************************************
 * Unit test for the library type-string parser (TypeParser in types.h).
 *
 * Build and run (from core/compiler, after `make` has built the objects):
 *   make types_test && ./types_test
 *
 * TypeParser reads the encoded type strings a library carries: "i", "o.Name",
 * "o.Name<o.Arg>*", "m.(i,f,)~o.Name". The linker calls it on strings read
 * out of .obl files, so a string that is cut short or not a type at all must
 * come back as "not a type" (null, or fewer parameters) and never as a throw
 * or a read past the end: nothing in the compiler catches the first, and a
 * sanitizer build (-fsanitize=address,undefined -D_GLIBCXX_ASSERTIONS) is how
 * to look for the second.
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved. See types.h for the license.
 ***************************************************************************/

#include "types.h"
#include <iostream>
#include <string>

using namespace frontend;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { ++checks; if(!(cond)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #cond << std::endl; } } while(0)

// well-formed strings parse to what they say
static void TestWellFormed()
{
  Type* t = TypeParser::ParseType(L"i");
  CHECK(t && t->GetType() == INT_TYPE && t->GetDimension() == 0);

  t = TypeParser::ParseType(L"f*");
  CHECK(t && t->GetType() == FLOAT_TYPE && t->GetDimension() == 1);

  t = TypeParser::ParseType(L"o.System.String");
  CHECK(t && t->GetType() == CLASS_TYPE && t->GetName() == L"System.String");

  t = TypeParser::ParseType(L"o.Collection.Vector<o.System.IntRef>**");
  CHECK(t && t->GetType() == CLASS_TYPE && t->GetName() == L"Collection.Vector");
  CHECK(t && t->GetGenerics().size() == 1 && t->GetDimension() == 2);

  t = TypeParser::ParseType(L"m.(i,f,)~o.System.String");
  CHECK(t && t->GetType() == FUNC_TYPE && t->GetFunctionParameterCount() == 2);
  CHECK(t && t->GetFunctionReturn() && t->GetFunctionReturn()->GetType() == CLASS_TYPE);

  // a function type whose parameter is a function type
  t = TypeParser::ParseType(L"m.(m.(i,)~i,i,)~i");
  CHECK(t && t->GetType() == FUNC_TYPE && t->GetFunctionParameterCount() == 2);

  std::vector<Type*> params = TypeParser::ParseParameters(L"i,o.System.String,f*,");
  CHECK(params.size() == 3);
  CHECK(params.size() == 3 && params[1]->GetName() == L"System.String" && params[2]->GetDimension() == 1);

  params = TypeParser::ParseParameters(L"o.Collection.Vector<o.System.IntRef>*,i,");
  CHECK(params.size() == 2);
  CHECK(params.size() == 2 && params[0]->GetDimension() == 1 && params[0]->GetGenerics().size() == 1);
}

// Every prefix of a well-formed string, and a few strings that are not types:
// each call must return. What it returns for a prefix is not pinned (some
// prefixes are types themselves); that it returns is the test.
static void TestCutShort()
{
  const std::wstring whole[] = {
    L"o.System.String",
    L"o.Collection.Vector<o.System.IntRef>**",
    L"m.(i,f,)~o.System.String",
    L"m.(m.(i,)~i,i,)~i",
    L"m.(o.Collection.Map<o.System.String,o.System.IntRef>,)~n",
    L"i,o.System.String,f*,m.(i,)~i,o.Collection.Vector<o.System.IntRef>*,",
  };

  for(const std::wstring& text : whole) {
    for(size_t length = 0; length <= text.size(); ++length) {
      const std::wstring cut = text.substr(0, length);
      TypeParser::ParseType(cut);
      TypeParser::ParseParameters(cut);
      ++checks;
    }
  }

  // the two that threw std::out_of_range: a class tag with nothing after it
  CHECK(TypeParser::ParseType(L"o") == nullptr);
  CHECK(TypeParser::ParseParameters(L"o").size() <= 1);
  CHECK(TypeParser::ParseParameters(L"i,o").size() <= 2);

  // an empty class name is still a class type, as it has always been
  Type* t = TypeParser::ParseType(L"o.");
  CHECK(t && t->GetType() == CLASS_TYPE && t->GetName().empty());

  // a function type with fewer '~' than "m.(": the scan for '~' ends at the
  // end of the string (the index-may-reach-size() case a static analyser flagged)
  t = TypeParser::ParseType(L"m.(i,)");
  CHECK(t && t->GetType() == FUNC_TYPE);
  t = TypeParser::ParseType(L"m");
  CHECK(t && t->GetType() == FUNC_TYPE);
  t = TypeParser::ParseType(L"m.(m.(i,)~i");
  CHECK(t && t->GetType() == FUNC_TYPE);

  // not a type at all
  CHECK(TypeParser::ParseType(L"") == nullptr);
  CHECK(TypeParser::ParseType(L"x") == nullptr);
  CHECK(TypeParser::ParseType(L"<") == nullptr);
  CHECK(TypeParser::ParseParameters(L"").empty());
  CHECK(TypeParser::ParseParameters(L",,,").empty());
  TypeParser::ParseParameters(L"<<<>>>");
  TypeParser::ParseParameters(L"o.A<o.B<o.C");
  TypeParser::ParseParameters(L"m.(((");
  TypeParser::ParseParameters(L"m.()))~");
  checks += 4;
}

int main()
{
  TestWellFormed();
  TestCutShort();

  std::cout << "types_test: " << checks << " checks, " << failures << " failures" << std::endl;
  return failures == 0 ? 0 : 1;
}
