// Objeck Playground - application
//
// No build step and no framework: Monaco (pinned, from the CDN, with a plain
// text-area fallback) plus this file. The server works out which libraries a
// program needs from its `use` lines, so nothing here asks the user to pick any.

(function () {
    'use strict';

    // All-on-VPS: API is same origin. Local dev: backend on :8000
    const API_BASE = window.location.port === '8080'
        ? 'http://localhost:8000'
        : window.location.origin;

    const DEFAULT_CODE = `#~
 Welcome to the Objeck Playground.
 Press Ctrl+Enter to run, or pick a program from Examples (Ctrl+K).
 Libraries are found from your 'use' lines: try "use Data.JSON;".
~#

class Hello {
  function : Main(args : String[]) ~ Nil {
    "Hello, World!"->PrintLine();

    for(i := 1; i <= 3; i += 1;) {
      "  Count: {$i}"->PrintLine();
    };
  }
}
`;

    const $ = (id) => document.getElementById(id);
    const IS_MAC = /Mac|iPhone|iPad/.test(navigator.platform || navigator.userAgent || '');

    // localStorage can throw (private windows, blocked storage); the page must not care
    const store = {
        get(key) { try { return localStorage.getItem(key); } catch (e) { return null; } },
        set(key, value) { try { localStorage.setItem(key, value); } catch (e) { /* ignore */ } },
        remove(key) { try { localStorage.removeItem(key); } catch (e) { /* ignore */ } },
    };

    const state = {
        editor: null,
        demos: null,            // {categories: [...]}
        libInfo: {},            // id -> {name, description}
        manualLibs: new Set(),  // the optional advanced override
        running: false,
        currentDemo: null,
        timer: null,
    };

    // ------------------------------------------------------------------ utils

    function el(tag, className, text) {
        const node = document.createElement(tag);
        if (className) node.className = className;
        if (text !== undefined) node.textContent = text;
        return node;
    }

    let toastTimer = null;
    function toast(message) {
        const node = $('toast');
        node.textContent = message;
        node.hidden = false;
        clearTimeout(toastTimer);
        toastTimer = setTimeout(function () { node.hidden = true; }, 2200);
    }

    // `code` spans from the server's hints ("add `use Data.JSON;`") without innerHTML
    function appendRich(parent, text) {
        text.split('`').forEach(function (part, i) {
            parent.appendChild(i % 2 ? el('code', null, part) : document.createTextNode(part));
        });
    }

    function seconds(ms) {
        return ms < 1000 ? ms + ' ms' : (ms / 1000).toFixed(ms < 10000 ? 2 : 1) + ' s';
    }

    async function api(path, options) {
        const response = await fetch(API_BASE + path, options);
        return response;
    }

    // ------------------------------------------------------------------ theme

    function currentTheme() {
        return document.documentElement.getAttribute('data-theme') === 'dark' ? 'dark' : 'light';
    }

    function applyTheme(theme, persist) {
        document.documentElement.setAttribute('data-theme', theme);
        const button = $('theme-toggle');
        const next = theme === 'dark' ? 'light' : 'dark';
        button.setAttribute('aria-label', 'Switch to ' + next + ' theme');
        button.title = 'Switch to ' + next + ' theme';
        if (persist) store.set('objeck-theme', theme);
        if (state.editor) state.editor.setTheme(theme);
    }

    function initTheme() {
        applyTheme(currentTheme(), false);
        $('theme-toggle').addEventListener('click', function () {
            applyTheme(currentTheme() === 'dark' ? 'light' : 'dark', true);
        });
        // Follow the system until the person has chosen for themselves
        if (window.matchMedia) {
            const query = window.matchMedia('(prefers-color-scheme: dark)');
            const follow = function (e) {
                if (!store.get('objeck-theme')) applyTheme(e.matches ? 'dark' : 'light', false);
            };
            if (query.addEventListener) query.addEventListener('change', follow);
        }
        if (IS_MAC) {
            document.querySelectorAll('kbd').forEach(function (k) {
                k.innerHTML = k.innerHTML.replace('Ctrl', '&#8984;');
            });
        }
    }

    // ----------------------------------------------------------------- editor
    //
    // A small interface over Monaco, with a text-area stand-in for when the CDN
    // is unreachable: getValue, setValue, focus, goTo, mark, clearMarks,
    // onChange, setTheme.

    function monacoEditor(container) {
        window.ObjeckLang.register(monaco);
        const instance = monaco.editor.create(container, {
            value: DEFAULT_CODE,
            language: 'objeck',
            theme: currentTheme() === 'dark' ? 'objeck-dark' : 'objeck-light',
            fontSize: 13.5,
            lineHeight: 21,
            fontFamily: "ui-monospace, 'JetBrains Mono', 'Cascadia Code', 'SF Mono', Menlo, Consolas, monospace",
            fontLigatures: false,
            minimap: { enabled: false },
            scrollBeyondLastLine: false,
            automaticLayout: true,
            tabSize: 2,
            insertSpaces: true,
            lineNumbers: 'on',
            lineNumbersMinChars: 3,
            renderLineHighlight: 'line',
            matchBrackets: 'always',
            padding: { top: 12, bottom: 12 },
            overviewRulerLanes: 0,
            hideCursorInOverviewRuler: true,
            scrollbar: { verticalScrollbarSize: 10, horizontalScrollbarSize: 10 },
            accessibilitySupport: 'auto',
            ariaLabel: 'Objeck source code',
        });
        const model = instance.getModel();

        return {
            kind: 'monaco',
            getValue: () => instance.getValue(),
            setValue: (text) => { instance.setValue(text); instance.setScrollTop(0); },
            focus: () => instance.focus(),
            goTo(line, column) {
                const position = { lineNumber: line, column: Math.max(1, column || 1) };
                instance.revealPositionInCenterIfOutsideViewport(position);
                instance.setPosition(position);
                instance.focus();
            },
            mark(diagnostics) {
                const markers = diagnostics.map(function (d) {
                    const column = Math.max(1, d.column || 1);
                    const word = model.getWordAtPosition({ lineNumber: d.line, column: column });
                    return {
                        startLineNumber: d.line, startColumn: column,
                        endLineNumber: d.line, endColumn: word ? word.endColumn : column + 1,
                        message: d.message,
                        severity: d.severity === 'warning' ? monaco.MarkerSeverity.Warning : monaco.MarkerSeverity.Error,
                    };
                });
                monaco.editor.setModelMarkers(model, 'obc', markers);
            },
            clearMarks: () => monaco.editor.setModelMarkers(model, 'obc', []),
            onChange: (callback) => instance.onDidChangeModelContent(callback),
            setTheme: (theme) => monaco.editor.setTheme(theme === 'dark' ? 'objeck-dark' : 'objeck-light'),
            addCommand: (keys, callback) => instance.addCommand(keys, callback),
        };
    }

    function textareaEditor(container) {
        const area = el('textarea', 'fallback-editor');
        area.value = DEFAULT_CODE;
        area.spellcheck = false;
        area.setAttribute('aria-label', 'Objeck source code');
        area.setAttribute('autocapitalize', 'off');
        container.appendChild(area);
        let trapTab = true;
        area.addEventListener('keydown', function (e) {
            if (e.key === 'Escape') { trapTab = false; return; }
            if (e.key === 'Tab' && trapTab && !e.shiftKey && !e.ctrlKey && !e.metaKey && !e.altKey) {
                e.preventDefault();
                const start = area.selectionStart;
                area.setRangeText('  ', start, area.selectionEnd, 'end');
                area.dispatchEvent(new Event('input'));
            } else if (e.key !== 'Shift') {
                trapTab = true;
            }
        });
        return {
            kind: 'textarea',
            getValue: () => area.value,
            setValue: (text) => { area.value = text; area.scrollTop = 0; },
            focus: () => area.focus(),
            goTo(line, column) {
                const lines = area.value.split('\n');
                let offset = 0;
                for (let i = 0; i < line - 1 && i < lines.length; i += 1) offset += lines[i].length + 1;
                area.focus();
                area.setSelectionRange(offset + Math.max(0, (column || 1) - 1), offset + lines[line - 1].length);
            },
            mark() {},
            clearMarks() {},
            onChange: (callback) => area.addEventListener('input', callback),
            setTheme() {},
            addCommand() {},
        };
    }

    function createEditor() {
        const container = $('editor-container');
        const loading = el('div', 'editor-loading', 'Loading editor…');
        container.appendChild(loading);

        return new Promise(function (resolve) {
            let settled = false;
            const finish = function (editor) {
                if (settled) return;
                settled = true;
                loading.remove();
                resolve(editor);
            };
            const fallback = function () {
                try { finish(textareaEditor(container)); } catch (e) { finish(textareaEditor(container)); }
            };

            // ?editor=plain asks for the text area on purpose (slow links, assistive tech)
            const plain = new URLSearchParams(window.location.search).get('editor') === 'plain';
            if (plain || window.__monacoFailed || typeof window.require !== 'function') {
                fallback();
                return;
            }
            const timeout = setTimeout(fallback, 12000);
            try {
                window.require.config({
                    paths: { vs: 'https://cdn.jsdelivr.net/npm/monaco-editor@0.50.0/min/vs' },
                });
                window.require(['vs/editor/editor.main'], function () {
                    clearTimeout(timeout);
                    if (settled) return;
                    try { finish(monacoEditor(container)); } catch (e) { fallback(); }
                }, function () { clearTimeout(timeout); fallback(); });
            } catch (e) {
                clearTimeout(timeout);
                fallback();
            }
        });
    }

    // ----------------------------------------------------------- output panel

    const statusEl = $('status');

    function setStatus(kind, text) {
        statusEl.setAttribute('data-state', kind);
        $('status-text').textContent = text;
    }

    function resetOutput() {
        $('callout').hidden = true;
        $('callout').replaceChildren();
        $('diagnostics').hidden = true;
        $('diagnostics').replaceChildren();
        const pre = $('output-content');
        pre.removeAttribute('data-kind');
        pre.replaceChildren();
        $('copy-output').hidden = true;
        if (state.editor) state.editor.clearMarks();
    }

    function showCallout(kind, title, lines) {
        const box = $('callout');
        box.setAttribute('data-kind', kind);
        box.replaceChildren();
        if (title) box.appendChild(el('strong', null, title));
        lines.filter(Boolean).forEach(function (line) {
            const p = el('p');
            appendRich(p, line);
            box.appendChild(p);
        });
        box.hidden = false;
    }

    function showDiagnostics(diagnostics) {
        const list = $('diagnostics');
        list.replaceChildren();
        diagnostics.forEach(function (d) {
            const item = el('li');
            const button = el('button', 'diagnostic');
            button.type = 'button';
            button.setAttribute('data-severity', d.severity);
            button.title = 'Go to line ' + d.line;
            button.appendChild(el('span', 'where', 'line ' + d.line + (d.column ? ':' + d.column : '')));
            button.appendChild(el('span', 'msg', d.message));
            button.addEventListener('click', function () { state.editor.goTo(d.line, d.column); });
            item.appendChild(button);
            list.appendChild(item);
        });
        list.hidden = diagnostics.length === 0;
    }

    // Names for the libraries the server reports, for the compact note under the editor
    function libName(id) {
        return state.libInfo[id] ? state.libInfo[id].name : id;
    }

    function setLibsNote(libs) {
        const note = $('libs-note');
        note.replaceChildren();
        if (!libs) {
            note.append('picked from your ');
            note.appendChild(el('code', null, 'use'));
            note.append(' lines');
            return;
        }
        if (libs.length === 0) {
            note.append('none needed');
            return;
        }
        note.appendChild(el('span', 'used', libs.map(libName).join(', ')));
        note.append(' · detected');
    }

    function showResult(result) {
        resetOutput();
        const pre = $('output-content');
        const took = result.execution_time_ms ? ' · ' + seconds(result.execution_time_ms) : '';

        if (Array.isArray(result.libs)) setLibsNote(result.libs);

        if (result.blocked) {
            setStatus('blocked', 'Not available');
            showCallout('blocked', 'Not available in the playground', (result.error || '').split('\n'));
            return;
        }

        if (result.compile_error) {
            const errors = (result.diagnostics || []).filter(function (d) { return d.severity !== 'warning'; });
            const warnings = (result.diagnostics || []).filter(function (d) { return d.severity === 'warning'; });
            const count = errors.length;
            setStatus('compile', count > 1 ? count + ' compile errors' : 'Compile error');
            if (errors.length || warnings.length) {
                showDiagnostics(errors.concat(warnings));
                state.editor.mark(errors.concat(warnings));
            }
            if (result.hint) showCallout('hint', 'Hint', result.hint.split('\n'));
            if (!errors.length) {
                pre.setAttribute('data-kind', 'raw');
                pre.textContent = result.output || result.error || 'Compilation failed.';
            }
            return;
        }

        let text = result.output || '';
        if (result.truncated) text += '\n\n[Output was truncated]';

        if (result.success) {
            setStatus('ok', 'Finished' + took);
            if (text) { pre.textContent = text; }
            else { pre.appendChild(el('span', 'placeholder', 'The program finished without printing anything.')); }
        } else if (/timed out/i.test(result.error || '')) {
            setStatus('timeout', 'Timed out');
            if (text) pre.textContent = text;
            showCallout('blocked', 'Stopped', [result.error]);
        } else {
            setStatus('error', (result.error || 'Failed').replace(/^Process exited with /, 'Exited with ') + took);
            pre.textContent = text || result.error || 'The program failed.';
        }
        $('copy-output').hidden = !text;
    }

    function showFailure(message) {
        resetOutput();
        setStatus('error', 'Request failed');
        showCallout('error', null, [message]);
    }

    // -------------------------------------------------------------------- run

    // Replace the program without it counting as an edit by the person
    function setCode(text) {
        state.silent = true;
        try { state.editor.setValue(text); } finally { state.silent = false; }
    }

    function manualLibsList() { return Array.from(state.manualLibs); }

    async function runCode() {
        if (state.running || !state.editor) return;
        state.running = true;

        const runButton = $('run-btn');
        runButton.disabled = true;
        resetOutput();
        const started = performance.now();
        setStatus('running', 'Running…');
        state.timer = setInterval(function () {
            setStatus('running', 'Running… ' + seconds(Math.round(performance.now() - started)));
        }, 100);

        const controller = new AbortController();
        const abort = setTimeout(function () { controller.abort(); }, 40000);

        try {
            const response = await api('/api/run', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ code: state.editor.getValue(), libs: manualLibsList(), timeout: 10 }),
                signal: controller.signal,
            });

            if (response.status === 429) {
                showFailure('Too many runs in a short time. Wait a few seconds and try again.');
                return;
            }
            if (!response.ok) {
                showFailure(response.status === 422
                    ? 'That program is empty or too large to run.'
                    : 'The server returned an error (' + response.status + ').');
                return;
            }
            showResult(await response.json());
        } catch (err) {
            showFailure(err.name === 'AbortError'
                ? 'The server took too long to answer.'
                : 'Could not reach the server: ' + err.message);
        } finally {
            clearInterval(state.timer);
            clearTimeout(abort);
            state.running = false;
            runButton.disabled = false;
        }
    }

    // ----------------------------------------------- libraries (advanced only)

    async function loadLibs() {
        try {
            const response = await api('/api/libs');
            if (!response.ok) return;
            const data = await response.json();
            const container = $('lib-options');
            container.replaceChildren();
            (data.libs || []).forEach(function (lib) {
                state.libInfo[lib.id] = lib;
                const label = el('label', 'lib-option');
                label.title = lib.description;
                const box = el('input');
                box.type = 'checkbox';
                box.value = lib.id;
                box.checked = state.manualLibs.has(lib.id);
                box.addEventListener('change', function () {
                    if (box.checked) state.manualLibs.add(lib.id); else state.manualLibs.delete(lib.id);
                    updateAdvancedSummary();
                });
                label.appendChild(box);
                label.append(lib.name);
                container.appendChild(label);
            });
        } catch (err) { /* the override is optional */ }
        state.libInfo.gen_collect = { name: 'Collections', description: '' };
    }

    function syncLibOptions() {
        document.querySelectorAll('#lib-options input').forEach(function (box) {
            box.checked = state.manualLibs.has(box.value);
        });
        updateAdvancedSummary();
    }

    function updateAdvancedSummary() {
        const more = document.querySelector('.libs-more');
        more.textContent = state.manualLibs.size ? 'Advanced (' + state.manualLibs.size + ' added)' : 'Advanced';
    }

    // --------------------------------------------------------------- examples

    const panel = $('examples-panel');
    const exButton = $('examples-btn');
    const search = $('examples-search');
    const list = $('examples-list');

    function matches(demo, category, query) {
        if (!query) return true;
        const hay = (demo.title + ' ' + demo.description + ' ' + category.name + ' ' + (demo.libs || []).join(' ')).toLowerCase();
        return query.toLowerCase().split(/\s+/).every(function (word) { return hay.indexOf(word) !== -1; });
    }

    function renderExamples(query) {
        list.replaceChildren();
        if (!state.demos) {
            list.appendChild(el('div', 'empty', 'Examples are not available right now.'));
            return;
        }
        let shown = 0;
        state.demos.categories.forEach(function (category) {
            const demos = category.demos.filter(function (d) { return matches(d, category, query); });
            if (!demos.length) return;
            list.appendChild(el('div', 'group-title', category.name));
            demos.forEach(function (demo) {
                shown += 1;
                const item = el('button', 'example');
                item.type = 'button';
                item.setAttribute('data-id', demo.id);
                if (demo.id === state.currentDemo) item.setAttribute('aria-current', 'true');
                const main = el('span', 'example-main');
                main.appendChild(el('span', 'example-title', demo.title));
                main.appendChild(el('span', 'example-desc', demo.description));
                item.appendChild(main);
                const tags = el('span', 'example-libs');
                (demo.libs || []).filter(function (id) { return id !== 'gen_collect'; }).slice(0, 2).forEach(function (id) {
                    tags.appendChild(el('span', 'tag', libName(id)));
                });
                item.appendChild(tags);
                item.addEventListener('click', function () { loadDemo(demo.id); closeExamples(true); });
                list.appendChild(item);
            });
        });
        if (!shown) list.appendChild(el('div', 'empty', 'No examples match “' + query + '”.'));
    }

    function openExamples() {
        panel.hidden = false;
        exButton.setAttribute('aria-expanded', 'true');
        search.value = '';
        renderExamples('');
        search.focus();
        const current = list.querySelector('[aria-current="true"]');
        if (current) current.scrollIntoView({ block: 'nearest' });
    }

    function closeExamples(refocus) {
        if (panel.hidden) return;
        panel.hidden = true;
        exButton.setAttribute('aria-expanded', 'false');
        if (refocus) state.editor ? state.editor.focus() : exButton.focus();
    }

    function initExamples() {
        exButton.addEventListener('click', function () {
            if (panel.hidden) openExamples(); else closeExamples(true);
        });
        search.addEventListener('input', function () { renderExamples(search.value.trim()); });
        panel.addEventListener('keydown', function (e) {
            const items = Array.from(list.querySelectorAll('.example'));
            const index = items.indexOf(document.activeElement);
            if (e.key === 'Escape') { e.preventDefault(); closeExamples(true); exButton.focus(); }
            else if (e.key === 'ArrowDown') {
                e.preventDefault();
                (items[index + 1] || items[0] || search).focus();
            } else if (e.key === 'ArrowUp') {
                e.preventDefault();
                if (index <= 0) search.focus(); else items[index - 1].focus();
            } else if (e.key === 'Enter' && document.activeElement === search && items[0]) {
                e.preventDefault();
                items[0].click();
            }
        });
        document.addEventListener('pointerdown', function (e) {
            if (!panel.hidden && !panel.contains(e.target) && !exButton.contains(e.target)) closeExamples(false);
        });
    }

    async function loadDemos() {
        try {
            const response = await api('/api/demos');
            if (!response.ok) return;
            state.demos = await response.json();
        } catch (err) { /* examples are optional */ }
    }

    function setDemoUrl(id) {
        try {
            const url = new URL(window.location.href);
            url.searchParams.delete('s');
            if (id) url.searchParams.set('demo', id); else url.searchParams.delete('demo');
            history.replaceState(null, '', url.pathname + url.search);
        } catch (e) { /* ignore */ }
    }

    async function loadDemo(id) {
        try {
            const response = await api('/api/demos/' + encodeURIComponent(id));
            if (!response.ok) return false;
            const demo = await response.json();
            state.currentDemo = id;
            state.manualLibs.clear();
            syncLibOptions();
            setCode(demo.code);
            $('demo-title').textContent = demo.title;
            setLibsNote(demo.libs || []);
            resetOutput();
            setStatus('idle', 'Ready');
            $('output-content').appendChild(el('span', 'placeholder', 'Press Run to see what ' + demo.title + ' prints.'));
            setDemoUrl(id);
            return true;
        } catch (err) {
            toast('Could not load that example');
            return false;
        }
    }

    // ------------------------------------------------------------------ share

    function initShare() {
        const dialog = $('share-dialog');
        const input = $('share-url');
        const copy = $('copy-share-url');

        $('share-btn').addEventListener('click', async function () {
            const button = $('share-btn');
            button.disabled = true;
            try {
                const response = await api('/api/share', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ code: state.editor.getValue(), libs: manualLibsList() }),
                });
                if (!response.ok) {
                    let detail = 'Could not create a share link.';
                    try { const body = await response.json(); if (body.detail) detail = String(body.detail); } catch (e) { /* keep default */ }
                    toast(detail);
                    return;
                }
                const result = await response.json();
                input.value = result.url;
                copy.textContent = 'Copy link';
                if (typeof dialog.showModal === 'function') dialog.showModal(); else dialog.setAttribute('open', '');
                input.select();
            } catch (err) {
                toast('Could not create a share link: ' + err.message);
            } finally {
                button.disabled = false;
            }
        });

        copy.addEventListener('click', function () {
            input.select();
            const done = function () { copy.textContent = 'Copied'; };
            if (navigator.clipboard && navigator.clipboard.writeText) {
                navigator.clipboard.writeText(input.value).then(done, function () { document.execCommand('copy'); done(); });
            } else {
                document.execCommand('copy');
                done();
            }
        });

        dialog.addEventListener('click', function (e) { if (e.target === dialog) dialog.close(); });
    }

    // ---------------------------------------------------------------- resizer

    function initResizer() {
        const handle = $('resizer');
        const workspace = $('workspace');
        const narrow = window.matchMedia('(max-width: 800px)');
        let ratio = parseFloat(store.get('objeck-split'));
        if (!(ratio >= 20 && ratio <= 80)) ratio = narrow.matches ? 55 : 50;

        function apply(value, persist) {
            ratio = Math.max(20, Math.min(80, value));
            workspace.style.setProperty('--split', ratio + '%');
            handle.setAttribute('aria-valuenow', String(Math.round(ratio)));
            if (persist) store.set('objeck-split', String(ratio));
        }
        function orient() {
            handle.setAttribute('aria-orientation', narrow.matches ? 'horizontal' : 'vertical');
        }
        apply(ratio, false);
        orient();
        if (narrow.addEventListener) narrow.addEventListener('change', orient);

        handle.addEventListener('pointerdown', function (e) {
            handle.setPointerCapture(e.pointerId);
            handle.classList.add('active');
            document.body.style.userSelect = 'none';
            e.preventDefault();
        });
        handle.addEventListener('pointermove', function (e) {
            if (!handle.classList.contains('active')) return;
            const rect = workspace.getBoundingClientRect();
            apply(narrow.matches
                ? ((e.clientY - rect.top) / rect.height) * 100
                : ((e.clientX - rect.left) / rect.width) * 100, false);
        });
        function release() {
            if (!handle.classList.contains('active')) return;
            handle.classList.remove('active');
            document.body.style.userSelect = '';
            store.set('objeck-split', String(ratio));
        }
        handle.addEventListener('pointerup', release);
        handle.addEventListener('pointercancel', release);
        handle.addEventListener('dblclick', function () { apply(50, true); });
        handle.addEventListener('keydown', function (e) {
            const back = narrow.matches ? 'ArrowUp' : 'ArrowLeft';
            const forward = narrow.matches ? 'ArrowDown' : 'ArrowRight';
            if (e.key === back) { apply(ratio - 3, true); e.preventDefault(); }
            else if (e.key === forward) { apply(ratio + 3, true); e.preventDefault(); }
            else if (e.key === 'Home') { apply(20, true); e.preventDefault(); }
            else if (e.key === 'End') { apply(80, true); e.preventDefault(); }
        });
    }

    // ------------------------------------------------------------- start-up

    function loadDraft() {
        const draft = store.get('objeck-draft');
        return draft && draft.trim() ? draft : null;
    }

    async function loadFromUrl() {
        const params = new URLSearchParams(window.location.search);

        const shareId = params.get('s');
        if (shareId) {
            try {
                const response = await api('/api/share/' + encodeURIComponent(shareId));
                if (response.ok) {
                    const data = await response.json();
                    setCode(data.code);
                    setLibsNote(null);
                    // Links made before libraries were inferred carry the ticked list;
                    // it still loads (as the advanced override) and is harmless.
                    (data.libs || []).forEach(function (id) { if (id !== 'gen_collect') state.manualLibs.add(id); });
                    syncLibOptions();
                    $('demo-title').textContent = 'shared program';
                    return true;
                }
                toast('That share link was not found');
            } catch (err) { toast('Could not load the shared program'); }
            return false;
        }

        const demoId = params.get('demo');
        if (demoId) return loadDemo(demoId);
        return false;
    }

    function initKeyboard() {
        document.addEventListener('keydown', function (e) {
            const mod = e.ctrlKey || e.metaKey;
            if (mod && e.key === 'Enter') {
                e.preventDefault();
                runCode();
            } else if (mod && (e.key === 'k' || e.key === 'K')) {
                e.preventDefault();
                if (panel.hidden) openExamples(); else closeExamples(true);
            } else if (e.key === 'Escape' && !panel.hidden) {
                closeExamples(true);
            }
        });
    }

    async function loadVersion() {
        try {
            const response = await api('/api/health');
            if (!response.ok) return;
            const data = await response.json();
            if (data.version) {
                const tag = $('version-tag');
                tag.textContent = data.version;
                tag.hidden = false;
            }
        } catch (err) { /* leave blank */ }
    }

    async function start() {
        initTheme();
        initResizer();
        initExamples();
        initShare();
        initKeyboard();

        $('run-btn').addEventListener('click', runCode);
        $('clear-output').addEventListener('click', function () {
            resetOutput();
            setStatus('idle', 'Ready');
        });
        $('copy-output').addEventListener('click', function () {
            const text = $('output-content').textContent;
            if (navigator.clipboard && navigator.clipboard.writeText) {
                navigator.clipboard.writeText(text).then(function () { toast('Output copied'); });
            }
        });

        // everything that does not need the editor starts at once
        const libsReady = loadLibs();
        const demosReady = loadDemos();
        loadVersion();

        state.editor = await createEditor();
        state.editor.setTheme(currentTheme());

        // An edit by the person (not setCode) detaches the program from its example
        // and keeps a draft, so a reload does not lose work.
        state.editor.onChange(function () {
            if (state.silent) return;
            if (state.currentDemo) {
                state.currentDemo = null;
                setDemoUrl(null);
                $('demo-title').textContent = '';
            }
            clearTimeout(start.saveTimer);
            start.saveTimer = setTimeout(function () { store.set('objeck-draft', state.editor.getValue()); }, 400);
        });

        await Promise.all([libsReady, demosReady]);
        const fromUrl = await loadFromUrl();
        if (!fromUrl) {
            const draft = loadDraft();
            if (draft) setCode(draft);
            setLibsNote(null);
        }

        // Ctrl/Cmd+Enter inside Monaco, which would otherwise insert a line
        if (state.editor.kind === 'monaco') {
            state.editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter, runCode);
            state.editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyK, function () {
                if (panel.hidden) openExamples(); else closeExamples(true);
            });
        }
        state.editor.focus();
    }

    start();
})();
