/***************************************************************************
 * Language parser.
 *
 * Copyright (c) 2008-2019, Randy Hollines
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

#ifndef __PARSER_H__
#define __PARSER_H__

#include "scanner.h"

using namespace frontend;

#define SECOND_INDEX 1
#define THIRD_INDEX 2
#define DEFAULT_BUNDLE_NAME L"Default"

/****************************
 * Parsers source files.
 ****************************/
class Parser {
	bool alt_syntax;
	ParsedProgram* program;
	ParsedBundle* current_bundle;
	Class* current_class;
	Method* current_method;
	Method* prev_method;
	Scanner* scanner;
	SymbolTableManager* symbol_table;
	map<ScannerTokenType, wstring> error_msgs;
	map<int, wstring> errors;
	wstring src_path;
	wstring run_prgm;
	unsigned int anonymous_class_id;

	inline void NextToken() {
		scanner->NextToken();
	}

	inline bool Match(ScannerTokenType type, int index = 0) {
		return scanner->GetToken(index)->GetType() == type;
	}

	inline bool IsBasicType(ScannerTokenType type) {
		switch(GetToken()) {
		case TOKEN_BOOLEAN_ID:
		case TOKEN_BYTE_ID:
		case TOKEN_INT_ID:
		case TOKEN_FLOAT_ID:
		case TOKEN_CHAR_ID:
			return true;

		default:
			break;
		}

		return false;
	}

	inline ScannerTokenType GetToken(int index = 0) {
		return scanner->GetToken(index)->GetType();
	}

	inline int GetLineNumber() {
		return scanner->GetToken()->GetLineNumber();
	}

	inline const wstring GetFileName() {
		return scanner->GetToken()->GetFileName();
	}

	inline const wstring GetScopeName(const wstring& ident) {
		wstring scope_name;
		if(current_method) {
			scope_name = current_method->GetName() + L":" + ident;
		}
		else if(current_class) {
			scope_name = current_class->GetName() + L":" + ident;
		}
		else {
			scope_name = ident;
		}

		return scope_name;
	}

	inline const wstring GetEnumScopeName(const wstring& ident) {
		wstring scope_name;
		if(current_class) {
			scope_name = current_class->GetName() + L"#" + ident;
		}
		else {
			scope_name = ident;
		}

		return scope_name;
	}

	void Debug(const wstring& msg, int depth) {
		GetLogger() << setw(4) << GetLineNumber() << L": ";
		for(int i = 0; i < depth; ++i) {
			GetLogger() << L"  ";
		}
		GetLogger() << msg << endl;
	}

	inline wstring ToString(int v) {
		wostringstream str;
		str << v;
		return str.str();
	}

	inline wstring ParseBundleName() {
		wstring name;
		if(Match(TOKEN_IDENT)) {
			while(Match(TOKEN_IDENT) && !Match(TOKEN_END_OF_STREAM)) {
				name += scanner->GetToken()->GetIdentifier();
				NextToken();
				if(Match(TOKEN_PERIOD)) {
					name += L'.';
					NextToken();
				}
				else if(Match(TOKEN_IDENT)) {
					ProcessError(L"Expected period", TOKEN_SEMI_COLON);
					NextToken();
				}
			}
		}
		else {
			ProcessError(TOKEN_IDENT);
		}

		return name;
	}

	vector<Type*> ParseGenericTypes() {
		vector<Type*> generic_types;

		if(Match(TOKEN_LES)) {
			NextToken();

			while(!Match(TOKEN_GTR) && !Match(TOKEN_END_OF_STREAM)) {
				if(!Match(TOKEN_IDENT)) {
					ProcessError(TOKEN_IDENT);
				}
				// identifier
				const wstring generic_name = scanner->GetToken()->GetIdentifier();
				generic_types.push_back(TypeFactory::Instance()->MakeType(CLASS_TYPE, generic_name));
				NextToken();

				if(Match(TOKEN_COMMA) && !Match(TOKEN_GTR, SECOND_INDEX)) {
					NextToken();
				}
				else if(!Match(TOKEN_GTR)) {
					ProcessError(L"Expected ',' or '>'");
				}
			}

			NextToken();
		}

		return generic_types;
	}

	vector<Class*> ParseGenericClasses(const wstring& bundle_name, const int line_num, const wstring& file_name) {
		vector<Class*> generic_classes;

		if(Match(TOKEN_LES)) {
			NextToken();

			while(!Match(TOKEN_GTR) && !Match(TOKEN_END_OF_STREAM)) {
				if(!Match(TOKEN_IDENT)) {
					ProcessError(TOKEN_IDENT);
				}

				// identifier
				wstring generic_name = scanner->GetToken()->GetIdentifier();
				for(size_t i = 0; i < generic_classes.size(); ++i) {
					if(bundle_name.size() > 0) {
						generic_name.insert(0, L".");
						generic_name.insert(0, bundle_name);
					}
				}
				generic_classes.push_back(TreeFactory::Instance()->MakeClass(file_name, line_num, generic_name, true));
				NextToken();

				if(Match(TOKEN_COMMA) && !Match(TOKEN_GTR, SECOND_INDEX)) {
					NextToken();
				}
				else if(!Match(TOKEN_GTR)) {
					ProcessError(L"Expected ',' or '>'");
				}
			}

			NextToken();
		}

		return generic_classes;
	}

	Declaration* AddDeclaration(const wstring& ident, Type* type, bool is_static, Declaration* child,
															const int line_num, const wstring& file_name, int depth) {
		// add entry
		wstring scope_name = GetScopeName(ident);
		SymbolEntry* entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num,
																																	scope_name, type, is_static,
																																	current_method != NULL);

#ifdef _DEBUG
		Debug(L"Adding variable: '" + scope_name + L"'", depth + 2);
#endif

		bool was_added = symbol_table->CurrentParseScope()->AddEntry(entry);
		if(!was_added) {
			ProcessError(L"Variable already defined in this scope: '" + ident + L"'");
		}

		Declaration* declaration;
		if(Match(TOKEN_ASSIGN)) {
			Variable* variable = ParseVariable(ident, depth + 1);
			// FYI: can not specify array indices here
			declaration = TreeFactory::Instance()->MakeDeclaration(file_name, line_num, entry, child,
																														 ParseAssignment(variable, depth + 1));
		}
		else {
			declaration = TreeFactory::Instance()->MakeDeclaration(file_name, line_num, entry, child);
		}

		return declaration;
	}

	// error processing
	void LoadErrorCodes();
	void ProcessError(const ScannerTokenType type);
	void ProcessError(const wstring & msg);
	void ProcessError(const wstring & msg, ParseNode * node);
	void ProcessError(const wstring & msg, const ScannerTokenType sync, int offset = 0);
	bool CheckErrors();

	// parsing operations
	void ParseFile(const wstring & file_name);
	void ParseProgram();
	void ParseBundle(int depth);
	wstring ParseBundleName(int depth);
	Class * ParseClass(const wstring & bundle_id, int depth);
	Class * ParseInterface(const wstring & bundle_id, int depth);
	Method * ParseMethod(bool is_function, bool virtual_required, int depth);
	Variable * ParseVariable(const wstring & ident, int depth);
	MethodCall * ParseMethodCall(int depth);
	MethodCall * ParseMethodCall(const wstring & ident, int depth);
	void ParseMethodCall(Expression * expression, int depth);
	MethodCall * ParseMethodCall(Variable * variable, int depth);
	void ParseAnonymousClass(MethodCall * method_call, int depth);
	StatementList * ParseStatementList(int depth);
	Statement * ParseStatement(int depth, bool semi_colon = true);
	Assignment * ParseAssignment(Variable * variable, int depth);
	StaticArray * ParseStaticArray(int depth);
	If * ParseIf(int depth);
	DoWhile * ParseDoWhile(int depth);
	While * ParseWhile(int depth);
	Select * ParseSelect(int depth);
	Enum * ParseEnum(int depth);
	Enum * ParseConsts(int depth);
	For * ParseFor(int depth);
	For * ParseEach(int depth);
	CriticalSection * ParseCritical(int depth);
	Return * ParseReturn(int depth);
	Leaving * ParseLeaving(int depth);
	Declaration * ParseDeclaration(const wstring & name, bool is_stmt, int depth);
	DeclarationList * ParseDecelerationList(int depth);
	ExpressionList * ParseExpressionList(int depth, ScannerTokenType open = TOKEN_OPEN_PAREN,
																			 ScannerTokenType close = TOKEN_CLOSED_PAREN);
	ExpressionList * ParseIndices(int depth);
	void ParseCastTypeOf(Expression * expression, int depth);
	Type * ParseType(int depth);
	Expression * ParseExpression(int depth);
	Expression * ParseLogic(int depth);
	Expression * ParseMathLogic(int depth);
	Expression * ParseTerm(int depth);
	Expression * ParseFactor(int depth);
	Expression * ParseSimpleExpression(int depth);

public:
	Parser(const wstring & p, bool a, const wstring & r) {
		src_path = p;
		alt_syntax = a;
		run_prgm = r;
		program = new ParsedProgram;
		LoadErrorCodes();
		current_class = NULL;
		current_method = prev_method = NULL;
		anonymous_class_id = 0;
	}

	~Parser() {
	}

	bool Parse();

	ParsedProgram* GetProgram() {
		return program;
	}

	SymbolTableManager* GetSymbolTable() {
		return symbol_table;
	}
};

#endif