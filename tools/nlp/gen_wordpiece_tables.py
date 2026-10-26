#!/usr/bin/env python3
"""Generate the Unicode tables System.NLP's WordPieceTokenizer needs.

BERT's BasicTokenizer is defined in terms of Python's Unicode database: a
character is punctuation when its general category starts with "P", a control
when it starts with "C", an accent when it is "Mn" after NFD, and lowercase is
str.lower(). Objeck's runtime has none of that -- Char->ToLower is ASCII-only
and there is no decomposition table -- so the tokenizer carries its own,
generated here from Python's unicodedata and written into nlp.obs between the
BEGIN/END GENERATED markers. Rerun after changing anything below; the output
is deterministic for a given Python (and so a given Unicode version, which the
generated block records).

Tables, all over every plane:
  punctuation   [start, end] pairs of the P* categories
  control       [start, end] pairs of Cc, Cf, Cs, Co. Cn (unassigned) is left
                out: Hugging Face drops an unassigned code point and this
                tokenizer keeps it, a difference only text using code points
                Unicode has not assigned can show.
  marks         [start, end] pairs of Mn, dropped when stripping accents
  lower         [start, end, step, delta] runs: cp -> cp + delta for every
                cp = start, start + step, ... <= end (str.lower() of one
                character). U+0130, the one character whose lowercase is two
                characters, is handled in code, as is the final sigma.
  strip         NFD of every character that has a canonical decomposition,
                with the Mn marks already removed: keys, offsets, values.
                Hangul syllables decompose algorithmically, in code.

usage: python tools/nlp/gen_wordpiece_tables.py   (rewrites core/compiler/lib_src/nlp.obs)
"""
import os
import sys
import unicodedata as ud

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TARGET = os.path.join(ROOT, "core", "compiler", "lib_src", "nlp.obs")
BEGIN = "\t# BEGIN GENERATED UNICODE TABLES (tools/nlp/gen_wordpiece_tables.py)"
END = "\t# END GENERATED UNICODE TABLES"
LIMIT = 0x110000


def ranges(pred):
    out, start = [], None
    for cp in range(LIMIT):
        hit = pred(cp)
        if hit and start is None:
            start = cp
        elif not hit and start is not None:
            out.append((start, cp - 1))
            start = None
    if start is not None:
        out.append((start, LIMIT - 1))
    return out


def lower_runs():
    pairs = []
    for cp in range(LIMIT):
        low = chr(cp).lower()
        if len(low) == 1 and low != chr(cp):
            pairs.append((cp, ord(low) - cp))
    runs = []
    for cp, delta in pairs:
        if runs:
            start, end, step, d = runs[-1]
            gap = cp - end
            if d == delta and gap in (1, 2) and (step == 0 or step == gap):
                runs[-1] = [start, cp, gap, d]
                continue
        runs.append([cp, cp, 0, delta])
    return [[s, e, (st or 1), d] for s, e, st, d in runs]


def strip_table():
    keys, offsets, values = [], [0], []
    for cp in range(LIMIT):
        if 0xAC00 <= cp <= 0xD7A3:
            continue  # Hangul: algorithmic
        ch = chr(cp)
        nfd = ud.normalize("NFD", ch)
        if nfd == ch:
            continue
        kept = [ord(c) for c in nfd if ud.category(c) != "Mn"]
        keys.append(cp)
        values.extend(kept)
        offsets.append(len(values))
    return keys, offsets, values


def literal(name, values, per_line=12):
    lines = []
    for i in range(0, len(values), per_line):
        chunk = ", ".join("0x%X" % v if v >= 0 else "%d" % v for v in values[i:i + per_line])
        lines.append("\t\t\t\t" + chunk)
    body = ",\n".join(lines)
    return ("\t\tfunction : private : %s() ~ Int[] {\n\t\t\treturn [\n%s\n\t\t\t];\n\t\t}\n" % (name, body))


def main():
    punct = ranges(lambda c: ud.category(chr(c)).startswith("P"))
    control = ranges(lambda c: ud.category(chr(c)) in ("Cc", "Cf", "Cs", "Co"))
    marks = ranges(lambda c: ud.category(chr(c)) == "Mn")
    lower = lower_runs()
    keys, offsets, values = strip_table()

    flat = lambda rs: [v for r in rs for v in r]
    block = [BEGIN,
             "\t# Unicode %s, from Python %s's unicodedata. Do not edit by hand." % (ud.unidata_version, sys.version.split()[0]),
             "\tclass : private : WordPieceUnicode {",
             "\t\t@punctuation : static : Int[];",
             "\t\t@control : static : Int[];",
             "\t\t@marks : static : Int[];",
             "\t\t@lower : static : Int[];",
             "\t\t@strip_keys : static : Int[];",
             "\t\t@strip_offsets : static : Int[];",
             "\t\t@strip_values : static : Int[];",
             "",
             "\t\t# Two threads racing here each build the same arrays; either result is right.",
             "\t\tfunction : Init() ~ Nil {",
             "\t\t\tif(@strip_values = Nil) {",
             "\t\t\t\t@punctuation := Punctuation();",
             "\t\t\t\t@control := Control();",
             "\t\t\t\t@marks := Marks();",
             "\t\t\t\t@lower := Lower();",
             "\t\t\t\t@strip_keys := StripKeys();",
             "\t\t\t\t@strip_offsets := StripOffsets();",
             "\t\t\t\t@strip_values := StripValues();",
             "\t\t\t};",
             "\t\t}",
             "",
             "\t\tfunction : GetPunctuation() ~ Int[] { Init(); return @punctuation; }",
             "\t\tfunction : GetControl() ~ Int[] { Init(); return @control; }",
             "\t\tfunction : GetMarks() ~ Int[] { Init(); return @marks; }",
             "\t\tfunction : GetLower() ~ Int[] { Init(); return @lower; }",
             "\t\tfunction : GetStripKeys() ~ Int[] { Init(); return @strip_keys; }",
             "\t\tfunction : GetStripOffsets() ~ Int[] { Init(); return @strip_offsets; }",
             "\t\tfunction : GetStripValues() ~ Int[] { Init(); return @strip_values; }",
             "",
             literal("Punctuation", flat(punct)),
             literal("Control", flat(control)),
             literal("Marks", flat(marks)),
             literal("Lower", flat(lower), 16),
             literal("StripKeys", keys),
             literal("StripOffsets", offsets),
             literal("StripValues", values),
             "\t}",
             END]
    generated = "\n".join(block)

    with open(TARGET, encoding="utf-8", newline="") as fh:
        source = fh.read()
    start = source.find(BEGIN)
    stop = source.find(END)
    if start < 0 or stop < 0:
        sys.exit("markers not found in %s" % TARGET)
    source = source[:start] + generated + source[stop + len(END):]
    with open(TARGET, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(source)
    print("punctuation %d ranges, control %d, marks %d, lower %d runs, strip %d characters (%d values)"
          % (len(punct), len(control), len(marks), len(lower), len(keys), len(values)))


if __name__ == "__main__":
    main()
