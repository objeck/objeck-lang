#!/usr/bin/env python3
"""Render docs/guides/*.md as the site pages docs/web/guide_*.html and docs/web/guides.html.

The Markdown in docs/guides is the source. The HTML follows the structure and styles of
docs/web/ai_guide.html: its head, menu and footer are copied from that page, so a change to the
menu there reaches the guides the next time this runs.

  python tools/docs/guides_to_html.py           write the pages
  python tools/docs/guides_to_html.py --check   exit 1 if a page on disk differs from what the
                                                Markdown renders to

It handles the Markdown the guides use: headings, paragraphs, fenced code ('text' blocks become
command blocks; 'ruby' and 'json' blocks are highlighted), tables, bullet and numbered lists,
block quotes, inline code, bold, italic and links. HTML comments are dropped. Relative links to
other guides become links to the matching page; links to anything else in the repository become
links to it on GitHub.
"""
import html
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
GUIDES = os.path.join(ROOT, "docs", "guides")
WEB = os.path.join(ROOT, "docs", "web")
GH = "https://github.com/objeck/objeck-lang"

# markdown file -> (html file, title, description)
PAGES = {
    "README.md": ("guides.html", "Getting-Started Guides",
                  "Short, task-oriented Objeck guides, each built around a complete program that was compiled and run."),
    "whats_new_10_3.md": ("guide_whats_new.html", "What is New in v2026.10.3",
                          "Upgrading to Objeck v2026.10.3: recompile your programs, HttpClient connection reuse, library Nil fixes, language server speedups."),
    "query.md": ("guide_query.html", "Query.Structured Guide",
                 "A database inside your program: typed in-memory tables, SQL joins, grouping, prepared statements and files."),
    "http.md": ("guide_http.html", "HTTP Client and Web Server Guide",
                "An Objeck web server, an HTTP client, and how v2026.10.3 reuses connections."),
    "data_and_text.md": ("guide_data.html", "JSON, XML and RegEx Guide",
                         "Parse JSON, XML and regular expressions, and what they do with missing or malformed input."),
    "collections.md": ("guide_collections.html", "Collections Guide",
                       "Hash, Map, Vector, Set and MultiHash in one short program."),
    "ai.md": ("guide_ai.html", "Game AI Guide",
              "Minimax and Monte Carlo tree search with System.AI, on tic-tac-toe, and the checkers example."),
    "language_server.md": ("guide_editors.html", "Editor and Language Server Guide",
                           "Install the Objeck language server for VS Code and other editors, and debug a program."),
}
SIBLING = {md: v[0] for md, v in PAGES.items()}


def slug(text):
    s = re.sub(r"<[^>]+>", "", text).lower()
    s = re.sub(r"[^a-z0-9]+", "-", s).strip("-")
    return s or "section"


def fix_link(url):
    if url.startswith(("http://", "https://", "#", "mailto:")):
        return url
    path, _, frag = url.partition("#")
    frag = "#" + frag if frag else ""
    if path in SIBLING:
        return SIBLING[path] + frag
    full = os.path.normpath(os.path.join("docs", "guides", path)).replace("\\", "/")
    real = os.path.join(ROOT, full.replace("/", os.sep))
    kind = "tree" if (path.endswith("/") or os.path.isdir(real)) else "blob"
    return "%s/%s/master/%s%s" % (GH, kind, full.rstrip("/"), frag)


def inline(text):
    spans = []

    def stash(m):
        spans.append("<code>%s</code>" % html.escape(m.group(1), quote=False))
        return "\x00%d\x00" % (len(spans) - 1)

    text = re.sub(r"`([^`]+)`", stash, text)
    text = html.escape(text, quote=False)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"(?<![\w*])\*([^*\s][^*]*)\*(?![\w*])", r"<em>\1</em>", text)

    def link(m):
        label, url = m.group(1), m.group(2)
        url = fix_link(html.unescape(url))
        ext = ' target="_blank"' if url.startswith("http") and "objeck.org" not in url else ""
        return '<a href="%s"%s>%s</a>' % (url, ext, label)

    text = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", link, text)
    text = re.sub(r"&lt;(https?://[^&\s]+)&gt;",
                  lambda m: '<a href="%s" target="_blank">%s</a>' % (m.group(1), m.group(1)), text)
    text = re.sub(r"\x00(\d+)\x00", lambda m: spans[int(m.group(1))], text)
    return text


def render(md_text):
    lines = md_text.split("\n")
    out, toc = [], []
    i = 0
    title = None
    lead_done = False
    while i < len(lines):
        ln = lines[i]
        if ln.startswith("<!--"):
            i += 1
            continue
        if ln.startswith("```"):
            lang = ln[3:].strip()
            i += 1
            buf = []
            while not lines[i].startswith("```"):
                buf.append(lines[i])
                i += 1
            i += 1
            body = html.escape("\n".join(buf), quote=False)
            if lang == "text":
                out.append('<pre class="cmd">%s</pre>' % body)
            else:
                cl = {"ruby": "cpp", "json": "json"}.get(lang, "cpp")
                out.append('<pre class="line-numbers"><code class="language-%s">%s\n</code></pre>' % (cl, body))
            continue
        if ln.startswith("# "):
            title = ln[2:].strip()
            i += 1
            continue
        m = re.match(r"(#{2,3}) (.*)", ln)
        if m:
            lvl, txt = len(m.group(1)), m.group(2).strip()
            sid = slug(txt)
            if lvl == 2:
                toc.append((sid, inline(txt)))
                out.append('<h3 id="%s">%s</h3>' % (sid, inline(txt)))
            else:
                out.append('<h4 id="%s">%s</h4>' % (sid, inline(txt)))
            i += 1
            continue
        if ln.startswith(">"):
            buf = []
            while i < len(lines) and lines[i].startswith(">"):
                buf.append(lines[i].lstrip("> ").rstrip())
                i += 1
            out.append('<div class="note">%s</div>' % inline(" ".join(buf)))
            continue
        if ln.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                i += 1
            head, body = rows[0], rows[2:]
            t = ['<table class="ref-table">', " <thead>",
                 "  <tr>" + "".join("<th>%s</th>" % inline(c) for c in head) + "</tr>", " </thead>", " <tbody>"]
            for r in body:
                t.append("  <tr>" + "".join("<td>%s</td>" % inline(c) for c in r) + "</tr>")
            t += [" </tbody>", "</table>"]
            out.append("\n".join(t))
            continue
        if re.match(r"\s*[-*] ", ln) or re.match(r"\s*\d+\. ", ln):
            ordered = bool(re.match(r"\s*\d+\. ", ln))
            items = []
            while i < len(lines) and (re.match(r"\s*[-*] ", lines[i]) or re.match(r"\s*\d+\. ", lines[i])
                                      or (lines[i].startswith("  ") and lines[i].strip() and items)):
                if re.match(r"\s*[-*] ", lines[i]) or re.match(r"\s*\d+\. ", lines[i]):
                    items.append(re.sub(r"^\s*([-*]|\d+\.) ", "", lines[i]))
                else:
                    items[-1] += " " + lines[i].strip()
                i += 1
            tag = "ol" if ordered else "ul"
            out.append("<%s>\n%s\n</%s>" % (tag, "\n".join(" <li>%s</li>" % inline(x) for x in items), tag))
            continue
        if not ln.strip():
            i += 1
            continue
        buf = []
        while (i < len(lines) and lines[i].strip() and not lines[i].startswith(("```", "#", ">", "|", "<!--"))
               and not re.match(r"\s*([-*]|\d+\.) ", lines[i])):
            buf.append(lines[i].strip())
            i += 1
        para = inline(" ".join(buf))
        if not lead_done:
            out.append('<p class="page-lead">%s</p>' % para)
            lead_done = True
        else:
            out.append("<p>%s</p>" % para)
    return title, toc, out


def main():
    check = "--check" in sys.argv
    stale = []
    tpl = open(os.path.join(WEB, "ai_guide.html"), encoding="utf-8", newline="").read().replace("\r\n", "\n")
    head = tpl[:tpl.index('  <main id="main"')]
    for md, (name, title, desc) in PAGES.items():
        text = open(os.path.join(GUIDES, md), encoding="utf-8", newline="").read().replace("\r\n", "\n")
        t, toc, body = render(text)
        h = re.sub(r"<title>.*?</title>", "<title>%s — Objeck Language</title>" % html.escape(title, quote=False),
                   head, flags=re.S)
        h = re.sub(r'<meta name="description" content=".*?"/>',
                   '<meta name="description" content="%s"/>' % html.escape(desc), h, flags=re.S)
        page = [h + '  <main id="main" role="main" style="text-align: left;">', '   <div class="container">', ""]
        page.append('    <h1 class="page-title">%s</h1>' % html.escape(t, quote=False))
        lead = [b for b in body if b.startswith('<p class="page-lead">')]
        rest = [b for b in body if not b.startswith('<p class="page-lead">')]
        page += ["    " + b for b in lead]
        if len(toc) > 3:
            page.append('    <div id="toc_container">')
            page.append("     <h4>Contents</h4>")
            page.append("     <ul>")
            for sid, label in toc:
                page.append('      <li><a href="#%s">%s</a></li>' % (sid, label))
            page.append("     </ul>")
            page.append("    </div>")
        page.append("")
        for b in rest:
            page.append("    " + b.replace("\n", "\n    ") if not b.startswith("<pre") else b)
        page += ["", "   </div>", "  </main>", "", '  <footer class="footer">',
                 "    &copy; 2026 Randy Hollines. All Rights Reserved.", "  </footer>", " </body>", "</html>", ""]
        text_out = "\n".join(page)
        target = os.path.join(WEB, name)
        if check:
            have = None
            if os.path.exists(target):
                have = open(target, encoding="utf-8", newline="").read()
            if have != text_out:
                stale.append(name)
        else:
            open(target, "w", encoding="utf-8", newline="\n").write(text_out)
            print("wrote", name)
    if check:
        for name in stale:
            print("stale:", name)
        sys.exit(1 if stale else 0)


main()
