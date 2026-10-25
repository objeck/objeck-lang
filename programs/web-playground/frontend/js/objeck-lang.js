// Objeck language support for Monaco: syntax highlighting and editor themes.
// Loaded before app.js; exposes window.ObjeckLang.register(monaco).

(function () {
    'use strict';

    const KEYWORDS = [
        'and', 'not', 'or', 'xor',
        'virtual', 'if', 'else', 'do', 'while', 'break', 'continue',
        'use', 'bundle', 'native', 'static', 'public', 'private',
        'class', 'record', 'interface', 'alias', 'implements',
        'function', 'method',
        'select', 'other', 'otherwise',
        'enum', 'consts',
        'for', 'each', 'in', 'reverse',
        'label', 'return', 'leaving',
        'from', 'critical', 'readonly',
        'try', 'on', 'finally', 'ref',
    ];

    function register(monaco) {
        monaco.languages.register({ id: 'objeck' });

        monaco.languages.setMonarchTokensProvider('objeck', {
            keywords: KEYWORDS,
            typeKeywords: ['Byte', 'Int', 'Float', 'Char', 'Bool', 'Nil', 'String'],
            builtinKeywords: ['New', 'As', 'TypeOf', 'Parent', 'true', 'false'],
            operators: [
                '->', '=>', ':=', '+=', '-=', '*=', '/=',
                '+', '-', '*', '/', '%',
                '=', '<>', '<', '>', '<=', '>=',
                '&', '|', '~',
            ],
            symbols: /[=><!~?:&|+\-*/^%]+/,
            escapes: /\\(?:[abfnrtv\\"']|x[0-9A-Fa-f]{1,4}|u[0-9A-Fa-f]{4})/,

            tokenizer: {
                root: [
                    [/#~/, 'comment', '@blockComment'],
                    [/#.*$/, 'comment'],
                    [/@[a-zA-Z_]\w*/, 'variable.predefined'],
                    // a capitalised name before -> or . is a class; the rest are plain identifiers
                    [/[A-Z]\w*(?=\s*(?:->|\.|<))/, {
                        cases: {
                            '@typeKeywords': 'type',
                            '@builtinKeywords': 'keyword.other',
                            '@default': 'type.identifier',
                        }
                    }],
                    [/[a-zA-Z_]\w*/, {
                        cases: {
                            '@keywords': 'keyword',
                            '@typeKeywords': 'type',
                            '@builtinKeywords': 'keyword.other',
                            '@default': 'identifier',
                        }
                    }],
                    { include: '@whitespace' },
                    [/"/, 'string', '@string'],
                    [/'[^\\']'/, 'string.char'],
                    [/'\\.'/, 'string.char'],
                    [/0[xX][0-9a-fA-F]+/, 'number.hex'],
                    [/\d+\.\d*([eE][-+]?\d+)?/, 'number.float'],
                    [/\d+/, 'number'],
                    [/[{}()[\]]/, '@brackets'],
                    [/[;,.]/, 'delimiter'],
                    [/@symbols/, {
                        cases: {
                            '@operators': 'operator',
                            '@default': '',
                        }
                    }],
                ],
                blockComment: [
                    [/[^~#]+/, 'comment'],
                    [/~#/, 'comment', '@pop'],
                    [/[~#]/, 'comment'],
                ],
                string: [
                    [/[^\\"{]+/, 'string'],
                    [/\{\$/, 'string.escape', '@interpolation'],
                    [/\{/, 'string'],
                    [/@escapes/, 'string.escape'],
                    [/\\./, 'string.escape.invalid'],
                    [/"/, 'string', '@pop'],
                ],
                interpolation: [
                    [/[^}]+/, 'variable'],
                    [/}/, 'string.escape', '@pop'],
                ],
                whitespace: [
                    [/[ \t\r\n]+/, 'white'],
                ],
            },
        });

        monaco.languages.setLanguageConfiguration('objeck', {
            comments: { lineComment: '#', blockComment: ['#~', '~#'] },
            brackets: [['{', '}'], ['[', ']'], ['(', ')']],
            autoClosingPairs: [
                { open: '{', close: '}' },
                { open: '[', close: ']' },
                { open: '(', close: ')' },
                { open: '"', close: '"', notIn: ['string'] },
            ],
            surroundingPairs: [
                { open: '{', close: '}' },
                { open: '[', close: ']' },
                { open: '(', close: ')' },
                { open: '"', close: '"' },
            ],
            indentationRules: {
                increaseIndentPattern: /\{\s*$/,
                decreaseIndentPattern: /^\s*}/,
            },
        });

        // Colours match the page tokens in playground.css (--editor-bg and friends).
        monaco.editor.defineTheme('objeck-dark', {
            base: 'vs-dark', inherit: true,
            rules: [
                { token: 'keyword', foreground: 'C4B5FD' },
                { token: 'keyword.other', foreground: '7DD3FC' },
                { token: 'type', foreground: '5EEAD4' },
                { token: 'type.identifier', foreground: '5EEAD4' },
                { token: 'string', foreground: 'FDBA74' },
                { token: 'string.escape', foreground: 'FCD34D' },
                { token: 'string.char', foreground: 'FDBA74' },
                { token: 'number', foreground: 'A7F3D0' },
                { token: 'number.hex', foreground: 'A7F3D0' },
                { token: 'number.float', foreground: 'A7F3D0' },
                { token: 'comment', foreground: '6B7488', fontStyle: 'italic' },
                { token: 'variable.predefined', foreground: '93C5FD' },
                { token: 'variable', foreground: 'FDE68A' },
                { token: 'operator', foreground: 'AEB6C8' },
                { token: 'delimiter', foreground: '8B93A7' },
            ],
            colors: {
                'editor.background': '#12141b',
                'editor.foreground': '#E6E8EE',
                'editorLineNumber.foreground': '#4B5266',
                'editorLineNumber.activeForeground': '#AEB6C8',
                'editor.selectionBackground': '#33407a99',
                'editor.lineHighlightBackground': '#1a1d27',
                'editorCursor.foreground': '#818CF8',
                'editorIndentGuide.background1': '#222633',
                'editorWidget.background': '#161922',
            },
        });

        monaco.editor.defineTheme('objeck-light', {
            base: 'vs', inherit: true,
            rules: [
                { token: 'keyword', foreground: '6D28D9' },
                { token: 'keyword.other', foreground: '0369A1' },
                { token: 'type', foreground: '0F766E' },
                { token: 'type.identifier', foreground: '0F766E' },
                { token: 'string', foreground: 'B45309' },
                { token: 'string.escape', foreground: 'A16207' },
                { token: 'string.char', foreground: 'B45309' },
                { token: 'number', foreground: '047857' },
                { token: 'number.hex', foreground: '047857' },
                { token: 'number.float', foreground: '047857' },
                { token: 'comment', foreground: '8A93A5', fontStyle: 'italic' },
                { token: 'variable.predefined', foreground: '1D4ED8' },
                { token: 'variable', foreground: '92400E' },
                { token: 'operator', foreground: '475467' },
                { token: 'delimiter', foreground: '667085' },
            ],
            colors: {
                'editor.background': '#ffffff',
                'editor.foreground': '#1B1F27',
                'editorLineNumber.foreground': '#B4BBC9',
                'editorLineNumber.activeForeground': '#475467',
                'editor.selectionBackground': '#c7d2fe99',
                'editor.lineHighlightBackground': '#f6f7fb',
                'editorCursor.foreground': '#4F46E5',
            },
        });
    }

    window.ObjeckLang = { register: register, KEYWORDS: KEYWORDS };
})();
