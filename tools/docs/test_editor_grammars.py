#!/usr/bin/env python3
"""The editor grammars know the contextual words of docs/design/AGENT_SYNTAX.md, and only where they are words
(python tools/docs/test_editor_grammars.py).

'prompt' is a keyword directly before a string and nowhere else, so the grammars must colour 'prompt "..."' and
its holes and must leave a variable, a parameter or a method called 'prompt' alone. The rules' regular
expressions are run here as Python regular expressions, which agree with the editors' engines for these patterns.
Checked: tools/lsp/clients/vscode/syntaxes/objeck.tmLanguage.json (also the source of the site highlighter's word
lists) and tools/lsp/clients/sublime/objeck.sublime-syntax.
"""
import io
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
VSCODE = os.path.join(ROOT, "tools", "lsp", "clients", "vscode", "syntaxes", "objeck.tmLanguage.json")
SUBLIME = os.path.join(ROOT, "tools", "lsp", "clients", "sublime", "objeck.sublime-syntax")

failures = []


def check(name, cond):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", name))
    if not cond:
        failures.append(name)


def sublime_rules(text, context):
    """The 'match' patterns of one context of the Sublime syntax, as Python regular expressions."""
    body = re.search(r"^  %s:\n((?:    .*\n|\n)*)" % re.escape(context), text, re.M)
    if not body:
        return []
    rules = []
    for raw in re.findall(r"^    - match: (.*)$", body.group(1), re.M):
        raw = raw.strip()
        if raw.startswith("'") and raw.endswith("'"):
            raw = raw[1:-1].replace("''", "'")
        raw = raw.replace("[[:alpha:]_]", "[A-Za-z_]").replace("[[:alnum:]_]", "[A-Za-z0-9_]")
        raw = raw.replace("{{identifier}}", r"\b[A-Za-z_][A-Za-z0-9_]*\b")
        rules.append(re.compile(raw))
    return rules


def prompt_checks(label, begin, hole):
    check("%s: 'prompt' before a string starts a prompt literal" % label, begin.search('p := prompt "Summarise {text}."') is not None)
    check("%s: with no space before the quote" % label, begin.search('F(prompt"x")') is not None)
    check("%s: a variable called prompt is left alone" % label, begin.search('prompt := "x";') is None and begin.search("prompt->Size()") is None)
    check("%s: a longer name is left alone" % label, begin.search('my_prompt "x"') is None and begin.search('prompts "x"') is None)
    typed = hole.search("in {count : Int} bullets")
    check("%s: a typed hole" % label, typed is not None and "count" in typed.groups() and "Int" in typed.groups())
    field = hole.search("read {@note}.")
    check("%s: a hole that names an instance variable" % label, field is not None and "@note" in field.groups())
    check("%s: a doubled brace is not a hole" % label, hole.search('{{"n": 1}}') is None)
    check("%s: an interpolation is not a hole" % label, hole.search("{$name}") is None)
    check("%s: two words are not a hole" % label, hole.search("{user name}") is None)


def main():
    with io.open(VSCODE, encoding="utf-8") as f:
        grammar = json.load(f)
    repo = grammar["repository"]
    order = [p["include"] for p in grammar["patterns"]]

    check("VS Code: prompt literals are tried before plain strings",
          "#prompt-literals" in order and order.index("#prompt-literals") < order.index("#strings"))
    literal = repo.get("prompt-literals", {})
    holes = [re.compile(p["match"]) for p in literal.get("patterns", []) if "captures" in p]
    if literal.get("begin") and holes:
        prompt_checks("VS Code", re.compile(literal["begin"]), holes[0])
    else:
        check("VS Code: the prompt literal rule exists", False)

    # the site highlighter takes its keywords from this grammar's plain word lists: a contextual word must not be in one
    words = set()
    for rule in repo.values():
        for pattern in rule.get("patterns", []):
            listed = re.match(r"\\b\(([\w|]+)\)\\b$", pattern.get("match", ""))
            if listed:
                words.update(listed.group(1).split("|"))
    check("VS Code: 'prompt' and 'agent' are in no plain keyword list", "prompt" not in words and "agent" not in words)

    with io.open(SUBLIME, encoding="utf-8") as f:
        sublime = f.read()
    main_rules = sublime_rules(sublime, "main")
    begins = [r for r in main_rules if r.pattern.startswith(r"\b(prompt)")]
    string_rules = sublime_rules(sublime, "prompt_string")
    holes = [r for r in string_rules if "String|Int|Float|Bool|Char|Byte" in r.pattern]
    if begins and holes:
        prompt_checks("Sublime", begins[0], holes[0])
        plain = [i for i, r in enumerate(main_rules) if r.pattern == '"']
        check("Sublime: prompt literals are tried before plain strings", bool(plain) and main_rules.index(begins[0]) < plain[0])
    else:
        check("Sublime: the prompt literal rule exists", False)

    if failures:
        print("FAIL: %d check(s) failed" % len(failures))
        return 1
    print("PASS: editor grammars")
    return 0


if __name__ == "__main__":
    sys.exit(main())
