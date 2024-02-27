-- Copyright 2006-2023 Mitchell. See LICENSE.
-- C# LPeg lexer.

local lexer = require('lexer')
local token, word_match = lexer.token, lexer.word_match
local P, S = lpeg.P, lpeg.S

local lex = lexer.new('objeck')

-- Whitespace.
lex:add_rule('whitespace', token(lexer.WHITESPACE, lexer.space^1))

-- Keywords.
lex:add_rule('keyword', token(lexer.KEYWORD, word_match{
  'class', 'method', 'function', 'public', 'abstract', 'private', 'static', 'native', 'virtual',
  'Parent', 'As', 'from', 'implements', 'interface', 'enum', 'alias', 'consts', 'bundle',
  'use', 'in', 'leaving', 'if', 'else', 'do', 'while', 'select', 'break', 'continue', 'other',
  'for', 'not', 'each', 'reverse', 'label', 'return', 'critical', 'New', 'and', 'or', 'xor',
  'true', 'false'--, 'Nil'
}))

-- Types.
lex:add_rule('type', token(lexer.TYPE, word_match{
  'Nil', 'Byte', 'ByteRef', 'Int', 'IntRef', 'Float', 'FloatRef', 'Char', 'CharRef',
  'Bool', 'BoolRef', 'String', 'BaseArrayRef', 'BoolArrayRef', 'ByteArrayRef',
  'CharArrayRef', 'FloatArrayRef', 'IntArrayRef', 'StringArrayRef',
  'Func2Ref', 'Func3Ref', 'Func4Ref', 'FuncRef'
}))

-- Identifiers.
lex:add_rule('identifier', token(lexer.IDENTIFIER, lexer.word))

-- Class variables
local class_var = '@' * lexer.word
lex:add_rule('variable', lex:tag(lexer.VARIABLE, class_var))

-- Comments.
local line_comment = lexer.to_eol('#', true)
local block_comment = lexer.range('#~', '~#')
lex:add_rule('comment', token(lexer.COMMENT, block_comment + line_comment))

-- Strings.
local sq_str = lexer.range("'", true)
local dq_str = lexer.range('"', true)
local ml_str = lexer.range('"', false, false)
lex:add_rule('string', token(lexer.STRING, sq_str + ml_str + dq_str))

-- Numbers.
lex:add_rule('number', token(lexer.NUMBER, lexer.number))

-- Operators.
lex:add_rule('operator', token(lexer.OPERATOR, S('~!.,:;+-*/<>=\\^|&%?()[]{}')))

-- Fold points.
lex:add_fold_point(lexer.OPERATOR, '{', '}')
lex:add_fold_point(lexer.COMMENT, '#~', '~#')

lexer.property['scintillua.comment'] = '#'

return lex
