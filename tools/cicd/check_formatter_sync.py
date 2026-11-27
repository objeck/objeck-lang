#!/usr/bin/env python3
"""Keeps System.Utility.CodeFormatter (core/compiler/lib_src/misc.obs) the same code as the language
server's formatter (tools/lsp/server/format_code/formatter.obs and scanner.obs).

The library's formatter began as a copy of the language server's and the two were then changed apart:
each had fixes the other lacked, and the library's had gone on writing 'parent' for 'Parent', losing
blank lines, and stopping on input the other had learned to read. The language server's files are
the source. This writes the library's three classes (CodeFormatter, Scanner, Token) from them, between
the two marker lines in misc.obs, with what a library needs added: the bundle's indentation, the
qualified name of the token enum, the two helper classes made private, API doc comments, and the
library-only Format(source, is_color).

  python3 tools/cicd/check_formatter_sync.py           exit 1 if misc.obs is not what the sources give
  python3 tools/cicd/check_formatter_sync.py --write   rewrite the region in misc.obs

After --write, rebuild misc.obl (core/compiler/build_libs.sh) and commit both.
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
FORMATTER = os.path.join(ROOT, 'tools', 'lsp', 'server', 'format_code', 'formatter.obs')
SCANNER = os.path.join(ROOT, 'tools', 'lsp', 'server', 'format_code', 'scanner.obs')
MISC = os.path.join(ROOT, 'core', 'compiler', 'lib_src', 'misc.obs')
BEGIN = '\t# formatter:begin (written by tools/cicd/check_formatter_sync.py from tools/lsp/server/format_code; change it there)'
END = '\t# formatter:end'
NL = chr(10)
TAB = chr(9)

CLASS_DOC = '''#~
Code formatter. Formats Objeck source text: indentation, one statement to a line, spacing around
operators. The text it returns has the same tokens as the text it was given, so it compiles to the
same program; formatting its own output changes nothing.
```
options := Map->New()<String, String>;
options->Insert("ident-space", "2");
options->Insert("trim-trailing", "true");
formatted := System.Utility.CodeFormatter->New(options)->Format(source, false);
```
~#'''

# doc comments for the public members, keyed by the declaration they go before
DOCS = {
    'New(options : Map<String, String>) {': '''#~
Constructor
@param options options

<p>Support for following options:<p>
<ul>
  <li>"function-space" - (true|false)</li>
  <li>"ident-space" - (number|0 for tabs)</li>
  <li>"trim-trailing" - (true|false)</li>
  <li>"start-line" - (optional|number)</li>
  <li>"end-line" - (optional|number)</li>
</ul>
```
opts := Map->New()<String, String>;
opts->Insert("ident-space", "2");
opts->Insert("trim-trailing", "true");
formatter := CodeFormatter->New(opts);
```
~#''',
    'method : public : Format(source : String) ~ String {': '''#~
Formats source code
@param source source code
@return formatted code
```
opts := Map->New()<String, String>;
opts->Insert("ident-space", "3");
formatter := CodeFormatter->New(opts);
formatter->Format("class Foo{method:public:Bar()~Nil{x:=1;}}")->PrintLine();
```
~#''',
    'method : public : LineDepths(source : String, line_count : Int) ~ Int[] {': '''#~
The nesting depth each line of the source is indented to
@param source source code
@param line_count number of lines in the source
@return depth of each line, index 1 is the first line; -1 for a line a multi-line comment runs over
~#''',
    'method : public : IndentFor(depth : Int) ~ String {': '''#~
The whitespace written before a line at a nesting depth
@param depth nesting depth
@return indentation
~#''',
}

# the part only the library has: Format with ANSI colors
LIBRARY_ONLY = '''
#~
Formats source code, optionally with ANSI colors for a terminal
@param source source code
@param is_color true for ascii color formatting, false otherwise
@return formatted code
```
opts := Map->New()<String, String>;
opts->Insert("ident-space", "3");
formatter := CodeFormatter->New(opts);
src := "class Foo{method:public:Bar()~Nil{x:=1;}}";
formatter->Format(src, false)->PrintLine();
```
~#
method : public : Format(source : String, is_color : Bool) ~ String {
	formatted := Format(source);
	if(is_color & formatted <> Nil) {
		return Colorize(formatted);
	};

	return formatted;
}

# The formatted text with a color around each token, for a terminal. It is read
# again here, after formatting, so that the colors are never part of what the
# layout rules measure and trim.
method : Colorize(formatted : String) ~ String {
	scanner := Scanner->New(formatted);
	scanner->Scan();
	tokens := scanner->GetTokens();

	colored := "";
	at := 0;
	each(token in tokens) {
		start := token->GetStart();
		end := token->GetEnd();
		if(start >= at & end > start & end <= formatted->Size()) {
			colored += formatted->SubString(at, start - at);

			color := "";
			select(token->GetType()) {
				label System.Utility.Token->Type->LINE_COMMENT
				label System.Utility.Token->Type->TRAIL_COMMENT
				label System.Utility.Token->Type->MULTI_COMMENT
				label System.Utility.Token->Type->INLINE_COMMENT {
					color := "\\e[90m";
				}

				label System.Utility.Token->Type->STRING_LIT
				label System.Utility.Token->Type->CHAR_LIT
				label System.Utility.Token->Type->NUM_LIT
				label System.Utility.Token->Type->TRUE_ID
				label System.Utility.Token->Type->FALSE_ID
				label System.Utility.Token->Type->NIL_ID {
					color := "\\e[36m";
				}

				label System.Utility.Token->Type->STRING_ID
				label System.Utility.Token->Type->INT_ID
				label System.Utility.Token->Type->FLOAT_ID
				label System.Utility.Token->Type->CHAR_ID
				label System.Utility.Token->Type->BOOL_ID
				label System.Utility.Token->Type->BYTE_ID {
					color := "\\e[94m";
				}

				label System.Utility.Token->Type->OPREN
				label System.Utility.Token->Type->CPREN
				label System.Utility.Token->Type->OBRACE
				label System.Utility.Token->Type->CBRACE
				label System.Utility.Token->Type->OCBRACE
				label System.Utility.Token->Type->CCBRACE {
					color := "\\e[31m";
				}

				label System.Utility.Token->Type->IDENT
				label System.Utility.Token->Type->VTAB
				label System.Utility.Token->Type->SEMI
				label System.Utility.Token->Type->COMMA {
				}

				other {
					# a keyword is a word; everything else left is an operator
					if(formatted->Get(start)->IsChar()) {
						color := "\\e[33m";
					}
					else {
						color := "\\e[32m";
					};
				}
			};

			if(color->Size() > 0) {
				colored += color;
				colored += formatted->SubString(start, end - start);
				colored += "\\e[0m";
			}
			else {
				colored += formatted->SubString(start, end - start);
			};
			at := end;
		};
	};
	colored += formatted->SubString(at, formatted->Size() - at);

	return colored;
}
'''


def indent(text, by):
    return NL.join((TAB * by + line) if line.strip() else '' for line in text.split(NL))


def class_body(text, header):
    """the text of a top-level class, from its header line to the '}' that closes it at column 0"""
    start = text.index(header)
    end = text.index(NL + '}', start) + 2
    return text[start:end]


def generate():
    formatter = open(FORMATTER, encoding='utf-8', newline='').read().replace(chr(13), '')
    scanner = open(SCANNER, encoding='utf-8', newline='').read().replace(chr(13), '')

    code = class_body(formatter, 'class CodeFormatter {')
    lines = code.split(NL)
    out = []
    for line in lines:
        key = line.strip()
        if key in DOCS:
            lead = line[:len(line) - len(line.lstrip())]
            out += [(lead + d) if d.strip() else '' for d in DOCS[key].split(NL)]
        out.append(line)
    assert sum(1 for l in lines if l.strip() in DOCS) == len(DOCS), 'a documented member was renamed in formatter.obs'
    # the library-only members go before the class's closing brace
    assert out[-1] == '}'
    out = out[:-1] + indent(LIBRARY_ONLY.strip(NL), 1).split(NL) + ['}']
    formatter_class = CLASS_DOC + NL + NL.join(out)

    scanner_class = class_body(scanner, 'class Scanner {').replace('class Scanner {', 'class : private : Scanner {', 1)
    token_class = class_body(scanner, 'class Token {').replace('class Token {', 'class : private : Token {', 1)

    body = formatter_class + NL + NL + scanner_class + NL + NL + token_class
    # inside the bundle the enum is named in full, as the rest of the library does
    body = re.sub(r'(?<![A-Za-z0-9_.])Token->Type->', 'System.Utility.Token->Type->', body)
    return BEGIN + NL + indent(body, 1) + NL + END


def main():
    want = generate()
    text = open(MISC, encoding='utf-8', newline='').read()
    assert chr(13) not in text
    if BEGIN in text:
        start = text.index(BEGIN)
        end = text.index(END, start) + len(END)
    else:
        # first time: the three classes run from the formatter's doc comment to the end of the bundle
        marker = TAB + '#~' + NL + TAB + 'Code formatter' + NL + TAB + '~#' + NL + TAB + 'class CodeFormatter {'
        start = text.index(marker)
        end = text.rindex(NL + '}')
    have = text[start:end]
    if have == want:
        print('misc.obs: System.Utility.CodeFormatter is the language server\'s formatter')
        return 0
    if '--write' in sys.argv[1:]:
        open(MISC, 'w', encoding='utf-8', newline='').write(text[:start] + want + text[end:])
        print('misc.obs: rewritten; rebuild misc.obl (core/compiler/build_libs.sh) and commit both')
        return 0
    print('misc.obs: System.Utility.CodeFormatter differs from tools/lsp/server/format_code')
    print('  the library\'s formatter is generated from the language server\'s: make the change in')
    print('  formatter.obs or scanner.obs, then run  python3 tools/cicd/check_formatter_sync.py --write')
    hl = have.split(NL); wl = want.split(NL)
    for k in range(min(len(hl), len(wl))):
        if hl[k] != wl[k]:
            print('  first difference at line %d of the region:' % (k + 1))
            print('    misc.obs:  ' + hl[k].strip()[:120])
            print('    generated: ' + wl[k].strip()[:120])
            break
    return 1


if __name__ == '__main__':
    sys.exit(main())
