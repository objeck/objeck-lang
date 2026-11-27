#!/usr/bin/env python3
"""Objeck and JSON syntax highlighting at page-generation time: no JavaScript, no external file, nothing to flash.

The word lists are read from the VS Code grammar (tools/lsp/clients/vscode/syntaxes/objeck.tmLanguage.json), so a
keyword added for the editors reaches the site the next time the pages are generated.

  highlight_objeck(text) -> HTML: the text escaped, with <span class="t-..."> around tokens
  highlight_json(text)   -> the same for JSON

Token classes: t-com comment (line, and #~ ... ~# blocks), t-str string and char, t-num number, t-kw control keyword,
t-mod modifier (public, static, native...), t-type built-in type, t-const Nil/true/false, t-cls a class, interface or
enum name, t-fn a function or method name at its declaration, t-op operator (:=  ->  =>  ~).
"""
import html
import json
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
GRAMMAR = os.path.join(ROOT, "tools", "lsp", "clients", "vscode", "syntaxes", "objeck.tmLanguage.json")


def _words(repo, name_part):
    """The alternation words of every rule in the grammar whose scope name contains name_part."""
    out = set()
    for rule in repo.values():
        for pat in rule.get("patterns", []):
            if name_part in pat.get("name", ""):
                m = re.match(r"\\b\(([\w|]+)\)\\b$", pat.get("match", ""))
                if m:
                    out.update(m.group(1).split("|"))
    return out


def _contextual(repo):
    """word -> what must follow it, for the grammar's contextual keywords: rules written `\\bword(?=...)`."""
    out = {}
    for rule in repo.values():
        for pat in rule.get("patterns", []):
            if "keyword." in pat.get("name", ""):
                m = re.match(r"\\b(\w+)(\(\?=.*\))$", pat.get("match", ""))
                if m:
                    out[m.group(1)] = re.compile(m.group(2))
    return out


def _load():
    with open(GRAMMAR, encoding="utf-8") as f:
        repo = json.load(f)["repository"]
    global CONTEXTUAL
    CONTEXTUAL = _contextual(repo)
    kw = _words(repo, "keyword.") | _words(repo, "support.function")
    mod = _words(repo, "storage.modifier")
    typ = _words(repo, "storage.type")
    const = _words(repo, "constant.numeric.boolean") | {"Nil"}
    typ -= const
    kw -= mod
    return kw, mod, typ, const


CONTEXTUAL = {}
KEYWORDS, MODIFIERS, TYPES, CONSTS = _load()
DECL = {"class", "interface", "enum", "bundle", "alias", "consts", "from", "implements"}

TOKEN = re.compile(r"""
    (?P<com>\#~.*?~\#|\#[^\n]*)
  | (?P<str>"(?:[^"\\\n]|\\.)*"|'(?:[^'\\\n]|\\.){1,4}')
  | (?P<num>\b0[xX][0-9a-fA-F_]+\b|\b\d[\d_]*(?:\.\d+)?(?:[eE][+-]?\d+)?\b)
  | (?P<id>[A-Za-z_][A-Za-z_0-9]*)
  | (?P<op>:=|->|=>|~|<>)
""", re.X | re.S)


def _span(cls, text):
    return '<span class="t-%s">%s</span>' % (cls, html.escape(text, quote=False))


def highlight_objeck(text):
    out, last = [], 0
    prev = ""          # previous significant word, for declarations
    colon_decl = False  # after `function :` or `method : ... :`
    for m in TOKEN.finditer(text):
        out.append(html.escape(text[last:m.start()], quote=False))
        last = m.end()
        kind, tok = m.lastgroup, m.group()
        if kind == "id":
            if tok in CONSTS:
                out.append(_span("const", tok))
            elif tok in MODIFIERS:
                out.append(_span("mod", tok))
            elif tok in TYPES:
                out.append(_span("type", tok))
            elif tok in KEYWORDS:
                out.append(_span("kw", tok))
            elif tok in CONTEXTUAL and CONTEXTUAL[tok].match(text, last):
                # a keyword only where the grammar expects it, an ordinary name elsewhere
                out.append(_span("kw", tok))
            elif tok in ("class", "interface", "enum", "bundle", "alias", "consts", "method", "function"):
                out.append(_span("kw", tok))
            elif tok == "prompt" and re.match(r'\s*"', text[last:last + 16]):
                # a prompt literal: 'prompt' is a keyword only directly before a string
                out.append(_span("kw", tok))
            elif (tok == "agent" and re.match(r"\s+[A-Za-z_]\w*\s*[({]", text[last:last + 120]) and
                  re.search(r"(^|\n)[ \t]*$", text[:m.start()])):
                # an agent declaration: 'agent' is a keyword only at the start of one; the name after it is a class
                out.append(_span("kw", tok))
                prev = "class"
                continue
            elif prev in DECL or (tok[:1].isupper() and re.match(r"\s*(->|\.)", text[last:last + 4])):
                out.append(_span("cls", tok))
            elif (prev in MODIFIERS or prev in ("method", "function")) and re.match(r"\s*\(", text[last:last + 4]):
                out.append(_span("fn", tok))
            else:
                out.append(html.escape(tok, quote=False))
            prev = tok
        elif kind == "op":
            out.append(_span("op", tok))
            prev = ""
        else:
            out.append(_span(kind, tok))
            prev = ""
    out.append(html.escape(text[last:], quote=False))
    return "".join(out)


_JSON = re.compile(r'(?P<str>"(?:[^"\\\n]|\\.)*")|(?P<num>-?\b\d+(?:\.\d+)?(?:[eE][+-]?\d+)?\b)|\b(?P<const>true|false|null)\b')


def highlight_json(text):
    out, last = [], 0
    for m in _JSON.finditer(text):
        out.append(html.escape(text[last:m.start()], quote=False))
        out.append(_span(m.lastgroup, m.group()))
        last = m.end()
    out.append(html.escape(text[last:], quote=False))
    return "".join(out)
