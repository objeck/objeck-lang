#!/usr/bin/env python3
"""Regenerate TESTS.md from the .obs files in this directory.

Run:  python gen_manifest.py   (writes TESTS.md in place)

Each test's "what it validates" is taken from its header doc comment
(`#~ ... ~#` block or leading `# ...` lines); negative tests are detected via
the `# EXPECT_COMPILE_ERROR` / `# EXPECT_RUNTIME_ERROR` markers.
"""
import glob, re
from collections import Counter


def read(fn):
    return open(fn, 'r', encoding='utf-8', errors='surrogateescape').read()


def desc(txt):
    lines = txt.splitlines(); out = []; indoc = False
    for ln in lines[:60]:
        s = ln.strip()
        if s.startswith('#~'):
            indoc = True
            s2 = s.lstrip('#~').strip()
            if s2: out.append(s2)
            if s.endswith('~#') and len(s) > 3: indoc = False
            continue
        if indoc:
            if s.startswith('~#'): indoc = False; continue
            if s: out.append(s.lstrip('# ').strip())
            continue
        if s.startswith('#'):
            t = s.lstrip('# ').strip()
            if t and not t.startswith(('EXPECT_', 'JIT_DISABLE', 'EXTRA_LIBS', '-*-', '!')):
                out.append(t)
            continue
        if s.startswith(('use ', 'class ', 'bundle ', 'function', 'interface')):
            break
    text = re.sub(r'\s+', ' ', ' '.join(out)).strip()
    return re.split(r'Related commit', text)[0].strip()


CATS = [
    ('arm64_', 'ARM64 JIT'), ('jit_', 'AMD64/JIT'),
    ('core_generic', 'Generics'), ('bad_generic', 'Generics (neg)'),
    ('core_thread', 'Concurrency/GC'), ('core_', 'Core Language'),
    ('bad_', 'Negative'), ('fix', 'Bug Fix'), ('collect_', 'Collections'),
    ('ml_', 'System.ML'), ('ml', 'System.ML'), ('ai_', 'System.AI'),
    ('math', 'Math'), ('func_', 'Functional'), ('string', 'Strings'),
    ('xml', 'XML'), ('json', 'JSON'), ('api_', 'API (network)'),
    ('regex', 'Regex'), ('mcp', 'MCP Server'), ('date', 'Date/Time'),
    ('websocket', 'Networking'), ('try_', 'Exceptions'), ('select', 'Control Flow'),
    ('odbc', 'ODBC'), ('oauth', 'Networking'), ('lsp', 'LSP'),
    ('io_', 'I/O'), ('dap', 'Debugger'), ('debugger', 'Debugger'),
    ('record', 'Records'),
]


def cat(name):
    for p, c in CATS:
        if name.startswith(p):
            return c
    return 'Other'


def esc(s):
    return s.replace('|', '\\|')


def build():
    rows = []
    for fn in sorted(glob.glob('*.obs')):
        name = fn[:-4]; txt = read(fn)
        neg = ('EXPECT_COMPILE_ERROR' in txt) or ('EXPECT_RUNTIME_ERROR' in txt)
        d = desc(txt)
        if not d:
            d = ('rejects ' + name[4:].replace('_', ' ')) if name.startswith('bad_') else name.replace('_', ' ')
        if len(d) > 100:
            d = d[:97].rstrip() + '...'
        c = cat(name)
        if neg and '(neg)' not in c and c != 'Negative':
            c = c + ' (neg)'
        rows.append((name, c, d))
    return rows


HEADER = """# Regression Test Manifest

This document inventories every test in `programs/regression/`. The suite runs via
`run_regression.sh` (Linux/macOS) or `run_regression.cmd` (Windows) and in CI on
every push; each test compiles with `obc` and runs with `obr`. Tests marked
**(neg)** expect a compile or runtime error (`# EXPECT_COMPILE_ERROR` /
`# EXPECT_RUNTIME_ERROR`) and pass when that error is produced.

This file is generated — regenerate it after adding or removing tests with:

```
python gen_manifest.py
```

"""

DEBUGGER = """
## Debugger Tests (`run_debugger_tests.sh`)

| # | Test | What it validates |
|---|------|-------------------|
| 1 | `help` | Help command displays all commands |
| 2 | `breakpoints` | Set, list, and delete breakpoints |
| 3 | `run_break` | Run program and hit breakpoint |
| 4 | `print_vars` | Print Int, array, and object variables |
| 5 | `step_into` | Step into method calls |
| 6 | `step_over` | Step over method calls |
| 7 | `stack_trace` | Call stack display |
| 8 | `list_source` | Source code listing |
| 9 | `memory` | Memory allocation stats |
| 10 | `info` | Program/class information |
| 11 | `print_self` | Print `@self` and instance variables |
| 12 | `step_out` | Step out of method (jump) |
| 13 | `full_run` | Full program execution without breakpoints |
| 14 | `clear_breaks` | Clear all breakpoints |

The DAP (Debug Adapter Protocol) tests run separately via `run_dap_tests.sh`.

## Running the Suite

```
cd programs/regression
./run_regression.sh x64            # or arm64; run_regression.cmd on Windows
TEST_TIMEOUT=120 ./run_regression.sh x64   # per-test wall-clock cap (seconds)
```

The runner compiles and runs every `*.obs`, reports a `PASS`/`FAIL` per test, and
prints a final `Results: N passed, M failed` summary (and a GitHub Actions step
summary in CI). Network-dependent tests (`api_*`, `oauth_*`, `mcp_*`) may be
skipped or quarantined in CI when the target service is unavailable.
"""


def main():
    rows = build()
    counts = Counter(c for _, c, _ in rows)
    parts = [HEADER]
    parts.append('**Total runtime tests: %d** (plus 14 debugger tests, see below).\n' % len(rows))
    parts.append('\n## Tests by Category\n')
    parts.append('| Category | Count |')
    parts.append('|----------|-------|')
    for c, n in sorted(counts.items(), key=lambda x: (-x[1], x[0])):
        parts.append('| %s | %d |' % (c, n))
    parts.append('\n## Test Inventory\n')
    parts.append('| # | Test | Category | What it validates | Status |')
    parts.append('|---|------|----------|-------------------|--------|')
    for i, (name, c, d) in enumerate(rows, 1):
        parts.append('| %d | `%s.obs` | %s | %s | ✅ |' % (i, name, esc(c), esc(d)))
    out = '\n'.join(parts) + '\n' + DEBUGGER
    open('TESTS.md', 'w', encoding='utf-8').write(out)
    print('wrote TESTS.md:', len(rows), 'runtime tests,', len(counts), 'categories')


if __name__ == '__main__':
    main()
