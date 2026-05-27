" Vim syntax file for Objeck
" Language: Objeck
" URL: https://github.com/objeck/objeck-lsp

if exists("b:current_syntax")
  finish
endif

" Comments
syn match objeckLineComment "#.*$" contains=@Spell
syn region objeckBlockComment start="#\~" end="\~#" contains=@Spell

" Strings and characters
syn region objeckString start=/"/ skip=/\\./ end=/"/
syn match objeckChar "b\?'[^'\\]'"
syn match objeckChar "b\?'\\[^']*'"

" Numbers
syn match objeckNumber "\<0[xX][0-9a-fA-F]\+\>"
syn match objeckNumber "\<0[bB][01]\+\>"
syn match objeckNumber "\<[0-9]\+\(\.[0-9]*\)\?\([eE][+-]\?[0-9]\+\)\?\>"

" Constants
syn keyword objeckBoolean true false
syn keyword objeckNil Nil

" Types
syn keyword objeckType Byte ByteRef Int IntRef Float FloatRef
syn keyword objeckType Char CharRef Bool BoolRef String
syn keyword objeckType BaseArrayRef BoolArrayRef ByteArrayRef CharArrayRef
syn keyword objeckType FloatArrayRef IntArrayRef StringArrayRef
syn keyword objeckType Func2Ref Func3Ref Func4Ref FuncRef

" Declarations
syn keyword objeckDeclaration class method function interface enum alias consts bundle

" Keywords
syn keyword objeckKeyword use leaving if else do while select break continue
syn keyword objeckKeyword in other for each reverse label return critical
syn keyword objeckKeyword from implements virtual

" Builtins
syn keyword objeckBuiltin New Parent Try Otherwise

" Operators
syn keyword objeckOperator As TypeOf and or xor not
syn match objeckOperator ":="
syn match objeckOperator "->"
syn match objeckOperator "[+\-*/%=<>]"
syn match objeckOperator "<>"
syn match objeckOperator "<="
syn match objeckOperator ">="

" Modifiers
syn keyword objeckModifier public private static native

" Instance variables
syn match objeckVariable "@[a-zA-Z_][a-zA-Z0-9_]*"

" Highlighting
hi def link objeckLineComment Comment
hi def link objeckBlockComment Comment
hi def link objeckString String
hi def link objeckChar Character
hi def link objeckNumber Number
hi def link objeckBoolean Boolean
hi def link objeckNil Constant
hi def link objeckType Type
hi def link objeckDeclaration Keyword
hi def link objeckKeyword Keyword
hi def link objeckBuiltin Function
hi def link objeckOperator Operator
hi def link objeckModifier StorageClass
hi def link objeckVariable Identifier

let b:current_syntax = "objeck"
