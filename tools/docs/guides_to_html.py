#!/usr/bin/env python3
"""Render the Markdown the site links to as pages in the site's theme, and keep the site's chrome in one place.

Source files that become pages in docs/web:

  * docs/guides/*.md     ->  guides.html and guide_*.html (the getting-started guides)
  * the files in DOCS    ->  docs.html (an index) and doc_*.html (architecture, design notes,
                             the language reference, performance, editors and so on)
  * the files in SITE    ->  start.html, learn.html and community.html (docs/site/*.md)
  * core/compiler/build_libs.sh + lib_src/*.obs + the LIBS table below  ->  libraries.html

The head, the top navigation, the section list, the breadcrumbs and the footer are written by this script
(see CHROME below). The hand-written pages (index.html, getting_started.html, ai_guide.html, readme.html)
keep their own <main> content; this script rewrites everything around it, and fills the marked regions
(<!-- version:start --> ... <!-- version:end --> and <!-- release:start --> ... <!-- release:end -->) from
core/shared/version.h and CHANGELOG.md. A change to the navigation or footer here reaches every page the next time it runs.

  python tools/docs/guides_to_html.py           write the pages
  python tools/docs/guides_to_html.py --check   exit 1 if a page on disk differs from what the
                                                sources render to (it also fails when a library in
                                                lib_src has no description in LIBS, or the reverse)

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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import objeck_highlight  # noqa: E402  (the Objeck and JSON highlighter; word lists come from the VS Code grammar)

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
WEB = os.path.join(ROOT, "docs", "web")
GH = "https://github.com/objeck/objeck-lang"
RAW = "https://raw.githubusercontent.com/objeck/objeck-lang/master"

# repo-relative Markdown file -> (html file, short title, description)
GUIDES = {
    "docs/guides/README.md": ("guides.html", "Getting-Started Guides",
                              "Short, task-oriented Objeck guides, each built around a complete program that was compiled and run."),
    "docs/guides/whats_new_11_4.md": ("guide_whats_new_11_4.html", "What is New in v2026.11.4",
                                      "MCP tools from a doc comment, typed clients, agents on a local model, run control, and what else changed in v2026.11.4."),
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
    'docs/MCP_WALKTHROUGH.md': ('doc_mcp.html', 'Language and libraries', 'MC',
        'Declare a function as an MCP tool, serve it, call it through a typed client and hand it to an agent.'),
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
    'docs/INSTRUCTION_SET.md': ('doc_instruction_set.html', 'Contributing', 'IS',
        'The bytecode instruction table: one description of every opcode, a checker for the sites that restate it, and how to add an opcode.'),
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
LANG_CLASS = {"ruby": "objeck", "objeck": "objeck", "json": "json", "elisp": "lisp", "vim": "vim", "": ""}


def highlight(source, lang, escaped):
    """Highlighted HTML for a code block; `escaped` is the plain escaped text for a language with no highlighter."""
    if lang == "objeck":
        return objeck_highlight.highlight_objeck(source)
    if lang == "json":
        return objeck_highlight.highlight_json(source)
    return escaped


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
    if rel.startswith("docs/web/") and rel.endswith(".html"):
        return rel[len("docs/web/"):] + frag
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


def image_dims(url, srcdir):
    """(width, height) of a repository image, or None: lets a lazy image reserve its space."""
    if url.startswith(("http://", "https://", "data:")):
        return None
    path = os.path.join(ROOT, os.path.normpath(os.path.join(srcdir, url)))
    try:
        with open(path, "rb") as f:
            head = f.read(65536)
    except OSError:
        return None
    if head[:8] == b"\x89PNG\r\n\x1a\n" and len(head) >= 24:
        return int.from_bytes(head[16:20], "big"), int.from_bytes(head[20:24], "big")
    m = re.search(rb"<svg\b[^>]*>", head)
    if m:
        tag = m.group(0)
        w = re.search(rb"\swidth=\"([0-9.]+)\"", tag)
        h = re.search(rb"\sheight=\"([0-9.]+)\"", tag)
        if w and h:
            return int(float(w.group(1))), int(float(h.group(1)))
        v = re.search(rb"viewBox=\"[0-9.\-]+[ ,]+[0-9.\-]+[ ,]+([0-9.]+)[ ,]+([0-9.]+)\"", tag)
        if v:
            return int(float(v.group(1))), int(float(v.group(2)))
    return None


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
        url = html.unescape(m.group(2))
        dims = image_dims(url, srcdir)
        size = ' width="%d" height="%d"' % dims if dims else ''
        return '<img src="%s" alt="%s"%s loading="lazy">' % (fix_image(url, srcdir), m.group(1), size)

    text = re.sub(r"!\[([^\]]*)\]\(([^)\s]+)\)", image, text)

    def link(m):
        label, url = m.group(1), m.group(2)
        url = fix_link(html.unescape(url), srcdir)
        ext = ' target="_blank" rel="noopener"' if url.startswith("http") and "objeck.org" not in url else ""
        return '<a href="%s"%s>%s</a>' % (url, ext, label)

    text = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", link, text)
    text = re.sub(r"&lt;(https?://[^&\s]+)&gt;",
                  lambda m: '<a href="%s" target="_blank" rel="noopener">%s</a>' % (m.group(1), m.group(1)), text)
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
                cl = LANG_CLASS.get(lang, lang or "objeck")
                out.append('<pre class="line-numbers"><code class="language-%s">%s\n</code></pre>' % (cl, highlight("\n".join(buf), cl, body)))
            continue
        if ln.startswith("# "):
            if title is None:
                title = ln[2:].strip()
                i += 1
                continue
            txt = ln[2:].strip()
            sid = unique(slug(txt))
            toc.append((sid, inline(txt, srcdir)))
            out.append('<h2 id="%s">%s</h2>' % (sid, inline(txt, srcdir)))
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
                out.append('<h2 id="%s">%s</h2>' % (sid, inline(txt, srcdir)))
            elif lvl == 3:
                out.append('<h3 id="%s">%s</h3>' % (sid, inline(txt, srcdir)))
            elif lvl == 4:
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


# ---------------------------------------------------------------------------------------------------
# Pages written from Markdown in docs/site: repo-relative Markdown -> (html file, title, description)
SITE = {
    "docs/site/start.md": ("start.html", "Get started",
                           "Install Objeck on Windows, macOS or Linux, write and run a first program, or skip the install and use the browser playground."),
    "docs/site/learn.md": ("learn.html", "Learn",
                           "Where to learn Objeck: the quick start, the task-oriented guides, the AI and ML guide, the language reference and worked examples."),
    "docs/site/community.md": ("community.html", "Community",
                               "Ask a question, report a bug, send a pull request, run the tests: how to take part in Objeck."),
}
for _md, _v in SITE.items():
    PAGE_OF[_md] = _v[0]

# Pages written by hand: their <main> content is kept, the chrome around it is rewritten.
HAND = ("index.html", "getting_started.html", "ai_guide.html", "readme.html")

API_URL = "https://www.objeck.org/api/latest/index.html"
PLAYGROUND = "https://playground.objeck.org"

# top navigation: (label, page, key)
NAV = [("Get started", "start.html", "start"), ("Learn", "learn.html", "learn"), ("Docs", "docs.html", "docs"),
       ("Libraries", "libraries.html", "libraries"), ("Community", "community.html", "community")]

FOOTER_LINKS = [("Playground", PLAYGROUND), ("Download", GH + "/releases/latest"), ("API reference", API_URL),
                ("AI &amp; ML guide", "ai_guide.html"), ("Changelog", "readme.html"), ("GitHub", GH),
                ("Discussions", GH + "/discussions"), ("Issues", GH + "/issues"),
                ("Code of conduct", GH + "/blob/master/CODE_OF_CONDUCT.md"), ("License", GH + "/blob/master/LICENSE")]

# ---------------------------------------------------------------------------------------------------
# The library list. build_libs.sh says which .obs files make which .obl (the name `-lib` takes); this table says what each is.
# The script stops when a library is missing here or listed here and gone from the tree, so the page cannot drift.
LIB_GROUPS = ["Core", "Data and text", "Web and networking", "LLMs, agents and inference",
              "Machine learning and statistics", "Graphics, games and media"]
LIBS = {
    "lang": (0, "The core classes: console, files, sockets, reflection, threads and time. Always loaded; never passed to <code>-lib</code>."),
    "gen_collect": (0, "Generic collections: Vector, Map, Hash, Set and the rest, plus tuples. Also always loaded."),
    "concurrent": (0, "Structured concurrency (<code>TaskScope</code>, <code>Task</code>) and a runtime <code>Monitor</code>."),
    "actor": (0, "Actors with supervision, the base for programs made of cooperating agents."),
    "math": (0, "Arbitrary-precision and exact arithmetic."),
    "regex": (0, "Regular expressions: matching, replacing, anchors, groups."),
    "cipher": (0, "SHA and MD5 hashes, HMAC, AES-256 encryption and Base64."),
    "misc": (0, "Date parsing and formatting, and network time."),
    "diags": (0, "Static analysis behind the language server: symbols, diagnostics and hover information."),
    "json": (1, "Parse and build JSON, with a schema layer for typed access."),
    "json_stream": (1, "A streaming JSON parser for documents too large to hold as a tree."),
    "json_rpc": (1, "JSON-RPC 2.0 client and server."),
    "xml": (1, "Parse and build XML."),
    "csv": (1, "Read and write delimited text (RFC 4180)."),
    "query": (1, "An in-memory, column-oriented SQL engine over typed tables, with joins, grouping and indexes; reads CSV, JSON, XML and archives."),
    "query_ml": (1, "Model evaluation results as <code>Query.Structured</code> tables."),
    "query_odbc": (1, "A table or a query in a real database, read as a table."),
    "query_server": (1, "Publish tables over HTTP, read-only, with a SQL endpoint."),
    "query_vision": (1, "A folder of images, and what a detector found in them, as tables."),
    "odbc": (1, "ODBC database access: connections, statements, result sets."),
    "rss": (2, "An RSS 2.0 feed reader."),
    "net": (2, "HTTP/1.1 and HTTPS clients, URL and response types, and a downloader."),
    "net_h2": (2, "An HTTP/2 client with multiplexed streams."),
    "net_quic": (2, "An HTTP/3 (QUIC) client."),
    "net_server": (2, "An HTTP server framework: routes, static files, an MCP transport and OAuth helpers."),
    "web_server": (2, "A small embedded web server with typed access to the request."),
    "openai": (3, "OpenAI chat, responses, audio, embeddings, vision and more."),
    "gemini": (3, "Google Gemini client, including tuning and corpora."),
    "anthropic": (3, "Anthropic Messages API client."),
    "ollama": (3, "Ollama client, for models that run on your own machine."),
    "llm_util": (3, "Prompt templates, token estimates, context windows and JSON Schema validation for model calls."),
    "agent": (3, "A provider-neutral tool-use loop: an agent that calls the functions you give it."),
    "actor_agents": (3, "Run an agent as a supervised actor."),
    "mcp": (3, "Model Context Protocol server and client."),
    "mcp_agent": (3, "Declared MCP tools and servers as agent tools, with approval by effect."),
    "models": (3, "Backend-neutral inference types shared by the model libraries."),
    "inference": (3, "A client for HTTP model-inference servers."),
    "onnx": (3, "ONNX Runtime inference for pre-trained models."),
    "opencv": (3, "OpenCV computer vision: load, transform and detect in images."),
    "ai_hub": (3, "Download models from the Hugging Face Hub into a local cache."),
    "rag": (3, "Retrieval-augmented generation: chunking, retrieval and grounded prompts."),
    "rag_adapters": (3, "Adapters that connect the RAG kit to the embedding and chat libraries."),
    "ml": (4, "Matrices and neural networks, linear models, trees and forests, naive Bayes, nearest neighbours, clustering, SVMs, outlier detection and preprocessing."),
    "ml_boost": (4, "Gradient boosting for regression and classification."),
    "ml_lab": (4, "Experiment support: splits, resampling and parameter sweeps."),
    "ml_report": (4, "Replicate statistics and a report builder for a study."),
    "ml_timeseries": (4, "Time series: transforms, forecasters, ARIMA and forecast evaluation."),
    "ml_vector": (4, "A local vector index: store embeddings, ask for the nearest."),
    "ml_verify": (4, "Verification metrics (FAR, FRR, EER) for match decisions."),
    "ml_clip": (4, "CLIP image embeddings and face-pair scoring."),
    "ml_cnn": (4, "Convolutional neural networks on the CPU."),
    "nlp": (4, "Tokenization, stemming, TF-IDF, similarity and sentiment."),
    "nlp_onnx": (4, "Sentence embeddings from BERT-family models exported to ONNX."),
    "nlp_topics": (4, "Topic modeling (LDA), quality metrics and an SVG topic report."),
    "ai": (4, "Classic AI: graph search, minimax and Monte Carlo tree search, optimization, reinforcement learning."),
    "stats": (4, "Descriptive statistics, distributions, t-tests and an OLS summary."),
    "plot": (4, "Scatter, histogram, line and bar charts as standalone SVG."),
    "plot_sdl": (4, "An SDL2 window for <code>System.Plot</code>, and PNG export."),
    "telemetry": (4, "Per-model, per-device inference timing and resource sampling."),
    "sdl2": (5, "SDL2: 2D graphics, input, audio and windows."),
    "sdl_game": (5, "A 2D game framework: sprites, collisions, tilemaps, camera and scenes."),
    "sdl_gl": (5, "OpenGL 3.3 core rendering on top of SDL2."),
    "lame": (5, "MP3 encoding with LAME."),
}


def lib_catalog():
    """{obl name: [lib_src files]} from build_libs.sh, plus lang. Exits on a mismatch with lib_src or LIBS."""
    cat = {"lang": ["lang.obs"]}
    with open(os.path.join(ROOT, "core", "compiler", "build_libs.sh"), encoding="utf-8") as f:
        for ln in f:
            m = re.search(r"-src\s+(\S+)\s.*-dest\s+\.\./lib/(\w+)\.obl", ln)
            if m:
                cat[m.group(2)] = [os.path.basename(s) for s in m.group(1).split(",")]
    src = os.path.join(ROOT, "core", "compiler", "lib_src")
    on_disk = set(n for n in os.listdir(src) if n.endswith(".obs"))
    built = set(n for files in cat.values() for n in files)
    problems = []
    for n in sorted(on_disk - built):
        problems.append("lib_src/%s is in no library of build_libs.sh" % n)
    for n in sorted(built - on_disk):
        problems.append("build_libs.sh names lib_src/%s, which does not exist" % n)
    for n in sorted(set(cat) - set(LIBS)):
        problems.append("library '%s' has no description in LIBS (tools/docs/guides_to_html.py)" % n)
    for n in sorted(set(LIBS) - set(cat)):
        problems.append("LIBS describes '%s', which build_libs.sh no longer builds" % n)
    if problems:
        sys.exit("libraries.html: " + "; ".join(problems))
    return cat


def lib_facts(files):
    """(sorted bundle names, experimental?) read from a library's source files."""
    bundles, experimental = set(), False
    for n in files:
        with open(os.path.join(ROOT, "core", "compiler", "lib_src", n), encoding="utf-8-sig", errors="replace") as f:
            text = f.read()
        bundles.update(re.findall(r"^\s*bundle\s+([A-Za-z][\w.]*)", text, re.M))
        if re.search(r"Experimental: API may change", text):
            experimental = True
    return sorted(bundles), experimental


def libraries_body():
    cat = lib_catalog()
    out = []
    toc = []
    for gi, gname in enumerate(LIB_GROUPS):
        names = sorted((n for n in cat if LIBS[n][0] == gi), key=lambda n: (n not in ("lang", "gen_collect"), n))
        sid = slug_guide(gname)
        toc.append((sid, html.escape(gname, quote=False)))
        out.append('<h2 id="%s">%s</h2>' % (sid, html.escape(gname, quote=False)))
        out.append('<table class="ref-table lib-table">\n <thead><tr><th>Library</th><th>What it gives you</th><th>Namespaces</th></tr></thead>\n <tbody>')
        for n in names:
            bundles, exp = lib_facts(cat[n])
            label = "<code>%s</code>" % n if n in ("lang", "gen_collect") else "<code>-lib %s</code>" % n
            desc = LIBS[n][1] + (' <em>Experimental: the API may change.</em>' if exp else "")
            ns = ", ".join("<code>%s</code>" % b for b in bundles)
            out.append('  <tr><td data-label="Library">%s</td><td data-label="What it gives you">%s</td><td data-label="Namespaces" class="ns">%s</td></tr>' % (label, desc, ns))
        out.append(" </tbody>\n</table>")
    lead = ('<p class="page-lead">%d libraries, generated from <code>core/compiler/build_libs.sh</code> and the sources. '
            'Pass them to <code>obc -lib</code>; a library\'s own dependencies are not added for you. '
            '<a href="%s" target="_blank" rel="noopener">API reference</a>.</p>' % (len(cat), API_URL))
    how = ['<pre class="cmd">obc -src app.obs -lib json,net,cipher</pre>']
    shown = []
    for ex, label in (("json", "JSON"), ("query", "SQL"), ("ml", "Machine learning"), ("threads", "Threads")):
        shown.append("<h3>%s</h3>" % label + example_html(ex).strip())
    toc.insert(0, ("in-practice", "In practice"))
    return "Libraries", toc, [lead] + how + ['<h2 id="in-practice">In practice</h2>'] + shown + out


# ---------------------------------------------------------------------------------------------------
# facts read from the repository
def read(path):
    with open(path, encoding="utf-8", newline="") as f:
        return f.read().replace("\r\n", "\n")


def project_version():
    m = re.search(r'VERSION_STRING\s+L?"([^"]+)"', read(os.path.join(ROOT, "core", "shared", "version.h")))
    if not m:
        sys.exit("cannot read VERSION_STRING from core/shared/version.h")
    return m.group(1)


def latest_release():
    """(version, date, [(section, highlight title)]) from the newest section of CHANGELOG.md."""
    text = read(os.path.join(ROOT, "CHANGELOG.md"))
    m = re.search(r"^## \[v?([\d.]+)\] - (\d{4}-\d{2}-\d{2})\s*$(.*?)(?=^## \[)", text, re.M | re.S)
    if not m:
        sys.exit("cannot read the newest release from CHANGELOG.md")
    picks = []
    for sect in ("Performance", "Security", "Changed", "Added", "Fixed"):
        sm = re.search(r"^### %s\s*$(.*?)(?=^### |\Z)" % sect, m.group(3), re.M | re.S)
        if not sm:
            continue
        b = re.search(r"^- \*\*(.+?)\*\*", sm.group(1), re.M)
        if b:
            title = re.sub(r"`([^`]*)`", r"\1", b.group(1)).strip().rstrip(".:")
            picks.append((sect, title))
        if len(picks) == 3:
            break
    return m.group(1), m.group(2), picks


def long_date(iso):
    months = ["January", "February", "March", "April", "May", "June", "July", "August", "September",
              "October", "November", "December"]
    y, mo, d = iso.split("-")
    return "%d %s %s" % (int(d), months[int(mo) - 1], y)


def example_files(name):
    """(source, transcript) of a tested sample in docs/site/examples (see tools/cicd/check_site_examples.py)."""
    base = os.path.join(ROOT, "docs", "site", "examples", name)
    return read(base + ".obs").strip("\n"), read(base + ".out").strip("\n")


def example_md(name):
    src, out = example_files(name)
    return "```objeck\n%s\n```\n\n```text\n%s\n```" % (src, out)


def example_html(name):
    src, out = example_files(name)
    return ('\n      <pre aria-label="Objeck example program"><code class="language-objeck">%s</code></pre>\n'
            '      <pre class="cmd example-out">%s</pre>\n      ' % (objeck_highlight.highlight_objeck(src), esc(out)))


CODE_BLOCK = re.compile(r'(<code class="language-)(cpp|objeck|ruby|json)(">)(.*?)(</code>)', re.S)


def rehighlight(text):
    """Highlight the code blocks of a hand-written page; safe to run again on its own output."""
    def one(m):
        src = html.unescape(re.sub(r"<[^>]+>", "", m.group(4)))
        lang = "json" if m.group(2) == "json" else "objeck"
        return m.group(1) + lang + m.group(3) + highlight(src, lang, "") + m.group(5)
    return CODE_BLOCK.sub(one, text)


def fill(text, name, value):
    pat = re.compile(r"(<!-- %s:start -->).*?(<!-- %s:end -->)" % (name, name), re.S)
    if not pat.search(text):
        return text
    return pat.sub(lambda m: m.group(1) + value + m.group(2), text)


# ---------------------------------------------------------------------------------------------------
# CHROME: head, header and navigation, breadcrumbs, section list, footer
MERMAID = ('  <script type="module">\n'
           "    import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@11.4.1/dist/mermaid.esm.min.mjs';\n"
           "    var dark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;\n"
           "    mermaid.initialize({ startOnLoad: false, theme: dark ? 'dark' : 'neutral', securityLevel: 'loose' });\n"
           "    await mermaid.run({ querySelector: 'pre.mermaid', suppressErrors: true });\n"
           "  </script>\n")

_TITLES = {}


def md_title(md):
    if md not in _TITLES:
        title = md
        for ln in read(os.path.join(ROOT, md.replace("/", os.sep))).split("\n"):
            if ln.startswith("# "):
                title = ln[2:].strip().replace("`", "")
                break
        _TITLES[md] = title
    return _TITLES[md]


def esc(s):
    return html.escape(s, quote=False)


def plain_text(para_html):
    t = re.sub(r"<[^>]+>", "", para_html)
    return html.unescape(t).strip()


def page_head(title, desc, mermaid=False):
    h = ['<!DOCTYPE html>', '<html lang="en">', ' <head>', '  <meta charset="utf-8">',
         '  <meta name="viewport" content="width=device-width, initial-scale=1">',
         '  <title>%s</title>' % title,
         '  <meta name="description" content="%s">' % html.escape(desc),
         '  <meta name="color-scheme" content="dark light">',
         '  <link rel="icon" href="style/favicon.ico" type="image/x-icon">',
         '  <link rel="stylesheet" href="style/site.css">',
         '  <script>document.documentElement.className = "js";</script>']
    if mermaid:
        h.append(MERMAID.rstrip("\n"))
    h += [' </head>', ' <body>']
    return "\n".join(h)


def site_header(active):
    items = []
    for label, page, key in NAV:
        cur = ' aria-current="page"' if key == active else ""
        items.append('     <li><a href="%s"%s>%s</a></li>' % (page, cur, label))
    return "\n".join([
        '  <a class="skip" href="#main">Skip to content</a>',
        '  <header class="site-header">',
        '   <div class="bar">',
        '    <a class="brand" href="index.html" aria-label="Objeck home"><img src="style/objeck-logo-sm.png" alt="Objeck" width="94" height="36"></a>',
        '    <button class="nav-toggle" type="button" aria-expanded="false" aria-controls="site-nav">Menu</button>',
        '    <nav class="site-nav" id="site-nav" aria-label="Main">',
        '     <ul>'] + items + [
        '     </ul>',
        '    </nav>',
        '    <div class="bar-actions">',
        '     <a class="link" href="%s">GitHub</a>' % GH,
        '     <a class="btn btn-primary btn-small" href="%s">Playground</a>' % PLAYGROUND,
        '    </div>',
        '   </div>',
        '  </header>'])


def site_footer():
    lis = "\n".join('     <li><a href="%s">%s</a></li>' % (u, l) for l, u in FOOTER_LINKS)
    return "\n".join([
        '  <footer class="site-footer">',
        '   <div class="wrap">',
        '    <nav aria-label="Footer"><ul class="footer-links">', lis, '    </ul></nav>',
        '    <div class="footer-base">',
        '     <p>&copy; 2026 Randy Hollines. All Rights Reserved.</p>',
        '     <a class="powered" href="%s" title="This site is served by Objeck\'s own web server">Powered by <img src="style/objeck-logo-sm.png" alt="Objeck" width="52" height="20"></a>' % GH,
        '    </div>',
        '   </div>',
        '  </footer>',
        '  <script src="style/site.js"></script>',
        ' </body>',
        '</html>', ''])


def crumbs(trail):
    """trail: [(label, href or None)]; Home is added."""
    items = ['<li><a href="index.html">Home</a></li>']
    for label, href in trail:
        if href:
            items.append('<li><a href="%s">%s</a></li>' % (href, label))
        else:
            items.append('<li aria-current="page">%s</li>' % label)
    return '   <nav class="crumbs" aria-label="Breadcrumb"><ol>%s</ol></nav>' % "".join(items)


def guide_short(title):
    return re.sub(r" Guide$", "", title).replace("What is New in", "What is new in")


def sidebar(area, current):
    """The section list: the pages that belong with `current`."""
    def link(label, page):
        cur = ' aria-current="page"' if page == current else ""
        return '<li><a href="%s"%s>%s</a></li>' % (page, cur, label)
    parts = []
    if area == "learn":
        parts.append("<p class=\"nav-title\">Start</p><ul>%s</ul>" % "".join(link(l, p) for l, p in [
            ("Get started", "start.html"), ("Learn overview", "learn.html"), ("Quick start tour", "getting_started.html")]))
        gl = [link("All guides", "guides.html")]
        for md, (page, title, _d) in GUIDES.items():
            if md != "docs/guides/README.md":
                gl.append(link(esc(guide_short(title)), page))
        parts.append("<p class=\"nav-title\">Guides</p><ul>%s</ul>" % "".join(gl))
        parts.append("<p class=\"nav-title\">AI and ML</p><ul>%s</ul>" % "".join(link(l, p) for l, p in [
            ("AI &amp; ML developer guide", "ai_guide.html"), ("Which models run", "doc_models.html"), ("AI examples", "doc_ai.html")]))
        label = "Learn"
    elif area == "docs":
        groups = []
        for md, (page, group, _l, _b) in DOCS.items():
            if group not in [g for g, _ in groups]:
                groups.append((group, []))
            dict(groups)[group].append((esc(md_title(md)) if page != "doc_changelog.html" else "Changelog", page))
        parts.append("<p class=\"nav-title\">Documentation</p><ul>%s</ul>" % link("All documentation", "docs.html"))
        for group, entries in groups:
            if group == "Releases":
                entries = [("Release notes and install", "readme.html")] + entries
            parts.append("<p class=\"nav-title\">%s</p><ul>%s</ul>" % (esc(group), "".join(link(l, p) for l, p in entries)))
        label = "Documentation"
    else:
        return ""
    return '   <nav class="sidebar" aria-label="%s sections">\n    %s\n   </nav>' % (label, "\n    ".join(parts))


def toc_nav(toc):
    lis = "".join('<li><a href="#%s">%s</a></li>' % (sid, label) for sid, label in toc)
    return '   <nav class="toc" aria-label="On this page">\n    <p class="nav-title">On this page</p>\n    <ul>%s</ul>\n   </nav>' % lis


def classify(name, group=None):
    """(nav key, sidebar area, breadcrumb trail before the page title) for a page."""
    if name == "start.html":
        return "start", "learn", []
    if name == "learn.html":
        return "learn", "learn", []
    if name in ("getting_started.html", "guides.html"):
        return "learn", "learn", [("Learn", "learn.html")]
    if name.startswith("guide_"):
        return "learn", "learn", [("Learn", "learn.html"), ("Guides", "guides.html")]
    if name == "ai_guide.html":
        return "ai", "learn", [("Learn", "learn.html")]
    if name == "libraries.html":
        return "libraries", None, []
    if name == "community.html":
        return "community", None, []
    if name == "docs.html":
        return "docs", "docs", []
    if name == "readme.html":
        return "docs", "docs", [("Docs", "docs.html")]
    if name.startswith("doc_"):
        trail = [("Docs", "docs.html")]
        if group:
            trail.append((esc(group), "docs.html#%s" % slug_guide(group)))
        return "docs", "docs", trail
    return "", None, []


def shell(name, title, desc, inner, toc=(), mermaid=False, group=None, wrap=True):
    """A whole page: chrome around `inner` (the <main> content)."""
    inner = "<!-- content:start -->\n%s\n<!-- content:end -->" % inner
    if name == "index.html":
        page = [page_head(title, desc), site_header(""), '  <main id="main" tabindex="-1">', inner, "  </main>", site_footer()]
        return "\n".join(page)
    active, area, trail = classify(name, group)
    side = sidebar(area, name) if area else ""
    tocn = toc_nav(list(toc)) if toc else ""
    page = [page_head("%s — Objeck Language" % title, desc, mermaid), site_header(active), '  <main id="main" tabindex="-1">',
            crumbs(trail + [(esc(title), None)])]
    cls = "layout" + (" has-side" if side else "") + (" has-toc" if tocn else "")
    page.append('  <div class="%s">' % cls)
    if side:
        page.append(side)
    page.append('   <div class="content">')
    page.append(inner)
    page.append("   </div>")
    if tocn:
        page.append(tocn)
    page.append("  </div>")
    page.append("  </main>")
    page.append(site_footer())
    return "\n".join(page)


# ---------------------------------------------------------------------------------------------------
def build_page(md_rel, text, name, title_hint, desc, doc, group=None, site=False, version=None):
    srcdir = os.path.dirname(md_rel)
    if site:
        text = text.replace("{{version}}", version)
        text = re.sub(r"\{\{example:(\w+)\}\}", lambda m: example_md(m.group(1)), text)
    t, toc, body, mermaid = render(text, srcdir, doc)
    t = t or title_hint
    plain_title = t.replace("`", "")
    lead = [b for b in body if b.startswith('<p class="page-lead">')]
    rest = [b for b in body if not b.startswith('<p class="page-lead">')]
    if doc or site:
        desc = (plain_text(lead[0]) if lead else desc)[:240] or desc
    inner = ['    <h1 class="page-title">%s</h1>' % inline(t, srcdir)]
    if doc:
        inner.append('    <p class="doc-meta"><a href="docs.html">All documentation</a>'
                     '<a href="%s/blob/master/%s" target="_blank" rel="noopener">View %s on GitHub</a></p>' % (GH, md_rel, md_rel))
    inner += ["    " + b for b in lead]
    inner.append("")
    for b in rest:
        inner.append("    " + b.replace("\n", "\n    ") if not b.startswith("<pre") else b)
    return shell(name, plain_title, desc, "\n".join(inner), toc if len(toc) > 2 else (), mermaid, group)


def docs_index():
    groups = []
    for md, (name, group, label, blurb) in DOCS.items():
        if group not in [g for g, _ in groups]:
            groups.append((group, []))
        dict(groups)[group].append((md, name, label, blurb))
    inner = ['    <h1 class="page-title">Documentation</h1>',
             '    <p class="page-lead">Architecture, design notes, reference and tools. New here? <a href="start.html">Get started</a>.</p>', ""]
    toc = []
    for group, entries in groups:
        toc.append((slug_guide(group), esc(group)))
        inner.append('    <h2 id="%s">%s</h2>' % (slug_guide(group), esc(group)))
        inner.append('    <div class="boxes">')
        for md, name, label, blurb in entries:
            inner.append('     <a class="box" href="%s"><b>%s</b><span class="box-text">%s</span></a>'
                         % (name, esc(md_title(md)), esc(blurb)))
        inner.append("    </div>")
    return shell("docs.html", "Documentation", "Architecture, design notes, the language reference, performance and tooling for Objeck.",
                 "\n".join(inner), toc)


def libraries_page():
    title, toc, body = libraries_body()
    inner = ['    <h1 class="page-title">%s</h1>' % title] + body
    return shell("libraries.html", title, "Every library that ships with Objeck, one line each, generated from the repository: collections, data, networking, LLM clients, ML, graphics.",
                 "\n".join(inner), toc)


# ---- hand-written pages: keep <main>, rewrite what is around it
def rechrome(name, text, version, release):
    title = re.search(r"<title>(.*?)</title>", text, re.S).group(1).strip()
    desc = re.search(r'<meta\s+name="description"\s+content="(.*?)"\s*/?>', text, re.S)
    desc = html.unescape(desc.group(1)) if desc else title
    marked = re.search(r"<!-- content:start -->\n(.*)\n<!-- content:end -->", text, re.S)
    inner = marked.group(1) if marked else re.search(r"<main\b[^>]*>(.*)</main>", text, re.S).group(1)
    inner = fill(inner, "version", version)
    inner = re.sub(r"Download v[\d.]+", "Download v" + version, inner)  # deploy_website.sh greps for this literal text
    for ex in re.findall(r"<!-- example:(\w+):start -->", inner):
        inner = fill(inner, "example:" + ex, example_html(ex))
    inner = rehighlight(inner)
    if name == "index.html":
        rv, rdate, picks = release
        strip = ['\n     <div class="release">',
                 '      <div><p class="tag">v%s</p><p class="date">Released %s</p></div>' % (rv, long_date(rdate)),
                 "      <ul>"]
        for sect, t in picks:
            strip.append("       <li><b>%s:</b> %s</li>" % (sect, esc(t)))
        strip += ["      </ul>",
                  '      <a class="btn btn-secondary" href="readme.html">Release notes</a>',
                  "     </div>\n     "]
        inner = fill(inner, "release", "\n".join(strip))
        return shell(name, re.sub(r"\s+", " ", title), desc, inner.strip("\n"))
    short = re.sub(r"\s*[—-]\s*Objeck Language\s*$", "", title)
    return shell(name, html.unescape(short), desc, inner.strip("\n"))


def main():
    check = "--check" in sys.argv
    stale = []
    version = project_version()
    release = latest_release()
    pages = {}
    for md, (name, title, desc) in GUIDES.items():
        pages[name] = build_page(md, read(os.path.join(ROOT, md.replace("/", os.sep))), name, title, desc, False)
    for md, (name, group, label, blurb) in DOCS.items():
        pages[name] = build_page(md, read(os.path.join(ROOT, md.replace("/", os.sep))), name, md, blurb, True, group)
    for md, (name, title, desc) in SITE.items():
        pages[name] = build_page(md, read(os.path.join(ROOT, md.replace("/", os.sep))), name, title, desc, False,
                                 site=True, version=version)
    pages["docs.html"] = docs_index()
    pages["libraries.html"] = libraries_page()
    for name in HAND:
        pages[name] = rechrome(name, read(os.path.join(WEB, name)), version, release)
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
