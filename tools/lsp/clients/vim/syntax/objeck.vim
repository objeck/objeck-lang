" Vim syntax file
" Language: Objeck
" Maintainer: Randy Hollines
" Latest Revision: 2026-04-06

if exists("b:current_syntax")
  finish
endif

" Comments
syn match objeckLineComment '#.*$'
syn region objeckBlockComment start='#\~' end='\~#'

" Strings and characters
syn region objeckString start='"' skip='\\"' end='"'
syn match objeckChar "'[^'\\]'"
syn match objeckChar "'\\[nrtfabe0\\']'"
syn match objeckChar "'\\x[0-9a-fA-F]\{2\}'"
syn match objeckChar "'\\u[0-9a-fA-F]\{1,4\}'"

" Numbers
syn match objeckNumber '\<\d\+\>'
syn match objeckNumber '\<0[xX][0-9a-fA-F]\+\>'
syn match objeckNumber '\<0[bB][01]\+\>'
syn match objeckFloat '\<\d\+\.\d*\([eE][+-]\?\d\+\)\?\>'
syn match objeckFloat '\<\.\d\+\([eE][+-]\?\d\+\)\?\>'

" Control flow keywords
syn keyword objeckControl if else do while for each reverse select other
syn keyword objeckControl break continue return label in critical

" Declaration keywords
syn keyword objeckDecl class interface enum consts bundle alias
syn keyword objeckDecl method function native virtual static
syn keyword objeckDecl public private

" Type keywords
syn keyword objeckType Nil Int IntRef Float FloatRef Char CharRef
syn keyword objeckType Byte ByteRef Bool BoolRef String
syn keyword objeckType BaseArrayRef BoolArrayRef ByteArrayRef
syn keyword objeckType CharArrayRef FloatArrayRef IntArrayRef StringArrayRef
syn keyword objeckType Func2Ref Func3Ref Func4Ref FuncRef

" Other keywords
syn keyword objeckKeyword use leaving Parent As TypeOf from implements
syn keyword objeckKeyword Try Otherwise New

" Constants
syn keyword objeckConstant true false Nil

" Operators
syn match objeckOperator ':='
syn match objeckOperator '+='
syn match objeckOperator '-='
syn match objeckOperator '\*='
syn match objeckOperator '/='
syn match objeckOperator '->'
syn keyword objeckOperator and or not xor

" Highlight links
hi def link objeckLineComment Comment
hi def link objeckBlockComment Comment
hi def link objeckString String
hi def link objeckChar Character
hi def link objeckNumber Number
hi def link objeckFloat Float
hi def link objeckControl Statement
hi def link objeckDecl Structure
hi def link objeckType Type
hi def link objeckKeyword Keyword
hi def link objeckConstant Constant
hi def link objeckOperator Operator

let b:current_syntax = "objeck"
