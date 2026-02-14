// Objeck language definition for Monaco Editor
// Keywords sourced from core/compiler/scanner.cpp

export function registerObjeckLanguage(monaco) {
    monaco.languages.register({ id: 'objeck' });

    monaco.languages.setMonarchTokensProvider('objeck', {
        keywords: [
            'and', 'not', 'or', 'xor',
            'virtual', 'if', 'else', 'do', 'while', 'break', 'continue',
            'use', 'bundle', 'native', 'static', 'public', 'private',
            'class', 'interface', 'alias', 'implements',
            'function', 'method',
            'select', 'other', 'otherwise',
            'enum', 'consts',
            'for', 'each', 'in', 'reverse',
            'label', 'return', 'leaving',
            'from', 'critical',
        ],

        typeKeywords: [
            'Byte', 'Int', 'Float', 'Char', 'Bool', 'Nil', 'String',
        ],

        builtinKeywords: [
            'New', 'As', 'TypeOf', 'Parent', 'Try', 'Otherwise', 'true', 'false',
        ],

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
                // Block comments: #~ ... ~#
                [/#~/, 'comment', '@blockComment'],

                // Line comments: # ...
                [/#.*$/, 'comment'],

                // Instance variables: @identifier
                [/@[a-zA-Z_]\w*/, 'variable.predefined'],

                // Identifiers and keywords
                [/[a-zA-Z_]\w*/, {
                    cases: {
                        '@keywords': 'keyword',
                        '@typeKeywords': 'type',
                        '@builtinKeywords': 'keyword.other',
                        '@default': 'identifier',
                    }
                }],

                { include: '@whitespace' },

                // Strings
                [/"/, 'string', '@string'],

                // Characters
                [/'[^\\']'/, 'string.char'],
                [/'\\.'/, 'string.char'],

                // Numbers
                [/0[xX][0-9a-fA-F]+/, 'number.hex'],
                [/\d+\.\d*([eE][-+]?\d+)?/, 'number.float'],
                [/\d+/, 'number'],

                // Operators
                [/->/, 'operator'],
                [/=>/, 'operator'],
                [/:=/, 'operator'],
                [/<>/, 'operator'],
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
                [/[^\\"${}]+/, 'string'],
                [/\{\$/, 'string.escape', '@interpolation'],
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
        comments: {
            lineComment: '#',
            blockComment: ['#~', '~#'],
        },
        brackets: [
            ['{', '}'],
            ['[', ']'],
            ['(', ')'],
        ],
        autoClosingPairs: [
            { open: '{', close: '}' },
            { open: '[', close: ']' },
            { open: '(', close: ')' },
            { open: '"', close: '"' },
            { open: "'", close: "'" },
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
}

export function defineObjeckThemes(monaco) {
    monaco.editor.defineTheme('objeck-dark', {
        base: 'vs-dark',
        inherit: true,
        rules: [
            { token: 'keyword', foreground: '569CD6', fontStyle: 'bold' },
            { token: 'keyword.other', foreground: '4FC1FF' },
            { token: 'type', foreground: '4EC9B0' },
            { token: 'string', foreground: 'CE9178' },
            { token: 'string.escape', foreground: 'D7BA7D' },
            { token: 'string.char', foreground: 'CE9178' },
            { token: 'number', foreground: 'B5CEA8' },
            { token: 'number.hex', foreground: 'B5CEA8' },
            { token: 'number.float', foreground: 'B5CEA8' },
            { token: 'comment', foreground: '6A9955' },
            { token: 'variable.predefined', foreground: '9CDCFE' },
            { token: 'variable', foreground: 'DCDCAA' },
            { token: 'operator', foreground: 'D4D4D4' },
            { token: 'identifier', foreground: 'D4D4D4' },
        ],
        colors: {
            'editor.background': '#1E1E2E',
            'editor.foreground': '#CDD6F4',
            'editorLineNumber.foreground': '#6C7086',
            'editorLineNumber.activeForeground': '#CDD6F4',
            'editor.selectionBackground': '#45475A',
            'editor.lineHighlightBackground': '#2B2B3B',
        },
    });

    monaco.editor.defineTheme('objeck-light', {
        base: 'vs',
        inherit: true,
        rules: [
            { token: 'keyword', foreground: '0000FF', fontStyle: 'bold' },
            { token: 'keyword.other', foreground: '0070C1' },
            { token: 'type', foreground: '267F99' },
            { token: 'string', foreground: 'A31515' },
            { token: 'string.escape', foreground: 'EE0000' },
            { token: 'string.char', foreground: 'A31515' },
            { token: 'number', foreground: '098658' },
            { token: 'comment', foreground: '008000' },
            { token: 'variable.predefined', foreground: '001080' },
            { token: 'variable', foreground: '795E26' },
            { token: 'operator', foreground: '000000' },
        ],
        colors: {
            'editor.background': '#FFFFFF',
            'editor.foreground': '#1E1E1E',
        },
    });
}
