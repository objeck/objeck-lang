#!/usr/bin/env python3
"""A known Objeck sample must come out with the expected token classes (python tools/docs/test_objeck_highlight.py)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import objeck_highlight as h  # noqa: E402


def classes(src):
    """[(class, text)] of the spans in the highlighted source."""
    import re
    return re.findall(r'<span class="t-(\w+)">(.*?)</span>', h.highlight_objeck(src), re.S)


class Highlight(unittest.TestCase):
    def test_sample(self):
        src = ('#~ doc ~#\nuse Collection;\nclass Circle from Shape {\n  @r : Float;\n'
               '  method : public : Area() ~ Float {\n    x := 2 * 3.5; # note\n    return "a\\"b";\n  }\n}\n')
        got = classes(src)
        for expected in [("com", "#~ doc ~#"), ("kw", "use"), ("kw", "class"), ("cls", "Circle"), ("kw", "from"),
                         ("cls", "Shape"), ("type", "Float"), ("kw", "method"), ("mod", "public"), ("fn", "Area"),
                         ("op", "~"), ("op", ":="), ("num", "2"), ("num", "3.5"), ("com", "# note"),
                         ("kw", "return"), ("str", '"a\\"b"')]:
            self.assertIn(expected, got)

    def test_escapes_html(self):
        out = h.highlight_objeck('x := Vector->New()<String>;')
        self.assertIn("&lt;", out)
        self.assertNotIn("<String>", out)

    def test_constants(self):
        self.assertIn(("const", "Nil"), classes("a := Nil; b := true;"))
        self.assertIn(("const", "true"), classes("a := Nil; b := true;"))

    def test_json(self):
        out = h.highlight_json('{"a": [1, true, null]}')
        self.assertIn('<span class="t-str">"a"</span>', out)
        self.assertIn('<span class="t-num">1</span>', out)
        self.assertIn('<span class="t-const">null</span>', out)

    def test_contextual_keyword_is(self):
        # 'is' is a keyword between a value and 'Type name)' and a name everywhere else
        got = classes("if(part is TextPart text) {\n  is := 3;\n  is += 1;\n};\n")
        self.assertEqual(got.count(("kw", "is")), 1)

    def test_contextual_keyword_json(self):
        # 'json' is a keyword before '{' and a name everywhere else
        got = classes('request := json{"n": 1};\njson := 3;\njson += 1;\n')
        self.assertEqual(got.count(("kw", "json")), 1)

    def test_contextual_keyword_match(self):
        # 'match' is a keyword before its subject and a name everywhere else
        got = classes("x := match c { Color->Red => 1, other => 2 };\nmatch := 3;\nmatch += 1;\n")
        self.assertEqual(got.count(("kw", "match")), 1)
        self.assertIn(("kw", "other"), got)

    def test_prompt_is_a_keyword_only_before_a_string(self):
        self.assertIn(("kw", "prompt"), classes('p := prompt "Summarise {text}.";'))
        self.assertNotIn(("kw", "prompt"), classes('prompt := "a name"; prompt->PrintLine();'))
        self.assertNotIn("prompt", h.KEYWORDS)

    def test_grammar_words_loaded(self):
        self.assertIn("while", h.KEYWORDS)
        self.assertIn("static", h.MODIFIERS)
        self.assertIn("Int", h.TYPES)


if __name__ == "__main__":
    unittest.main()
