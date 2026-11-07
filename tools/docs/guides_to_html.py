#!/usr/bin/env python3
"""Render the Markdown the site links to as pages in the site's theme.

Two sets of source files become pages in docs/web:

  * docs/guides/*.md     ->  guides.html and guide_*.html (the getting-started guides)
  * the files in DOCS    ->  docs.html (an index) and doc_*.html (architecture, design notes,
                             the language reference, performance, editors and so on)

The Markdown is the source. Every page copies its head, menu and footer from
docs/web/ai_guide.html, so a change to the menu there reaches all of them the next time this runs.

  python tools/docs/guides_to_html.py           write the pages
  python tools/docs/guides_to_html.py --check   exit 1 if a page on disk differs from what the
                                                Markdown renders to

It handles the Markdown these files use: headings, paragraphs, fenced code ('text', 'bash' and
'cmd' blocks become command blocks, 'mermaid' blocks become diagrams, 'ruby', 'objeck' and 'json'
blocks are highlighted), tables, nested bullet and numbered lists, block quotes, rules, images,
inline code, bold, italic and links, and passes raw HTML blocks through. HTML comments are
dropped. A relative link to another file that has a page here becomes a link to that page; a link
to anything else in the repository becomes a link to it on GitHub.
"""
import html
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
WEB = os.path.join(ROOT, "docs", "web")
GH = "https://github.com/objeck/objeck-lang"
RAW = "https://raw.githubusercontent.com/objeck/objeck-lang/master"

# repo-relative Markdown file -> (html file, short title, description)
GUIDES = {
    "docs/guides/README.md": ("guides.html", "Getting-Started Guides",
                              "Short, task-oriented Objeck guides, each built around a complete program that was compiled and run."),
    "docs/guides/whats_new_10_3.md": ("guide_whats_new.html", "What is New in v2026.10.3",
                                      "Upgrading to Objeck v2026.10.3: recompile your programs, HttpClient connection reuse, library Nil fixes, language server speedups."),
    "docs/guides/query.md": ("guide_query.html", "Query.Structured Guide",
                             "A database inside your program: typed in-memory tables, SQL joins, grouping, prepared statements and files."),
    "docs/guides/http.md": ("guide_http.html", "HTTP Client and Web Server Guide",
                            "An Objeck web server, an HTTP client, and how v2026.10.3 reuses connections."),
    "docs/guides/data_and_text.md": ("guide_data.html", "JSON, XML and RegEx Guide",
                                     "Parse JSON, XML and regular expressions, and what they do with missing or malformed input."),
    "docs/guides/collections.md": ("guide_collections.html", "Collections Guide",
                                   "Hash, Map, Vector, Set and MultiHash in one short program."),
    "docs/guides/ai.md": ("guide_ai.html", "Game AI Guide",
                          "Minimax and Monte Carlo tree search with System.AI, on tic-tac-toe, and the checkers example."),
    "docs/guides/language_server.md": ("guide_editors.html", "Editor and Language Server Guide",
                                       "Install the Objeck language server for VS Code and other editors, and debug a program."),
}

# repo-relative Markdown file -> (html file, group, card label, one-line blurb)
DOCS = {
    'docs/ARCHITECTURE_MAP.md': ('doc_architecture_map.html', 'How it works', 'MP',
        'One page that shows how the compiler, file formats, VM, JIT, collector, libraries and tools fit together.'),
    'docs/architecture.md': ('doc_architecture.html', 'How it works', 'AR',
        'The compiler, file formats, VM, JIT, garbage collector, libraries and CI, with diagrams.'),
    'docs/ARCHITECTURE_ASSESSMENT_2026_11.md': ('doc_assessment.html', 'How it works', 'AS',
        'An honest assessment of the architecture: what is sound, what is risky, what to do next.'),
    'docs/SECURITY_MODEL.md': ('doc_security.html', 'How it works', 'SM',
        'The VM trust model: an .obe is native code, what the loader checks and what it cannot.'),
    'docs/performance.md': ('doc_performance.html', 'How it works', 'PF',
        'Benchmarks, methodology and cross-language comparisons.'),
    'docs/FEATURES.md': ('doc_features.html', 'Language and libraries', 'LF',
        'The language reference with examples: classes, generics, closures, errors, strings and more.'),
    'docs/AI.md': ('doc_ai.html', 'Language and libraries', 'AI',
        'OpenAI, Gemini, Ollama, ONNX, OpenCV, ML and NLP, with working code.'),
    'docs/COLLECTIONS_GUIDE.md': ('doc_collections.html', 'Language and libraries', 'CO',
        'Choosing between Vector, Hash, Map, Set, Cache and the unboxed collections.'),
    'docs/ERROR_CONVENTIONS.md': ('doc_errors.html', 'Language and libraries', 'ER',
        'How library calls report failure: Nil, Raise, status codes, and what to test.'),
    'docs/EXAMPLES.md': ('doc_examples.html', 'Language and libraries', 'EX',
        'Practical code examples across the standard library.'),
    'docs/MODELS.md': ('doc_models.html', 'Language and libraries', 'ML',
        'Which AI models Objeck can run, and where to get them.'),
    'docs/GC_ROOTS_DESIGN.md': ('doc_gc_roots.html', 'Design notes', 'GC',
        'How compiled code tells the collector which words are references, on x64 and ARM64.'),
    'docs/INCREMENTAL_ANALYSIS_DESIGN.md': ('doc_incremental.html', 'Design notes', 'IA',
        'Per-file incremental analysis in the compiler, for a faster language server.'),
    'docs/QUERY_DESIGN.md': ('doc_query_design.html', 'Design notes', 'QD',
        'Query.Structured: columns, zone maps, indexes, the planner and the .otab format.'),
    'docs/AI_FOUNDATION_DESIGN.md': ('doc_ai_foundation.html', 'Design notes', 'AF',
        'The tensor and model foundation under the ONNX, BERT and inference libraries.'),
    'docs/ML_BERT_DESIGN.md': ('doc_ml_bert.html', 'Design notes', 'BE',
        'BERT sentence embeddings over ONNX, and a trainable head.'),
    'docs/ML_LAB_SUPPORT_DESIGN.md': ('doc_ml_lab.html', 'Design notes', 'LB',
        'System.ML.Lab: experiments, result tables and charts for an ML lab.'),
    'docs/NAMED_ARGUMENTS_DESIGN.md': ('doc_named_arguments.html', 'Design notes', 'NA',
        'How named arguments are parsed, checked and compiled.'),
    'docs/METHOD_GENERICS_DESIGN.md': ('doc_method_generics.html', 'Design notes', 'MG',
        'Generics on methods, with bounds, inference and the checks they need.'),
    'docs/REFERENCE_PARAMETERS_DESIGN.md': ('doc_reference_parameters.html', 'Design notes', 'RP',
        'Reference parameters: semantics and the code the compiler emits.'),
    'docs/RECOVERED_ERRORS_DESIGN.md': ('doc_recovered_errors.html', 'Design notes', 'RE',
        'Reading a recovered error: the API and what it reports.'),
    'docs/JAGGED_ARRAYS_DESIGN.md': ('doc_jagged_arrays.html', 'Design notes', 'JA',
        'Jagged arrays: layout, access and bounds checks.'),
    'docs/OTAB_FORMAT.md': ('doc_otab_format.html', 'Reference', 'OT',
        'Byte-exact specification of the .otab table file, versions 1 and 2.'),
    'docs/cli_options.md': ('doc_cli_options.html', 'Reference', 'CL',
        'Every command-line option of obc, obr, obd and the other tools.'),
    'docs/native_interface.md': ('doc_native_interface.html', 'Reference', 'NI',
        'Write a native library: the C++ interface, the ABI version and parking for the collector.'),
    'docs/third_party_dependencies.md': ('doc_dependencies.html', 'Reference', 'TP',
        'Where each platform gets its dependencies, and how to check versions.'),
    'docs/ADDING_AN_INTRINSIC.md': ('doc_intrinsic.html', 'Contributing', 'IN',
        'Add a new built-in method to the compiler, VM and both JITs.'),
    'programs/regression/README.md': ('doc_regression.html', 'Contributing', 'RG',
        'The regression suite: how to run it and how to add a test.'),
    'tools/fuzz/loader/README.md': ('doc_fuzz.html', 'Contributing', 'FZ',
        'Fuzz targets for the loader, and how to run them.'),
    'docs/editors.md': ('doc_editors.html', 'Tools', 'ED',
        'Syntax highlighting, build integration, language server and debugger for each editor.'),
    'tools/lsp/README.md': ('doc_lsp.html', 'Tools', 'LS',
        'The language server: features, installation and the editor clients.'),
    'tools/lsp/clients/sublime/README.md': ('doc_sublime.html', 'Tools', 'SB',
        'The Sublime Text client for the language server.'),
    'programs/deploy/README.md': ('doc_deploy_examples.html', 'Tools', 'PR',
        'The numbered example programs that ship with every release, in learning order.'),
    'programs/examples/checkers/README.md': ('doc_checkers.html', 'Tools', 'CK',
        'A checkers game with ten AI levels, a learner, ten piece sets and generated music.'),
    'CHANGELOG.md': ('doc_changelog.html', 'Releases', 'CH',
        'Every change in every release, in detail.'),
}


PAGE_OF = {}
for _md, _v in GUIDES.items():
    PAGE_OF[_md] = _v[0]
for _md, _v in DOCS.items():
    PAGE_OF[_md] = _v[0]

CMD_LANGS = {"text", "bash", "sh", "cmd", "powershell", "ps1", "shell", "console"}
LANG_CLASS = {"ruby": "cpp", "objeck": "cpp", "json": "json", "elisp": "lisp", "vim": "vim", "": ""}


def slug_guide(text):
    s = re.sub(r"<[^>]+>", "", text).lower()
    s = re.sub(r"[^a-z0-9]+", "-", s).strip("-")
    return s or "section"


def slug_gfm(text):
    """GitHub's heading anchors, so a link written for GitHub keeps working."""
    s = html.unescape(re.sub(r"<[^>]+>", "", text)).lower().strip()
    s = re.sub(r"[^\w\- ]", "", s)
    return s.replace(" ", "-") or "section"


def fix_link(url, srcdir):
    if url.startswith(("http://", "https://", "#", "mailto:")):
        return url
    path, _, frag = url.partition("#")
    frag = "#" + frag if frag else ""
    rel = os.path.normpath(os.path.join(srcdir, path)).replace("\\", "/")
    if rel in PAGE_OF:
        return PAGE_OF[rel] + frag
    if rel.startswith(".."):
        return url
    real = os.path.join(ROOT, rel.replace("/", os.sep))
    kind = "tree" if (path.endswith("/") or os.path.isdir(real)) else "blob"
    return "%s/%s/master/%s%s" % (GH, kind, rel.rstrip("/"), frag)


def fix_image(url, srcdir):
    if url.startswith(("http://", "https://", "data:")):
        return url
    rel = os.path.normpath(os.path.join(srcdir, url)).replace("\\", "/")
    return "%s/%s" % (RAW, rel)


ALLOWED_INLINE = re.compile(r"&lt;(/?)(br|kbd|sub|sup|b|i|em|strong)\s*/?&gt;")


def inline(text, srcdir):
    spans = []

    def stash(m):
        spans.append("<code>%s</code>" % html.escape(m.group(1), quote=False))
        return "\x00%d\x00" % (len(spans) - 1)

    text = re.sub(r"`([^`]+)`", stash, text)
    text = html.escape(text, quote=False)
    text = re.sub(r'&lt;a (?:name|id)="?[\w-]+"?&gt;&lt;/a&gt;', "", text)
    text = ALLOWED_INLINE.sub(lambda m: "<br>" if m.group(2) == "br" else "<%s%s>" % (m.group(1), m.group(2)), text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"(?<![\w*])\*([^*\s][^*]*)\*(?![\w*])", r"<em>\1</em>", text)

    def image(m):
        return '<img src="%s" alt="%s" loading="lazy">' % (fix_image(html.unescape(m.group(2)), srcdir), m.group(1))

    text = re.sub(r"!\[([^\]]*)\]\(([^)\s]+)\)", image, text)

    def link(m):
        label, url = m.group(1), m.group(2)
        url = fix_link(html.unescape(url), srcdir)
        ext = ' target="_blank"' if url.startswith("http") and "objeck.org" not in url else ""
        return '<a href="%s"%s>%s</a>' % (url, ext, label)

    text = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", link, text)
    text = re.sub(r"&lt;(https?://[^&\s]+)&gt;",
                  lambda m: '<a href="%s" target="_blank">%s</a>' % (m.group(1), m.group(1)), text)
    text = re.sub(r"\x00(\d+)\x00", lambda m: spans[int(m.group(1))], text)
    return text


LIST_RE = re.compile(r"^(\s*)([-*+]|\d+\.) (.*)")
HTML_BLOCK_RE = re.compile(r"^\s*<(?!!--)(/?[a-zA-Z][\w-]*)(\s|>|/)")
HR_RE = re.compile(r"^(-{3,}|\*{3,}|_{3,})\s*$")


def split_cells(line):
    cells = re.split(r"(?<!\\)\|", line.strip().strip("|"))
    return [c.strip().replace("\\|", "|") for c in cells]


def render_list(items, srcdir):
    """items: list of (indent, ordered, text). Renders nesting by indentation."""
    out = []

    def build(pos, base):
        ordered = items[pos][1]
        tag = "ol" if ordered else "ul"
        start = ""
        m = re.match(r"\d+", items[pos][3]) if ordered else None
        if ordered and items[pos][3] != "1":
            start = ' start="%s"' % items[pos][3]
        lines = ["<%s%s>" % (tag, start)]
        while pos < len(items) and items[pos][0] >= base:
            indent, _, text, _num = items[pos]
            if indent > base:
                break
            li = " <li>%s" % inline(text, srcdir)
            pos += 1
            if pos < len(items) and items[pos][0] > indent:
                sub, pos = build(pos, items[pos][0])
                li += "\n" + sub + "\n "
            li += "</li>"
            lines.append(li)
            if pos < len(items) and items[pos][0] < base:
                break
        lines.append("</%s>" % tag)
        return "\n".join(lines), pos

    pos = 0
    while pos < len(items):
        block, pos = build(pos, items[pos][0])
        out.append(block)
    return out


def render(md_text, srcdir, doc=False):
    slug = slug_gfm if doc else slug_guide
    lines = md_text.split("\n")
    out, toc = [], []
    seen = {}
    i = 0
    title = None
    lead_done = False
    has_mermaid = False

    def unique(sid):
        n = seen.get(sid, 0)
        seen[sid] = n + 1
        return sid if n == 0 else "%s-%d" % (sid, n)

    while i < len(lines):
        ln = lines[i]
        if ln.lstrip().startswith("<!--"):
            while i < len(lines) and "-->" not in lines[i]:
                i += 1
            i += 1
            continue
        fm = re.match(r"^(\s*)```(.*)$", ln)
        if fm:
            lang = fm.group(2).strip().split()[0] if fm.group(2).strip() else ""
            indent = len(fm.group(1))
            i += 1
            buf = []
            while i < len(lines) and not re.match(r"^\s*```\s*$", lines[i]):
                buf.append(lines[i][indent:] if lines[i][:indent].strip() == "" else lines[i])
                i += 1
            i += 1
            body = html.escape("\n".join(buf), quote=False)
            if lang == "mermaid":
                has_mermaid = True
                out.append('<pre class="mermaid">%s</pre>' % body)
            elif lang in CMD_LANGS:
                out.append('<pre class="cmd">%s</pre>' % body)
            elif lang == "" and doc:
                out.append('<pre class="plain">%s</pre>' % body)
            else:
                cl = LANG_CLASS.get(lang, lang or "cpp")
                out.append('<pre class="line-numbers"><code class="language-%s">%s\n</code></pre>' % (cl, body))
            continue
        if ln.startswith("# "):
            if title is None:
                title = ln[2:].strip()
                i += 1
                continue
            txt = ln[2:].strip()
            sid = unique(slug(txt))
            toc.append((sid, inline(txt, srcdir)))
            out.append('<h3 id="%s">%s</h3>' % (sid, inline(txt, srcdir)))
            i += 1
            continue
        m = re.match(r"(#{2,6}) (.*)", ln)
        if m and doc and m.group(2).strip().lower() in ("table of contents", "contents"):
            i += 1
            while i < len(lines) and not lines[i].startswith("#"):
                i += 1
            continue
        if m:
            lvl, txt = len(m.group(1)), m.group(2).strip()
            sid = unique(slug(txt))
            if lvl == 2:
                toc.append((sid, inline(txt, srcdir)))
                out.append('<h3 id="%s">%s</h3>' % (sid, inline(txt, srcdir)))
            elif lvl == 3:
                out.append('<h4 id="%s">%s</h4>' % (sid, inline(txt, srcdir)))
            else:
                out.append('<h5 id="%s">%s</h5>' % (sid, inline(txt, srcdir)))
            i += 1
            continue
        if HR_RE.match(ln):
            out.append('<hr class="section-end">')
            i += 1
            continue
        if ln.startswith(">"):
            buf = []
            while i < len(lines) and lines[i].startswith(">"):
                buf.append(lines[i].lstrip("> ").rstrip())
                i += 1
            out.append('<div class="note">%s</div>' % inline(" ".join(buf), srcdir))
            continue
        if ln.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append(split_cells(lines[i]))
                i += 1
            head, body = rows[0], rows[2:]
            t = ['<table class="ref-table">', " <thead>",
                 "  <tr>" + "".join("<th>%s</th>" % inline(c, srcdir) for c in head) + "</tr>", " </thead>", " <tbody>"]
            for r in body:
                cells = []
                for n, c in enumerate(r):
                    label = html.escape(re.sub(r"[`*]", "", head[n]) if n < len(head) else "", quote=True)
                    cells.append('<td data-label="%s">%s</td>' % (label, inline(c, srcdir)))
                t.append("  <tr>" + "".join(cells) + "</tr>")
            t += [" </tbody>", "</table>"]
            out.append("\n".join(t))
            continue
        if LIST_RE.match(ln):
            items = []
            while i < len(lines):
                cur = lines[i]
                lm = LIST_RE.match(cur)
                if lm:
                    ordered = lm.group(2)[0].isdigit()
                    items.append([len(lm.group(1)), ordered, lm.group(3), lm.group(2)[:-1] if ordered else ""])
                    i += 1
                elif cur.startswith("  ") and cur.strip() and items and not re.match(r"^\s*```", cur):
                    items[-1][2] += " " + cur.strip()
                    i += 1
                elif not cur.strip():
                    j = i
                    while j < len(lines) and not lines[j].strip():
                        j += 1
                    if j < len(lines) and LIST_RE.match(lines[j]) and j - i <= 2:
                        i = j
                    else:
                        break
                else:
                    break
            out += render_list([(a, b, c, d) for a, b, c, d in items], srcdir)
            continue
        if not ln.strip():
            i += 1
            continue
        if HTML_BLOCK_RE.match(ln):
            buf = []
            while i < len(lines) and lines[i].strip():
                buf.append(lines[i].rstrip())
                i += 1
            raw = "\n".join(buf)
            raw = re.sub(r'(src|href)="(?!https?:|#|mailto:|data:)([^"]+)"',
                         lambda m: '%s="%s"' % (m.group(1), fix_image(m.group(2), srcdir) if m.group(1) == "src"
                                                else fix_link(m.group(2), srcdir)), raw)
            out.append('<div class="raw">%s</div>' % raw)
            continue
        buf = []
        while (i < len(lines) and lines[i].strip() and not re.match(r"^\s*```", lines[i])
               and not lines[i].startswith(("#", ">", "|", "<!--")) and not HR_RE.match(lines[i])
               and not LIST_RE.match(lines[i]) and not (HTML_BLOCK_RE.match(lines[i]) and buf)):
            buf.append(lines[i].strip())
            i += 1
        if not buf:
            buf.append(lines[i].strip())
            i += 1
        para = inline(" ".join(buf), srcdir)
        if not lead_done:
            out.append('<p class="page-lead">%s</p>' % para)
            lead_done = True
        else:
            out.append("<p>%s</p>" % para)
    return title, toc, out, has_mermaid


MERMAID = ('  <script type="module">\n'
           "    import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@11.4.1/dist/mermaid.esm.min.mjs';\n"
           "    mermaid.initialize({ startOnLoad: false, theme: 'dark', securityLevel: 'loose',\n"
           "      themeVariables: { fontFamily: 'Inter, sans-serif', primaryColor: '#16223a', primaryBorderColor: '#6366f1',\n"
           "                        lineColor: '#7dd3fc', primaryTextColor: '#e8eef8' } });\n"
           "    await mermaid.run({ querySelector: 'pre.mermaid', suppressErrors: true });\n"
           "  </script>\n")


def plain_text(para_html):
    t = re.sub(r"<[^>]+>", "", para_html)
    return html.unescape(t).strip()


def page_head(tpl_head, title, desc, mermaid=False):
    h = re.sub(r"<title>.*?</title>", "<title>%s — Objeck Language</title>" % html.escape(title, quote=False),
               tpl_head, flags=re.S)
    h = re.sub(r'<meta name="description" content=".*?"/>',
               lambda _m: '<meta name="description" content="%s"/>' % html.escape(desc), h, flags=re.S)
    if mermaid:
        h = h.replace(" </head>", MERMAID + " </head>")
    return h


def page_tail():
    return ["", "   </div>", "  </main>", "", '  <footer class="footer">',
            "    &copy; 2026 Randy Hollines. All Rights Reserved.", "    <br>",
            '    <a class="powered" href="https://github.com/objeck/objeck-lang" title="This site is served by Objeck\'s own web server">Powered by <img src="style/objeck-logo.png" alt="Objeck" height="20"></a>',
            "  </footer>", " </body>", "</html>", ""]


def build_page(tpl_head, md_rel, text, name, title_hint, desc, doc):
    srcdir = os.path.dirname(md_rel)
    t, toc, body, mermaid = render(text, srcdir, doc)
    t = t or title_hint
    plain_title = t.replace("`", "")
    lead = [b for b in body if b.startswith('<p class="page-lead">')]
    rest = [b for b in body if not b.startswith('<p class="page-lead">')]
    if doc:
        desc = (plain_text(lead[0]) if lead else desc)[:240] or desc
    page = [page_head(tpl_head, plain_title, desc, mermaid) + '  <main id="main" role="main" style="text-align: left;">',
            '   <div class="container">', ""]
    page.append('    <h1 class="page-title">%s</h1>' % inline(t, srcdir))
    if doc:
        page.append('    <div class="doc-meta"><a href="docs.html">&larr; All docs</a>'
                    '<a href="%s/blob/master/%s" target="_blank">%s on GitHub</a></div>' % (GH, md_rel, md_rel))
    page += ["    " + b for b in lead]
    if len(toc) > 3:
        page.append('    <div id="toc_container"%s>' % (' class="plain"' if doc else ""))
        page.append("     <h4>Contents</h4>")
        page.append("     <ul>")
        for sid, label in toc:
            page.append('      <li><a href="#%s">%s</a></li>' % (sid, label))
        page.append("     </ul>")
        page.append("    </div>")
    page.append("")
    for b in rest:
        page.append("    " + b.replace("\n", "\n    ") if not b.startswith("<pre") else b)
    return "\n".join(page + page_tail())


def docs_index(tpl_head):
    groups = []
    for md, (name, group, label, blurb) in DOCS.items():
        if group not in [g for g, _ in groups]:
            groups.append((group, []))
        dict(groups)[group].append((md, name, label, blurb))
    page = [page_head(tpl_head, "Documentation",
                      "Architecture, design notes, the language reference, performance and tooling for Objeck.")
            + '  <main id="main" role="main" style="text-align: left;">',
            '   <div class="container">', "",
            '    <h1 class="page-title">Documentation</h1>',
            '    <p class="page-lead">How Objeck works inside, the language and library reference, and the tools around it. '
            'New to the language? Start with the <a href="getting_started.html">Quick Start</a> and the '
            '<a href="guides.html">guides</a>; the <a href="https://www.objeck.org/api/latest/index.html" target="_blank">API reference</a> '
            'lists every class.</p>', ""]
    for group, entries in groups:
        page.append('    <h3 id="%s">%s</h3>' % (slug_guide(group), group))
        page.append('    <div class="boxes">')
        for md, name, label, blurb in entries:
            title = None
            with open(os.path.join(ROOT, md.replace("/", os.sep)), encoding="utf-8", newline="") as f:
                for ln in f.read().replace("\r\n", "\n").split("\n"):
                    if ln.startswith("# "):
                        title = ln[2:].strip()
                        break
            page.append('     <a class="box" href="%s"><span class="box-icon">%s</span><b>%s</b>'
                        '<span class="box-text">%s</span></a>'
                        % (name, label, html.escape(title or md, quote=False), html.escape(blurb, quote=False)))
        page.append("    </div>")
    return "\n".join(page + page_tail())


def read(path):
    with open(path, encoding="utf-8", newline="") as f:
        return f.read().replace("\r\n", "\n")


def main():
    check = "--check" in sys.argv
    stale = []
    tpl = read(os.path.join(WEB, "ai_guide.html"))
    head = tpl[:tpl.index('  <main id="main"')]
    pages = {}
    for md, (name, title, desc) in GUIDES.items():
        pages[name] = build_page(head, md, read(os.path.join(ROOT, md.replace("/", os.sep))), name, title, desc, False)
    for md, (name, group, label, blurb) in DOCS.items():
        pages[name] = build_page(head, md, read(os.path.join(ROOT, md.replace("/", os.sep))), name, md, blurb, True)
    pages["docs.html"] = docs_index(head)
    for name, text_out in pages.items():
        target = os.path.join(WEB, name)
        if check:
            have = read(target) if os.path.exists(target) else None
            if have != text_out:
                stale.append(name)
        else:
            with open(target, "w", encoding="utf-8", newline="\n") as f:
                f.write(text_out)
            print("wrote", name)
    if check:
        for name in stale:
            print("stale:", name)
        sys.exit(1 if stale else 0)


main()
