// Objeck Playground - Main Application
// Initializes Monaco editor, handles API calls, manages UI state

(function () {
    'use strict';

    // --- Configuration ---

    // All-on-VPS: API is same origin. Local dev: backend on :8000
    const API_BASE = window.location.port === '8080'
        ? 'http://localhost:8000'
        : window.location.origin;

    const AVAILABLE_LIBS = [
        { id: 'collect', name: 'Collections', desc: 'Vector, Map, List, etc.' },
        { id: 'json', name: 'JSON', desc: 'JSON parsing and creation' },
        { id: 'xml', name: 'XML', desc: 'XML parsing with XPath' },
        { id: 'regex', name: 'RegEx', desc: 'Regular expressions' },
        { id: 'cipher', name: 'Cipher', desc: 'Encryption and hashing' },
        { id: 'csv', name: 'CSV', desc: 'CSV file processing' },
        { id: 'query', name: 'Query', desc: 'Data query support' },
        { id: 'ml', name: 'ML', desc: 'Machine learning and matrix math' },
        { id: 'misc', name: 'Misc', desc: 'Miscellaneous utilities' },
    ];

    const DEFAULT_CODE = `#~
 Welcome to the Objeck Playground!
 Click Run (or Ctrl+Enter) to execute this code.

 Tips:
 - Pick an example from the 'Examples' dropdown above
 - Some programs need libraries: enable them in the toolbar
   (demos auto-select the right ones for you)
 - Use 'use' to import a library, e.g. "use Collection;"
~#

class Hello {
  function : Main(args : String[]) ~ Nil {
    "Hello World!"->PrintLine();
    "Welcome to the Objeck Playground"->PrintLine();

    # Try modifying this code and click Run (or Ctrl+Enter)
    for(i := 1; i <= 5; i += 1;) {
      "  Count: {$i}"->PrintLine();
    };
  }
}`;

    let editor = null;
    let demosData = null;

    // --- Monaco Editor Setup ---

    require.config({
        paths: { vs: 'https://cdn.jsdelivr.net/npm/monaco-editor@0.50.0/min/vs' }
    });

    require(['vs/editor/editor.main'], function () {
        registerObjeckLanguage(monaco);
        defineObjeckThemes(monaco);

        const theme = getStoredTheme();
        applyTheme(theme);

        editor = monaco.editor.create(document.getElementById('editor-container'), {
            value: DEFAULT_CODE,
            language: 'objeck',
            theme: theme === 'dark' ? 'objeck-dark' : 'objeck-light',
            fontSize: 14,
            fontFamily: "'JetBrains Mono', 'Fira Code', Consolas, monospace",
            minimap: { enabled: false },
            scrollBeyondLastLine: false,
            automaticLayout: true,
            tabSize: 4,
            insertSpaces: false,
            lineNumbers: 'on',
            renderLineHighlight: 'line',
            matchBrackets: 'always',
            padding: { top: 8 },
        });

        // Ctrl+Enter to run
        editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter, runCode);

        // Initialize UI
        initLibSelector();
        initThemeToggle();
        initSplitPane();
        initShareHandling();
        loadDemos();
        loadFromUrl();
        loadVersion();
    });

    // --- Objeck Language Definition ---
    // (Inlined to avoid module loading complexity with Monaco's require)

    function registerObjeckLanguage(monaco) {
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
            typeKeywords: ['Byte', 'Int', 'Float', 'Char', 'Bool', 'Nil', 'String'],
            builtinKeywords: ['New', 'As', 'TypeOf', 'Parent', 'Try', 'Otherwise', 'true', 'false'],
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
            comments: { lineComment: '#', blockComment: ['#~', '~#'] },
            brackets: [['{', '}'], ['[', ']'], ['(', ')']],
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

    function defineObjeckThemes(monaco) {
        monaco.editor.defineTheme('objeck-dark', {
            base: 'vs-dark', inherit: true,
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
            base: 'vs', inherit: true,
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
            ],
            colors: {
                'editor.background': '#FFFFFF',
                'editor.foreground': '#1E1E1E',
            },
        });
    }

    // --- Run Code ---

    async function runCode() {
        const code = editor.getValue();
        const libs = getSelectedLibs();
        const runBtn = document.getElementById('run-btn');
        const spinner = document.getElementById('spinner');
        const outputEl = document.getElementById('output-content');
        const execTimeEl = document.getElementById('exec-time');

        runBtn.disabled = true;
        spinner.classList.remove('hidden');
        outputEl.textContent = '';
        outputEl.className = '';
        execTimeEl.classList.add('hidden');

        try {
            const response = await fetch(`${API_BASE}/api/run`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ code, libs, timeout: 10 }),
            });

            if (response.status === 429) {
                outputEl.textContent = 'Rate limit exceeded. Please wait a moment and try again.';
                outputEl.className = 'error';
                return;
            }

            const result = await response.json();

            let output = result.output || '';
            if (result.error && !result.success) {
                output += (output ? '\n' : '') + result.error;
            }
            if (result.truncated) {
                output += '\n[Output was truncated]';
            }

            outputEl.textContent = output || '(No output)';

            if (result.compile_error) {
                outputEl.className = 'compile-error';
            } else if (result.success) {
                outputEl.className = 'success';
            } else {
                outputEl.className = 'error';
            }

            if (result.execution_time_ms) {
                execTimeEl.textContent = `${result.execution_time_ms}ms`;
                execTimeEl.classList.remove('hidden');
            }
        } catch (err) {
            outputEl.textContent = `Network error: ${err.message}\n\nMake sure the backend is running.`;
            outputEl.className = 'error';
        } finally {
            runBtn.disabled = false;
            spinner.classList.add('hidden');
        }
    }

    document.getElementById('run-btn').addEventListener('click', runCode);
    document.getElementById('clear-output').addEventListener('click', function () {
        const outputEl = document.getElementById('output-content');
        outputEl.textContent = '';
        outputEl.className = '';
        document.getElementById('exec-time').classList.add('hidden');
    });

    // --- Library Selector ---

    function initLibSelector() {
        const container = document.getElementById('lib-selector');
        AVAILABLE_LIBS.forEach(function (lib) {
            const chip = document.createElement('label');
            chip.className = 'lib-chip';
            chip.title = lib.desc;
            chip.innerHTML = `<input type="checkbox" value="${lib.id}"><span>${lib.name}</span>`;
            chip.addEventListener('click', function () {
                // Toggle after the checkbox changes
                setTimeout(function () {
                    const cb = chip.querySelector('input');
                    chip.classList.toggle('active', cb.checked);
                }, 0);
            });
            container.appendChild(chip);
        });
    }

    function getSelectedLibs() {
        var libs = [];
        var checkboxes = document.querySelectorAll('#lib-selector input:checked');
        checkboxes.forEach(function (cb) { libs.push(cb.value); });
        return libs;
    }

    function setSelectedLibs(libs) {
        document.querySelectorAll('#lib-selector .lib-chip').forEach(function (chip) {
            var cb = chip.querySelector('input');
            var isSelected = libs.indexOf(cb.value) !== -1;
            cb.checked = isSelected;
            chip.classList.toggle('active', isSelected);
        });
    }

    // --- Demo Loader ---

    async function loadDemos() {
        try {
            const response = await fetch(`${API_BASE}/api/demos`);
            if (!response.ok) return;
            demosData = await response.json();

            const select = document.getElementById('demo-select');
            demosData.categories.forEach(function (category) {
                const group = document.createElement('optgroup');
                group.label = category.name;
                category.demos.forEach(function (demo) {
                    const option = document.createElement('option');
                    option.value = demo.id;
                    option.textContent = demo.title;
                    option.title = demo.description;
                    group.appendChild(option);
                });
                select.appendChild(group);
            });
        } catch (err) {
            // Demos endpoint not available - that's fine
        }
    }

    document.getElementById('demo-select').addEventListener('change', async function () {
        const demoId = this.value;
        if (!demoId) return;

        try {
            const response = await fetch(`${API_BASE}/api/demos/${demoId}`);
            if (!response.ok) return;
            const demo = await response.json();

            editor.setValue(demo.code);
            setSelectedLibs(demo.libs || []);

            // Show library notification when demo auto-selects libs
            if (demo.libs && demo.libs.length > 0) {
                showLibNotification(demo.libs);
            }

            // Clear output
            document.getElementById('output-content').textContent = '';
            document.getElementById('output-content').className = '';
        } catch (err) {
            // Ignore
        }
    });

    function showLibNotification(libs) {
        var el = document.getElementById('lib-notification');
        var names = libs.map(function (id) {
            var lib = AVAILABLE_LIBS.find(function (l) { return l.id === id; });
            return lib ? lib.name : id;
        });
        el.textContent = 'Auto-enabled: ' + names.join(', ');
        el.classList.add('show');
        setTimeout(function () { el.classList.remove('show'); }, 3000);
    }

    // --- Theme Toggle ---

    function getStoredTheme() {
        return localStorage.getItem('objeck-theme') || 'dark';
    }

    function applyTheme(theme) {
        document.documentElement.setAttribute('data-theme', theme);
        var icon = document.getElementById('theme-icon');
        icon.innerHTML = theme === 'dark' ? '&#9790;' : '&#9728;';
        localStorage.setItem('objeck-theme', theme);

        if (editor) {
            monaco.editor.setTheme(theme === 'dark' ? 'objeck-dark' : 'objeck-light');
        }
    }

    function initThemeToggle() {
        document.getElementById('theme-toggle').addEventListener('click', function () {
            var current = getStoredTheme();
            applyTheme(current === 'dark' ? 'light' : 'dark');
        });
    }

    // --- Split Pane Resize ---

    function initSplitPane() {
        const handle = document.getElementById('resize-handle');
        const main = document.getElementById('main-content');
        const editorPane = document.getElementById('editor-pane');
        const outputPane = document.getElementById('output-pane');
        let isResizing = false;

        handle.addEventListener('mousedown', function (e) {
            isResizing = true;
            handle.classList.add('active');
            document.body.style.cursor = 'col-resize';
            document.body.style.userSelect = 'none';
            e.preventDefault();
        });

        document.addEventListener('mousemove', function (e) {
            if (!isResizing) return;

            const rect = main.getBoundingClientRect();
            const percent = ((e.clientX - rect.left) / rect.width) * 100;
            const clamped = Math.max(20, Math.min(80, percent));

            editorPane.style.flex = `0 0 ${clamped}%`;
            outputPane.style.flex = `0 0 ${100 - clamped}%`;
        });

        document.addEventListener('mouseup', function () {
            if (isResizing) {
                isResizing = false;
                handle.classList.remove('active');
                document.body.style.cursor = '';
                document.body.style.userSelect = '';
            }
        });
    }

    // --- Share Links ---

    function initShareHandling() {
        const shareBtn = document.getElementById('share-btn');
        const modal = document.getElementById('share-modal');
        const closeBtn = document.getElementById('close-modal');
        const copyBtn = document.getElementById('copy-share-url');
        const urlInput = document.getElementById('share-url');

        shareBtn.addEventListener('click', async function () {
            const code = editor.getValue();
            const libs = getSelectedLibs();

            try {
                const response = await fetch(`${API_BASE}/api/share`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ code, libs }),
                });

                if (!response.ok) {
                    alert('Failed to create share link');
                    return;
                }

                const result = await response.json();
                urlInput.value = result.url;
                modal.classList.remove('hidden');
                urlInput.select();
            } catch (err) {
                alert('Failed to create share link: ' + err.message);
            }
        });

        closeBtn.addEventListener('click', function () {
            modal.classList.add('hidden');
        });

        copyBtn.addEventListener('click', function () {
            urlInput.select();
            navigator.clipboard.writeText(urlInput.value).then(function () {
                copyBtn.textContent = 'Copied!';
                setTimeout(function () { copyBtn.textContent = 'Copy URL'; }, 2000);
            });
        });

        modal.addEventListener('click', function (e) {
            if (e.target === modal) modal.classList.add('hidden');
        });
    }

    async function loadVersion() {
        try {
            const response = await fetch(`${API_BASE}/api/health`);
            if (!response.ok) return;
            const data = await response.json();
            if (data.version) {
                document.getElementById('version-tag').textContent = data.version;
            }
        } catch (err) {
            // Version not available - leave blank
        }
    }

    async function loadFromUrl() {
        const params = new URLSearchParams(window.location.search);
        const shareId = params.get('s');
        if (!shareId) return;

        try {
            const response = await fetch(`${API_BASE}/api/share/${shareId}`);
            if (!response.ok) return;
            const data = await response.json();

            editor.setValue(data.code);
            setSelectedLibs(data.libs || []);
        } catch (err) {
            // Ignore - shared code not found
        }
    }

})();
